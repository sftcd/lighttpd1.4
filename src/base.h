#ifndef _BASE_H_
#define _BASE_H_
#include "first.h"

#include <sys/types.h>
#include <sys/time.h>

#include "base_decls.h"
#include "buffer.h"
#include "array.h"
#include "chunk.h"
#include "http_kv.h"
#include "sock_addr.h"

struct fdevents;        /* declaration */
struct stat_cache;      /* declaration */
struct cond_cache_t;    /* declaration */
struct cond_match_t;    /* declaration */

#define DIRECT 0        /* con->mode */


typedef struct {
	/** HEADER */
	/* the request-line */
	buffer *request;
	buffer *uri;

	buffer *orig_uri;

	http_method_t  http_method;
	http_version_t http_version;

	/* strings to the header */
	buffer *http_host; /* not alloced */

	unsigned int htags; /* bitfield of flagged headers present in request */
	array headers;

	/* CONTENT */
	off_t content_length; /* returned by strtoll() */
	off_t te_chunked;

	/* internal */
	buffer *pathinfo;
} request;

typedef struct {
	off_t   content_length;
	unsigned int htags; /* bitfield of flagged headers present in response */
	array headers;
	int send_chunked;
} response;

typedef struct {
	buffer *scheme; /* scheme without colon or slashes ( "http" or "https" ) */

	/* authority with optional portnumber ("site.name" or "site.name:8080" ) NOTE: without "username:password@" */
	buffer *authority;

	/* path including leading slash ("/" or "/index.html") - urldecoded, and sanitized  ( buffer_path_simplify() && buffer_urldecode_path() ) */
	buffer *path;
	buffer *path_raw; /* raw path, as sent from client. no urldecoding or path simplifying */
	buffer *query; /* querystring ( everything after "?", ie: in "/index.php?foo=1", query is "foo=1" ) */
} request_uri;

typedef struct {
	buffer *path;
	buffer *basedir; /* path = "(basedir)(.*)" */

	buffer *doc_root; /* path = doc_root + rel_path */
	buffer *rel_path;

	buffer *etag;
} physical;

typedef struct {
	const array *mimetypes;

	/* virtual-servers */
	const buffer *document_root;
	const buffer *server_name;
	const buffer *server_tag;
	log_error_st *errh;

	unsigned short max_keep_alive_requests;
	unsigned short max_keep_alive_idle;
	unsigned short max_read_idle;
	unsigned short max_write_idle;
	unsigned short stream_request_body;
	unsigned short stream_response_body;
	unsigned char high_precision_timestamps;
	unsigned char allow_http11;
	unsigned char follow_symlink;
	unsigned char etag_flags;
	unsigned char force_lowercase_filenames; /* if the FS is case-insensitive, force all files to lower-case */
	unsigned char use_xattr;
	unsigned char range_requests;
	unsigned char error_intercept;

	/* debug */

	unsigned char log_file_not_found;
	unsigned char log_request_header;
	unsigned char log_request_handling;
	unsigned char log_response_header;
	unsigned char log_condition_handling;
	unsigned char log_timeouts;

	unsigned int http_parseopts;
	unsigned int max_request_size;

	unsigned int bytes_per_second; /* connection bytes/sec limit */
	unsigned int global_bytes_per_second;/*total bytes/sec limit for scope*/

	/* server-wide traffic-shaper
	 *
	 * each context has the counter which is inited once
	 * a second by the global_bytes_per_second config-var
	 *
	 * as soon as global_bytes_per_second gets below 0
	 * the connected conns are "offline" a little bit
	 *
	 * the problem:
	 * we somehow have to loose our "we are writable" signal
	 * on the way.
	 *
	 */
	off_t *global_bytes_per_second_cnt_ptr; /*  */

	const buffer *error_handler;
	const buffer *error_handler_404;
	const buffer *errorfile_prefix;
	log_error_st *serrh; /* script errh */
} specific_config;

/* the order of the items should be the same as they are processed
 * read before write as we use this later */
typedef enum {
	CON_STATE_CONNECT,
	CON_STATE_REQUEST_START,
	CON_STATE_READ,
	CON_STATE_REQUEST_END,
	CON_STATE_READ_POST,
	CON_STATE_HANDLE_REQUEST,
	CON_STATE_RESPONSE_START,
	CON_STATE_WRITE,
	CON_STATE_RESPONSE_END,
	CON_STATE_ERROR,
	CON_STATE_CLOSE
} connection_state_t;

struct connection {
	connection_state_t state;

	/* timestamps */
	time_t read_idle_ts;
	time_t close_timeout_ts;
	time_t write_request_ts;

	time_t connection_start;
	time_t request_start;
	struct timespec request_start_hp;

	uint32_t request_count;      /* number of requests handled in this connection */
	uint32_t loops_per_request;  /* to catch endless loops in a single request
				      *
				      * used by mod_rewrite, mod_fastcgi, ... and others
				      * this is self-protection
				      */

	fdnode *fdn;                 /* fdevent (fdnode *) object */
	int fd;                      /* the FD for this connection */
	int ndx;                     /* reverse mapping to server->connection[ndx] */

	/* fd states */
	int is_readable;
	int is_writable;
	int is_ssl_sock;

	int keep_alive;              /* only request.c can enable it, all other just disable */
	int keep_alive_idle;         /* remember max_keep_alive_idle from config */

	int file_started;
	int file_finished;

	chunkqueue *write_queue;      /* a large queue for low-level write ( HTTP response ) [ file, mem ] */
	chunkqueue *read_queue;       /* a small queue for low-level read ( HTTP request ) [ mem ] */
	chunkqueue *request_content_queue; /* takes request-content into tempfile if necessary [ tempfile, mem ]*/

	int traffic_limit_reached;

	off_t bytes_written;          /* used by mod_accesslog, mod_rrd */
	off_t bytes_written_cur_second; /* used by mod_accesslog, mod_rrd */
	off_t bytes_read;             /* used by mod_accesslog, mod_rrd */
	off_t bytes_header;

	sock_addr dst_addr;
	buffer *dst_addr_buf;

	/* request */
	int http_status;
	uint32_t header_len;

	request  request;
	request_uri uri;
	physical physical;
	response response;

	array environment; /* used to pass lighttpd internal stuff to the FastCGI/CGI apps, setenv does that */

	int mode;                    /* DIRECT (0) or plugin id */
	int async_callback;

	server *srv;

	void *plugin_slots;
	void **plugin_ctx;           /* plugin connection specific config */

	specific_config conf;        /* global connection specific config */
	uint32_t conditional_is_valid;
	struct cond_cache_t *cond_cache;
	struct cond_match_t *cond_match;
	void *config_data_base;

	const buffer *server_name;
	buffer *server_name_buf;
	uint16_t proto_default_port;

	/* error-handler */
	int error_handler_saved_status;
	http_method_t error_handler_saved_method;

	struct server_socket *srv_socket;   /* reference to the server-socket */
	int (* network_write)(struct connection *con, chunkqueue *cq, off_t max_bytes);
	int (* network_read)(struct connection *con, chunkqueue *cq, off_t max_bytes);
};

typedef struct {
	connection **ptr;
	uint32_t size;
	uint32_t used;
} connections;

typedef struct {
	void *ptr;
	uint32_t used;
	uint32_t size;
} buffer_plugin;

typedef struct {
	unsigned int max_request_field_size;
	unsigned int log_state_handling;
	unsigned char log_request_header_on_error;

	/*(used sparsely, if at all, after config at startup)*/

	unsigned char http_header_strict;
	unsigned char http_host_strict;
	unsigned char http_host_normalize;
	unsigned char http_method_get_body;
	unsigned char high_precision_timestamps;
	unsigned short http_url_normalize;

	unsigned short max_worker;
	unsigned short max_fds;
	unsigned short max_conns;
	unsigned short port;

	unsigned int upload_temp_file_size;
	array *upload_tempdirs;

	unsigned char dont_daemonize;
	unsigned char preflight_check;
	unsigned char enable_cores;
	unsigned char compat_module_load;
	unsigned char config_deprecated;
	unsigned char config_unsupported;
	unsigned char systemd_socket_activation;
	unsigned char errorlog_use_syslog;
	const buffer *syslog_facility;
	const buffer *bindhost;
	const buffer *changeroot;
	const buffer *username;
	const buffer *groupname;
	const buffer *network_backend;
	const char *event_handler;
	buffer *pid_file;
	buffer *modules_dir;
	array *modules;
	array *config_touched;
	array empty_array;
} server_config;

typedef struct server_socket {
	sock_addr addr;
	int       fd;

	unsigned short is_ssl;
	unsigned short sidx;

	fdnode *fdn;
	server *srv;
	buffer *srv_token;
} server_socket;

typedef struct {
	server_socket **ptr;

	uint32_t size;
	uint32_t used;
} server_socket_array;

struct server {
	void *plugin_slots;

	struct fdevents *ev;
	int (* network_backend_write)(int fd, chunkqueue *cq, off_t max_bytes, log_error_st *errh);
	handler_t (* request_env)(connection *con);

	/* buffers */
	buffer *tmp_buf;

	connections conns;
	connections joblist;
	connections fdwaitqueue;

	/* counters */
	int con_opened;
	int con_read;
	int con_written;
	int con_closed;

	int max_fds;    /* max possible fds */
	int max_fds_lowat;/* low  watermark */
	int max_fds_hiwat;/* high watermark */
	int cur_fds;    /* currently used fds */
	int sockets_disabled;

	uint32_t max_conns;

	log_error_st *errh;

	server_config  srvconf;

	time_t loadts;
	double loadavg[3];

	/* config-file */
	void *config_data_base;
	array *config_context;

	/* members used at start-up or rarely used */
	server_socket_array srv_sockets;
	server_socket_array srv_sockets_inherited;
	buffer_plugin plugins;

	int event_handler;
	time_t startup_ts;

	uid_t uid;
	gid_t gid;
	pid_t pid;
};


#endif
