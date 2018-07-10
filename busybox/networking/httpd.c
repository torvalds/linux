/* vi: set sw=4 ts=4: */
/*
 * httpd implementation for busybox
 *
 * Copyright (C) 2002,2003 Glenn Engel <glenne@engel.org>
 * Copyright (C) 2003-2006 Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 *****************************************************************************
 *
 * Typical usage:
 * For non root user:
 *      httpd -p 8080 -h $HOME/public_html
 * For daemon start from rc script with uid=0:
 *      httpd -u www
 * which is equivalent to (assuming user www has uid 80):
 *      httpd -p 80 -u 80 -h $PWD -c /etc/httpd.conf -r "Web Server Authentication"
 *
 * When an url starts with "/cgi-bin/" it is assumed to be a cgi script.
 * The server changes directory to the location of the script and executes it
 * after setting QUERY_STRING and other environment variables.
 *
 * If directory URL is given, no index.html is found and CGI support is enabled,
 * cgi-bin/index.cgi will be run. Directory to list is ../$QUERY_STRING.
 * See httpd_indexcgi.c for an example GCI code.
 *
 * Doc:
 * "CGI Environment Variables": http://hoohoo.ncsa.uiuc.edu/cgi/env.html
 *
 * The applet can also be invoked as an url arg decoder and html text encoder
 * as follows:
 *      foo=`httpd -d $foo`             # decode "Hello%20World" as "Hello World"
 *      bar=`httpd -e "<Hello World>"`  # encode as "&#60Hello&#32World&#62"
 * Note that url encoding for arguments is not the same as html encoding for
 * presentation.  -d decodes an url-encoded argument while -e encodes in html
 * for page display.
 *
 * httpd.conf has the following format:
 *
 * H:/serverroot     # define the server root. It will override -h
 * A:172.20.         # Allow address from 172.20.0.0/16
 * A:10.0.0.0/25     # Allow any address from 10.0.0.0-10.0.0.127
 * A:10.0.0.0/255.255.255.128  # Allow any address that previous set
 * A:127.0.0.1       # Allow local loopback connections
 * D:*               # Deny from other IP connections
 * E404:/path/e404.html # /path/e404.html is the 404 (not found) error page
 * I:index.html      # Show index.html when a directory is requested
 *
 * P:/url:[http://]hostname[:port]/new/path
 *                   # When /urlXXXXXX is requested, reverse proxy
 *                   # it to http://hostname[:port]/new/pathXXXXXX
 *
 * /cgi-bin:foo:bar  # Require user foo, pwd bar on urls starting with /cgi-bin/
 * /adm:admin:setup  # Require user admin, pwd setup on urls starting with /adm/
 * /adm:toor:PaSsWd  # or user toor, pwd PaSsWd on urls starting with /adm/
 * /adm:root:*       # or user root, pwd from /etc/passwd on urls starting with /adm/
 * /wiki:*:*         # or any user from /etc/passwd with according pwd on urls starting with /wiki/
 * .au:audio/basic   # additional mime type for audio.au files
 * *.php:/path/php   # run xxx.php through an interpreter
 *
 * A/D may be as a/d or allow/deny - only first char matters.
 * Deny/Allow IP logic:
 *  - Default is to allow all (Allow all (A:*) is a no-op).
 *  - Deny rules take precedence over allow rules.
 *  - "Deny all" rule (D:*) is applied last.
 *
 * Example:
 *   1. Allow only specified addresses
 *     A:172.20          # Allow any address that begins with 172.20.
 *     A:10.10.          # Allow any address that begins with 10.10.
 *     A:127.0.0.1       # Allow local loopback connections
 *     D:*               # Deny from other IP connections
 *
 *   2. Only deny specified addresses
 *     D:1.2.3.        # deny from 1.2.3.0 - 1.2.3.255
 *     D:2.3.4.        # deny from 2.3.4.0 - 2.3.4.255
 *     A:*             # (optional line added for clarity)
 *
 * If a sub directory contains config file, it is parsed and merged with
 * any existing settings as if it was appended to the original configuration.
 *
 * subdir paths are relative to the containing subdir and thus cannot
 * affect the parent rules.
 *
 * Note that since the sub dir is parsed in the forked thread servicing the
 * subdir http request, any merge is discarded when the process exits.  As a
 * result, the subdir settings only have a lifetime of a single request.
 *
 * Custom error pages can contain an absolute path or be relative to
 * 'home_httpd'. Error pages are to be static files (no CGI or script). Error
 * page can only be defined in the root configuration file and are not taken
 * into account in local (directories) config files.
 *
 * If -c is not set, an attempt will be made to open the default
 * root configuration file.  If -c is set and the file is not found, the
 * server exits with an error.
 *
 */
 /* TODO: use TCP_CORK, parse_config() */
//config:config HTTPD
//config:	bool "httpd (32 kb)"
//config:	default y
//config:	help
//config:	HTTP server.
//config:
//config:config FEATURE_HTTPD_RANGES
//config:	bool "Support 'Ranges:' header"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	Makes httpd emit "Accept-Ranges: bytes" header and understand
//config:	"Range: bytes=NNN-[MMM]" header. Allows for resuming interrupted
//config:	downloads, seeking in multimedia players etc.
//config:
//config:config FEATURE_HTTPD_SETUID
//config:	bool "Enable -u <user> option"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	This option allows the server to run as a specific user
//config:	rather than defaulting to the user that starts the server.
//config:	Use of this option requires special privileges to change to a
//config:	different user.
//config:
//config:config FEATURE_HTTPD_BASIC_AUTH
//config:	bool "Enable HTTP authentication"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	Utilizes password settings from /etc/httpd.conf for basic
//config:	authentication on a per url basis.
//config:	Example for httpd.conf file:
//config:	/adm:toor:PaSsWd
//config:
//config:config FEATURE_HTTPD_AUTH_MD5
//config:	bool "Support MD5-encrypted passwords in HTTP authentication"
//config:	default y
//config:	depends on FEATURE_HTTPD_BASIC_AUTH
//config:	help
//config:	Enables encrypted passwords, and wildcard user/passwords
//config:	in httpd.conf file.
//config:	User '*' means 'any system user name is ok',
//config:	password of '*' means 'use system password for this user'
//config:	Examples:
//config:	/adm:toor:$1$P/eKnWXS$aI1aPGxT.dJD5SzqAKWrF0
//config:	/adm:root:*
//config:	/wiki:*:*
//config:
//config:config FEATURE_HTTPD_CGI
//config:	bool "Support Common Gateway Interface (CGI)"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	This option allows scripts and executables to be invoked
//config:	when specific URLs are requested.
//config:
//config:config FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
//config:	bool "Support running scripts through an interpreter"
//config:	default y
//config:	depends on FEATURE_HTTPD_CGI
//config:	help
//config:	This option enables support for running scripts through an
//config:	interpreter. Turn this on if you want PHP scripts to work
//config:	properly. You need to supply an additional line in your
//config:	httpd.conf file:
//config:	*.php:/path/to/your/php
//config:
//config:config FEATURE_HTTPD_SET_REMOTE_PORT_TO_ENV
//config:	bool "Set REMOTE_PORT environment variable for CGI"
//config:	default y
//config:	depends on FEATURE_HTTPD_CGI
//config:	help
//config:	Use of this option can assist scripts in generating
//config:	references that contain a unique port number.
//config:
//config:config FEATURE_HTTPD_ENCODE_URL_STR
//config:	bool "Enable -e option (useful for CGIs written as shell scripts)"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	This option allows html encoding of arbitrary strings for display
//config:	by the browser. Output goes to stdout.
//config:	For example, httpd -e "<Hello World>" produces
//config:	"&#60Hello&#32World&#62".
//config:
//config:config FEATURE_HTTPD_ERROR_PAGES
//config:	bool "Support custom error pages"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	This option allows you to define custom error pages in
//config:	the configuration file instead of the default HTTP status
//config:	error pages. For instance, if you add the line:
//config:		E404:/path/e404.html
//config:	in the config file, the server will respond the specified
//config:	'/path/e404.html' file instead of the terse '404 NOT FOUND'
//config:	message.
//config:
//config:config FEATURE_HTTPD_PROXY
//config:	bool "Support reverse proxy"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	This option allows you to define URLs that will be forwarded
//config:	to another HTTP server. To setup add the following line to the
//config:	configuration file
//config:		P:/url/:http://hostname[:port]/new/path/
//config:	Then a request to /url/myfile will be forwarded to
//config:	http://hostname[:port]/new/path/myfile.
//config:
//config:config FEATURE_HTTPD_GZIP
//config:	bool "Support GZIP content encoding"
//config:	default y
//config:	depends on HTTPD
//config:	help
//config:	Makes httpd send files using GZIP content encoding if the
//config:	client supports it and a pre-compressed <file>.gz exists.

//applet:IF_HTTPD(APPLET(httpd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_HTTPD) += httpd.o

//usage:#define httpd_trivial_usage
//usage:       "[-ifv[v]]"
//usage:       " [-c CONFFILE]"
//usage:       " [-p [IP:]PORT]"
//usage:	IF_FEATURE_HTTPD_SETUID(" [-u USER[:GRP]]")
//usage:	IF_FEATURE_HTTPD_BASIC_AUTH(" [-r REALM]")
//usage:       " [-h HOME]\n"
//usage:       "or httpd -d/-e" IF_FEATURE_HTTPD_AUTH_MD5("/-m") " STRING"
//usage:#define httpd_full_usage "\n\n"
//usage:       "Listen for incoming HTTP requests\n"
//usage:     "\n	-i		Inetd mode"
//usage:     "\n	-f		Don't daemonize"
//usage:     "\n	-v[v]		Verbose"
//usage:     "\n	-p [IP:]PORT	Bind to IP:PORT (default *:80)"
//usage:	IF_FEATURE_HTTPD_SETUID(
//usage:     "\n	-u USER[:GRP]	Set uid/gid after binding to port")
//usage:	IF_FEATURE_HTTPD_BASIC_AUTH(
//usage:     "\n	-r REALM	Authentication Realm for Basic Authentication")
//usage:     "\n	-h HOME		Home directory (default .)"
//usage:     "\n	-c FILE		Configuration file (default {/etc,HOME}/httpd.conf)"
//usage:	IF_FEATURE_HTTPD_AUTH_MD5(
//usage:     "\n	-m STRING	MD5 crypt STRING")
//usage:     "\n	-e STRING	HTML encode STRING"
//usage:     "\n	-d STRING	URL decode STRING"

#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_PAM
/* PAM may include <locale.h>. We may need to undefine bbox's stub define: */
# undef setlocale
/* For some obscure reason, PAM is not in pam/xxx, but in security/xxx.
 * Apparently they like to confuse people. */
# include <security/pam_appl.h>
# include <security/pam_misc.h>
#endif
#if ENABLE_FEATURE_USE_SENDFILE
# include <sys/sendfile.h>
#endif
/* amount of buffering in a pipe */
#ifndef PIPE_BUF
# define PIPE_BUF 4096
#endif

#define DEBUG 0

#define IOBUF_SIZE 8192
#if PIPE_BUF >= IOBUF_SIZE
# error "PIPE_BUF >= IOBUF_SIZE"
#endif

#define HEADER_READ_TIMEOUT 60

static const char DEFAULT_PATH_HTTPD_CONF[] ALIGN1 = "/etc";
static const char HTTPD_CONF[] ALIGN1 = "httpd.conf";
static const char HTTP_200[] ALIGN1 = "HTTP/1.0 200 OK\r\n";
static const char index_html[] ALIGN1 = "index.html";

typedef struct has_next_ptr {
	struct has_next_ptr *next;
} has_next_ptr;

/* Must have "next" as a first member */
typedef struct Htaccess {
	struct Htaccess *next;
	char *after_colon;
	char before_colon[1];  /* really bigger, must be last */
} Htaccess;

/* Must have "next" as a first member */
typedef struct Htaccess_IP {
	struct Htaccess_IP *next;
	unsigned ip;
	unsigned mask;
	int allow_deny;
} Htaccess_IP;

/* Must have "next" as a first member */
typedef struct Htaccess_Proxy {
	struct Htaccess_Proxy *next;
	char *url_from;
	char *host_port;
	char *url_to;
} Htaccess_Proxy;

enum {
	HTTP_OK = 200,
	HTTP_PARTIAL_CONTENT = 206,
	HTTP_MOVED_TEMPORARILY = 302,
	HTTP_BAD_REQUEST = 400,       /* malformed syntax */
	HTTP_UNAUTHORIZED = 401, /* authentication needed, respond with auth hdr */
	HTTP_NOT_FOUND = 404,
	HTTP_FORBIDDEN = 403,
	HTTP_REQUEST_TIMEOUT = 408,
	HTTP_NOT_IMPLEMENTED = 501,   /* used for unrecognized requests */
	HTTP_INTERNAL_SERVER_ERROR = 500,
	HTTP_CONTINUE = 100,
#if 0   /* future use */
	HTTP_SWITCHING_PROTOCOLS = 101,
	HTTP_CREATED = 201,
	HTTP_ACCEPTED = 202,
	HTTP_NON_AUTHORITATIVE_INFO = 203,
	HTTP_NO_CONTENT = 204,
	HTTP_MULTIPLE_CHOICES = 300,
	HTTP_MOVED_PERMANENTLY = 301,
	HTTP_NOT_MODIFIED = 304,
	HTTP_PAYMENT_REQUIRED = 402,
	HTTP_BAD_GATEWAY = 502,
	HTTP_SERVICE_UNAVAILABLE = 503, /* overload, maintenance */
#endif
};

static const uint16_t http_response_type[] ALIGN2 = {
	HTTP_OK,
#if ENABLE_FEATURE_HTTPD_RANGES
	HTTP_PARTIAL_CONTENT,
#endif
	HTTP_MOVED_TEMPORARILY,
	HTTP_REQUEST_TIMEOUT,
	HTTP_NOT_IMPLEMENTED,
#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	HTTP_UNAUTHORIZED,
#endif
	HTTP_NOT_FOUND,
	HTTP_BAD_REQUEST,
	HTTP_FORBIDDEN,
	HTTP_INTERNAL_SERVER_ERROR,
#if 0   /* not implemented */
	HTTP_CREATED,
	HTTP_ACCEPTED,
	HTTP_NO_CONTENT,
	HTTP_MULTIPLE_CHOICES,
	HTTP_MOVED_PERMANENTLY,
	HTTP_NOT_MODIFIED,
	HTTP_BAD_GATEWAY,
	HTTP_SERVICE_UNAVAILABLE,
#endif
};

static const struct {
	const char *name;
	const char *info;
} http_response[ARRAY_SIZE(http_response_type)] = {
	{ "OK", NULL },
#if ENABLE_FEATURE_HTTPD_RANGES
	{ "Partial Content", NULL },
#endif
	{ "Found", NULL },
	{ "Request Timeout", "No request appeared within 60 seconds" },
	{ "Not Implemented", "The requested method is not recognized" },
#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	{ "Unauthorized", "" },
#endif
	{ "Not Found", "The requested URL was not found" },
	{ "Bad Request", "Unsupported method" },
	{ "Forbidden", ""  },
	{ "Internal Server Error", "Internal Server Error" },
#if 0   /* not implemented */
	{ "Created" },
	{ "Accepted" },
	{ "No Content" },
	{ "Multiple Choices" },
	{ "Moved Permanently" },
	{ "Not Modified" },
	{ "Bad Gateway", "" },
	{ "Service Unavailable", "" },
#endif
};

struct globals {
	int verbose;            /* must be int (used by getopt32) */
	smallint flg_deny_all;
#if ENABLE_FEATURE_HTTPD_GZIP
	/* client can handle gzip / we are going to send gzip */
	smallint content_gzip;
#endif
	unsigned rmt_ip;        /* used for IP-based allow/deny rules */
	time_t last_mod;
	char *rmt_ip_str;       /* for $REMOTE_ADDR and $REMOTE_PORT */
	const char *bind_addr_or_port;

	const char *g_query;
	const char *opt_c_configFile;
	const char *home_httpd;
	const char *index_page;

	const char *found_mime_type;
	const char *found_moved_temporarily;
	Htaccess_IP *ip_a_d;    /* config allow/deny lines */

	IF_FEATURE_HTTPD_BASIC_AUTH(const char *g_realm;)
	IF_FEATURE_HTTPD_BASIC_AUTH(char *remoteuser;)
	IF_FEATURE_HTTPD_CGI(char *referer;)
	IF_FEATURE_HTTPD_CGI(char *user_agent;)
	IF_FEATURE_HTTPD_CGI(char *host;)
	IF_FEATURE_HTTPD_CGI(char *http_accept;)
	IF_FEATURE_HTTPD_CGI(char *http_accept_language;)

	off_t file_size;        /* -1 - unknown */
#if ENABLE_FEATURE_HTTPD_RANGES
	off_t range_start;
	off_t range_end;
	off_t range_len;
#endif

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	Htaccess *g_auth;       /* config user:password lines */
#endif
	Htaccess *mime_a;       /* config mime types */
#if ENABLE_FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
	Htaccess *script_i;     /* config script interpreters */
#endif
	char *iobuf;            /* [IOBUF_SIZE] */
#define        hdr_buf bb_common_bufsiz1
#define sizeof_hdr_buf COMMON_BUFSIZE
	char *hdr_ptr;
	int hdr_cnt;
#if ENABLE_FEATURE_HTTPD_ERROR_PAGES
	const char *http_error_page[ARRAY_SIZE(http_response_type)];
#endif
#if ENABLE_FEATURE_HTTPD_PROXY
	Htaccess_Proxy *proxy;
#endif
};
#define G (*ptr_to_globals)
#define verbose           (G.verbose          )
#define flg_deny_all      (G.flg_deny_all     )
#if ENABLE_FEATURE_HTTPD_GZIP
# define content_gzip     (G.content_gzip     )
#else
# define content_gzip     0
#endif
#define rmt_ip            (G.rmt_ip           )
#define bind_addr_or_port (G.bind_addr_or_port)
#define g_query           (G.g_query          )
#define opt_c_configFile  (G.opt_c_configFile )
#define home_httpd        (G.home_httpd       )
#define index_page        (G.index_page       )
#define found_mime_type   (G.found_mime_type  )
#define found_moved_temporarily (G.found_moved_temporarily)
#define last_mod          (G.last_mod         )
#define ip_a_d            (G.ip_a_d           )
#define g_realm           (G.g_realm          )
#define remoteuser        (G.remoteuser       )
#define file_size         (G.file_size        )
#if ENABLE_FEATURE_HTTPD_RANGES
#define range_start       (G.range_start      )
#define range_end         (G.range_end        )
#define range_len         (G.range_len        )
#else
enum {
	range_start = -1,
	range_end = MAXINT(off_t) - 1,
	range_len = MAXINT(off_t),
};
#endif
#define rmt_ip_str        (G.rmt_ip_str       )
#define g_auth            (G.g_auth           )
#define mime_a            (G.mime_a           )
#define script_i          (G.script_i         )
#define iobuf             (G.iobuf            )
#define hdr_ptr           (G.hdr_ptr          )
#define hdr_cnt           (G.hdr_cnt          )
#define http_error_page   (G.http_error_page  )
#define proxy             (G.proxy            )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	IF_FEATURE_HTTPD_BASIC_AUTH(g_realm = "Web Server Authentication";) \
	IF_FEATURE_HTTPD_RANGES(range_start = -1;) \
	bind_addr_or_port = "80"; \
	index_page = index_html; \
	file_size = -1; \
} while (0)


#define STRNCASECMP(a, str) strncasecmp((a), (str), sizeof(str)-1)

/* Prototypes */
enum {
	SEND_HEADERS     = (1 << 0),
	SEND_BODY        = (1 << 1),
	SEND_HEADERS_AND_BODY = SEND_HEADERS + SEND_BODY,
};
static void send_file_and_exit(const char *url, int what) NORETURN;

static void free_llist(has_next_ptr **pptr)
{
	has_next_ptr *cur = *pptr;
	while (cur) {
		has_next_ptr *t = cur;
		cur = cur->next;
		free(t);
	}
	*pptr = NULL;
}

static ALWAYS_INLINE void free_Htaccess_list(Htaccess **pptr)
{
	free_llist((has_next_ptr**)pptr);
}

static ALWAYS_INLINE void free_Htaccess_IP_list(Htaccess_IP **pptr)
{
	free_llist((has_next_ptr**)pptr);
}

/* Returns presumed mask width in bits or < 0 on error.
 * Updates strp, stores IP at provided pointer */
static int scan_ip(const char **strp, unsigned *ipp, unsigned char endc)
{
	const char *p = *strp;
	int auto_mask = 8;
	unsigned ip = 0;
	int j;

	if (*p == '/')
		return -auto_mask;

	for (j = 0; j < 4; j++) {
		unsigned octet;

		if ((*p < '0' || *p > '9') && *p != '/' && *p)
			return -auto_mask;
		octet = 0;
		while (*p >= '0' && *p <= '9') {
			octet *= 10;
			octet += *p - '0';
			if (octet > 255)
				return -auto_mask;
			p++;
		}
		if (*p == '.')
			p++;
		if (*p != '/' && *p)
			auto_mask += 8;
		ip = (ip << 8) | octet;
	}
	if (*p) {
		if (*p != endc)
			return -auto_mask;
		p++;
		if (*p == '\0')
			return -auto_mask;
	}
	*ipp = ip;
	*strp = p;
	return auto_mask;
}

/* Returns 0 on success. Stores IP and mask at provided pointers */
static int scan_ip_mask(const char *str, unsigned *ipp, unsigned *maskp)
{
	int i;
	unsigned mask;
	char *p;

	i = scan_ip(&str, ipp, '/');
	if (i < 0)
		return i;

	if (*str) {
		/* there is /xxx after dotted-IP address */
		i = bb_strtou(str, &p, 10);
		if (*p == '.') {
			/* 'xxx' itself is dotted-IP mask, parse it */
			/* (return 0 (success) only if it has N.N.N.N form) */
			return scan_ip(&str, maskp, '\0') - 32;
		}
		if (*p)
			return -1;
	}

	if (i > 32)
		return -1;

	if (sizeof(unsigned) == 4 && i == 32) {
		/* mask >>= 32 below may not work */
		mask = 0;
	} else {
		mask = 0xffffffff;
		mask >>= i;
	}
	/* i == 0 -> *maskp = 0x00000000
	 * i == 1 -> *maskp = 0x80000000
	 * i == 4 -> *maskp = 0xf0000000
	 * i == 31 -> *maskp = 0xfffffffe
	 * i == 32 -> *maskp = 0xffffffff */
	*maskp = (uint32_t)(~mask);
	return 0;
}

/*
 * Parse configuration file into in-memory linked list.
 *
 * Any previous IP rules are discarded.
 * If the flag argument is not SUBDIR_PARSE then all /path and mime rules
 * are also discarded.  That is, previous settings are retained if flag is
 * SUBDIR_PARSE.
 * Error pages are only parsed on the main config file.
 *
 * path   Path where to look for httpd.conf (without filename).
 * flag   Type of the parse request.
 */
/* flag param: */
enum {
	FIRST_PARSE    = 0, /* path will be "/etc" */
	SIGNALED_PARSE = 1, /* path will be "/etc" */
	SUBDIR_PARSE   = 2, /* path will be derived from URL */
};
static void parse_conf(const char *path, int flag)
{
	/* internally used extra flag state */
	enum { TRY_CURDIR_PARSE = 3 };

	FILE *f;
	const char *filename;
	char buf[160];

	/* discard old rules */
	free_Htaccess_IP_list(&ip_a_d);
	flg_deny_all = 0;
	/* retain previous auth and mime config only for subdir parse */
	if (flag != SUBDIR_PARSE) {
		free_Htaccess_list(&mime_a);
#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
		free_Htaccess_list(&g_auth);
#endif
#if ENABLE_FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
		free_Htaccess_list(&script_i);
#endif
	}

	filename = opt_c_configFile;
	if (flag == SUBDIR_PARSE || filename == NULL) {
		filename = alloca(strlen(path) + sizeof(HTTPD_CONF) + 2);
		sprintf((char *)filename, "%s/%s", path, HTTPD_CONF);
	}

	while ((f = fopen_for_read(filename)) == NULL) {
		if (flag >= SUBDIR_PARSE) { /* SUBDIR or TRY_CURDIR */
			/* config file not found, no changes to config */
			return;
		}
		if (flag == FIRST_PARSE) {
			/* -c CONFFILE given, but CONFFILE doesn't exist? */
			if (opt_c_configFile)
				bb_simple_perror_msg_and_die(opt_c_configFile);
			/* else: no -c, thus we looked at /etc/httpd.conf,
			 * and it's not there. try ./httpd.conf: */
		}
		flag = TRY_CURDIR_PARSE;
		filename = HTTPD_CONF;
	}

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	/* in "/file:user:pass" lines, we prepend path in subdirs */
	if (flag != SUBDIR_PARSE)
		path = "";
#endif
	/* The lines can be:
	 *
	 * I:default_index_file
	 * H:http_home
	 * [AD]:IP[/mask]   # allow/deny, * for wildcard
	 * Ennn:error.html  # error page for status nnn
	 * P:/url:[http://]hostname[:port]/new/path # reverse proxy
	 * .ext:mime/type   # mime type
	 * *.php:/path/php  # run xxx.php through an interpreter
	 * /file:user:pass  # username and password
	 */
	while (fgets(buf, sizeof(buf), f) != NULL) {
		unsigned strlen_buf;
		unsigned char ch;
		char *after_colon;

		{ /* remove all whitespace, and # comments */
			char *p, *p0;

			p0 = buf;
			/* skip non-whitespace beginning. Often the whole line
			 * is non-whitespace. We want this case to work fast,
			 * without needless copying, therefore we don't merge
			 * this operation into next while loop. */
			while ((ch = *p0) != '\0' && ch != '\n' && ch != '#'
			 && ch != ' ' && ch != '\t'
			) {
				p0++;
			}
			p = p0;
			/* if we enter this loop, we have some whitespace.
			 * discard it */
			while (ch != '\0' && ch != '\n' && ch != '#') {
				if (ch != ' ' && ch != '\t') {
					*p++ = ch;
				}
				ch = *++p0;
			}
			*p = '\0';
			strlen_buf = p - buf;
			if (strlen_buf == 0)
				continue; /* empty line */
		}

		after_colon = strchr(buf, ':');
		/* strange line? */
		if (after_colon == NULL || *++after_colon == '\0')
			goto config_error;

		ch = (buf[0] & ~0x20); /* toupper if it's a letter */

		if (ch == 'I') {
			if (index_page != index_html)
				free((char*)index_page);
			index_page = xstrdup(after_colon);
			continue;
		}

		/* do not allow jumping around using H in subdir's configs */
		if (flag == FIRST_PARSE && ch == 'H') {
			home_httpd = xstrdup(after_colon);
			xchdir(home_httpd);
			continue;
		}

		if (ch == 'A' || ch == 'D') {
			Htaccess_IP *pip;

			if (*after_colon == '*') {
				if (ch == 'D') {
					/* memorize "deny all" */
					flg_deny_all = 1;
				}
				/* skip assumed "A:*", it is a default anyway */
				continue;
			}
			/* store "allow/deny IP/mask" line */
			pip = xzalloc(sizeof(*pip));
			if (scan_ip_mask(after_colon, &pip->ip, &pip->mask)) {
				/* IP{/mask} syntax error detected, protect all */
				ch = 'D';
				pip->mask = 0;
			}
			pip->allow_deny = ch;
			if (ch == 'D') {
				/* Deny:from_IP - prepend */
				pip->next = ip_a_d;
				ip_a_d = pip;
			} else {
				/* A:from_IP - append (thus all D's precedes A's) */
				Htaccess_IP *prev_IP = ip_a_d;
				if (prev_IP == NULL) {
					ip_a_d = pip;
				} else {
					while (prev_IP->next)
						prev_IP = prev_IP->next;
					prev_IP->next = pip;
				}
			}
			continue;
		}

#if ENABLE_FEATURE_HTTPD_ERROR_PAGES
		if (flag == FIRST_PARSE && ch == 'E') {
			unsigned i;
			int status = atoi(buf + 1); /* error status code */

			if (status < HTTP_CONTINUE) {
				goto config_error;
			}
			/* then error page; find matching status */
			for (i = 0; i < ARRAY_SIZE(http_response_type); i++) {
				if (http_response_type[i] == status) {
					/* We chdir to home_httpd, thus no need to
					 * concat_path_file(home_httpd, after_colon)
					 * here */
					http_error_page[i] = xstrdup(after_colon);
					break;
				}
			}
			continue;
		}
#endif

#if ENABLE_FEATURE_HTTPD_PROXY
		if (flag == FIRST_PARSE && ch == 'P') {
			/* P:/url:[http://]hostname[:port]/new/path */
			char *url_from, *host_port, *url_to;
			Htaccess_Proxy *proxy_entry;

			url_from = after_colon;
			host_port = strchr(after_colon, ':');
			if (host_port == NULL) {
				goto config_error;
			}
			*host_port++ = '\0';
			if (is_prefixed_with(host_port, "http://"))
				host_port += 7;
			if (*host_port == '\0') {
				goto config_error;
			}
			url_to = strchr(host_port, '/');
			if (url_to == NULL) {
				goto config_error;
			}
			*url_to = '\0';
			proxy_entry = xzalloc(sizeof(*proxy_entry));
			proxy_entry->url_from = xstrdup(url_from);
			proxy_entry->host_port = xstrdup(host_port);
			*url_to = '/';
			proxy_entry->url_to = xstrdup(url_to);
			proxy_entry->next = proxy;
			proxy = proxy_entry;
			continue;
		}
#endif
		/* the rest of directives are non-alphabetic,
		 * must avoid using "toupper'ed" ch */
		ch = buf[0];

		if (ch == '.' /* ".ext:mime/type" */
#if ENABLE_FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
		 || (ch == '*' && buf[1] == '.') /* "*.php:/path/php" */
#endif
		) {
			char *p;
			Htaccess *cur;

			cur = xzalloc(sizeof(*cur) /* includes space for NUL */ + strlen_buf);
			strcpy(cur->before_colon, buf);
			p = cur->before_colon + (after_colon - buf);
			p[-1] = '\0';
			cur->after_colon = p;
			if (ch == '.') {
				/* .mime line: prepend to mime_a list */
				cur->next = mime_a;
				mime_a = cur;
			}
#if ENABLE_FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
			else {
				/* script interpreter line: prepend to script_i list */
				cur->next = script_i;
				script_i = cur;
			}
#endif
			continue;
		}

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
		if (ch == '/') { /* "/file:user:pass" */
			char *p;
			Htaccess *cur;
			unsigned file_len;

			/* note: path is "" unless we are in SUBDIR parse,
			 * otherwise it does NOT start with "/" */
			cur = xzalloc(sizeof(*cur) /* includes space for NUL */
				+ 1 + strlen(path)
				+ strlen_buf
				);
			/* form "/path/file" */
			sprintf(cur->before_colon, "/%s%.*s",
				path,
				(int) (after_colon - buf - 1), /* includes "/", but not ":" */
				buf);
			/* canonicalize it */
			p = bb_simplify_abs_path_inplace(cur->before_colon);
			file_len = p - cur->before_colon;
			/* add "user:pass" after NUL */
			strcpy(++p, after_colon);
			cur->after_colon = p;

			/* insert cur into g_auth */
			/* g_auth is sorted by decreased filename length */
			{
				Htaccess *auth, **authp;

				authp = &g_auth;
				while ((auth = *authp) != NULL) {
					if (file_len >= strlen(auth->before_colon)) {
						/* insert cur before auth */
						cur->next = auth;
						break;
					}
					authp = &auth->next;
				}
				*authp = cur;
			}
			continue;
		}
#endif /* BASIC_AUTH */

		/* the line is not recognized */
 config_error:
		bb_error_msg("config error '%s' in '%s'", buf, filename);
	} /* while (fgets) */

	fclose(f);
}

#if ENABLE_FEATURE_HTTPD_ENCODE_URL_STR
/*
 * Given a string, html-encode special characters.
 * This is used for the -e command line option to provide an easy way
 * for scripts to encode result data without confusing browsers.  The
 * returned string pointer is memory allocated by malloc().
 *
 * Returns a pointer to the encoded string (malloced).
 */
static char *encodeString(const char *string)
{
	/* take the simple route and encode everything */
	/* could possibly scan once to get length.     */
	int len = strlen(string);
	char *out = xmalloc(len * 6 + 1);
	char *p = out;
	char ch;

	while ((ch = *string++) != '\0') {
		/* very simple check for what to encode */
		if (isalnum(ch))
			*p++ = ch;
		else
			p += sprintf(p, "&#%u;", (unsigned char) ch);
	}
	*p = '\0';
	return out;
}
#endif

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
/*
 * Decode a base64 data stream as per rfc1521.
 * Note that the rfc states that non base64 chars are to be ignored.
 * Since the decode always results in a shorter size than the input,
 * it is OK to pass the input arg as an output arg.
 * Parameter: a pointer to a base64 encoded string.
 * Decoded data is stored in-place.
 */
static void decodeBase64(char *Data)
{
	const unsigned char *in = (const unsigned char *)Data;
	/* The decoded size will be at most 3/4 the size of the encoded */
	unsigned ch = 0;
	int i = 0;

	while (*in) {
		int t = *in++;

		if (t >= '0' && t <= '9')
			t = t - '0' + 52;
		else if (t >= 'A' && t <= 'Z')
			t = t - 'A';
		else if (t >= 'a' && t <= 'z')
			t = t - 'a' + 26;
		else if (t == '+')
			t = 62;
		else if (t == '/')
			t = 63;
		else if (t == '=')
			t = 0;
		else
			continue;

		ch = (ch << 6) | t;
		i++;
		if (i == 4) {
			*Data++ = (char) (ch >> 16);
			*Data++ = (char) (ch >> 8);
			*Data++ = (char) ch;
			i = 0;
		}
	}
	*Data = '\0';
}
#endif

/*
 * Create a listen server socket on the designated port.
 */
static int openServer(void)
{
	unsigned n = bb_strtou(bind_addr_or_port, NULL, 10);
	if (!errno && n && n <= 0xffff)
		n = create_and_bind_stream_or_die(NULL, n);
	else
		n = create_and_bind_stream_or_die(bind_addr_or_port, 80);
	xlisten(n, 9);
	return n;
}

/*
 * Log the connection closure and exit.
 */
static void log_and_exit(void) NORETURN;
static void log_and_exit(void)
{
	/* Paranoia. IE said to be buggy. It may send some extra data
	 * or be confused by us just exiting without SHUT_WR. Oh well. */
	shutdown(1, SHUT_WR);
	/* Why??
	(this also messes up stdin when user runs httpd -i from terminal)
	ndelay_on(0);
	while (read(STDIN_FILENO, iobuf, IOBUF_SIZE) > 0)
		continue;
	*/

	if (verbose > 2)
		bb_error_msg("closed");
	_exit(xfunc_error_retval);
}

/*
 * Create and send HTTP response headers.
 * The arguments are combined and sent as one write operation.  Note that
 * IE will puke big-time if the headers are not sent in one packet and the
 * second packet is delayed for any reason.
 * responseNum - the result code to send.
 */
static void send_headers(unsigned responseNum)
{
	static const char RFC1123FMT[] ALIGN1 = "%a, %d %b %Y %H:%M:%S GMT";
	/* Fixed size 29-byte string. Example: Sun, 06 Nov 1994 08:49:37 GMT */
	char date_str[40]; /* using a bit larger buffer to paranoia reasons */

	struct tm tm;
	const char *responseString = "";
	const char *infoString = NULL;
#if ENABLE_FEATURE_HTTPD_ERROR_PAGES
	const char *error_page = NULL;
#endif
	unsigned len;
	unsigned i;
	time_t timer = time(NULL);

	for (i = 0; i < ARRAY_SIZE(http_response_type); i++) {
		if (http_response_type[i] == responseNum) {
			responseString = http_response[i].name;
			infoString = http_response[i].info;
#if ENABLE_FEATURE_HTTPD_ERROR_PAGES
			error_page = http_error_page[i];
#endif
			break;
		}
	}

	if (verbose)
		bb_error_msg("response:%u", responseNum);

	/* We use sprintf, not snprintf (it's less code).
	 * iobuf[] is several kbytes long and all headers we generate
	 * always fit into those kbytes.
	 */

	strftime(date_str, sizeof(date_str), RFC1123FMT, gmtime_r(&timer, &tm));
	/* ^^^ using gmtime_r() instead of gmtime() to not use static data */
	len = sprintf(iobuf,
			"HTTP/1.0 %u %s\r\n"
			"Date: %s\r\n"
			"Connection: close\r\n",
			responseNum, responseString,
			date_str
	);

	if (responseNum != HTTP_OK || found_mime_type) {
		len += sprintf(iobuf + len,
				"Content-type: %s\r\n",
				/* if it's error message, then it's HTML */
				(responseNum != HTTP_OK ? "text/html" : found_mime_type)
		);
	}

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	if (responseNum == HTTP_UNAUTHORIZED) {
		len += sprintf(iobuf + len,
				"WWW-Authenticate: Basic realm=\"%.999s\"\r\n",
				g_realm /* %.999s protects from overflowing iobuf[] */
		);
	}
#endif
	if (responseNum == HTTP_MOVED_TEMPORARILY) {
		/* Responding to "GET /dir" with
		 * "HTTP/1.0 302 Found" "Location: /dir/"
		 * - IOW, asking them to repeat with a slash.
		 * Here, overflow IS possible, can't use sprintf:
		 * mkdir test
		 * python -c 'print("get /test?" + ("x" * 8192))' | busybox httpd -i -h .
		 */
		len += snprintf(iobuf + len, IOBUF_SIZE-3 - len,
				"Location: %s/%s%s\r\n",
				found_moved_temporarily,
				(g_query ? "?" : ""),
				(g_query ? g_query : "")
		);
		if (len > IOBUF_SIZE-3)
			len = IOBUF_SIZE-3;
	}

#if ENABLE_FEATURE_HTTPD_ERROR_PAGES
	if (error_page && access(error_page, R_OK) == 0) {
		iobuf[len++] = '\r';
		iobuf[len++] = '\n';
		if (DEBUG) {
			iobuf[len] = '\0';
			fprintf(stderr, "headers: '%s'\n", iobuf);
		}
		full_write(STDOUT_FILENO, iobuf, len);
		if (DEBUG)
			fprintf(stderr, "writing error page: '%s'\n", error_page);
		return send_file_and_exit(error_page, SEND_BODY);
	}
#endif

	if (file_size != -1) {    /* file */
		strftime(date_str, sizeof(date_str), RFC1123FMT, gmtime_r(&last_mod, &tm));
#if ENABLE_FEATURE_HTTPD_RANGES
		if (responseNum == HTTP_PARTIAL_CONTENT) {
			len += sprintf(iobuf + len,
				"Content-Range: bytes %"OFF_FMT"u-%"OFF_FMT"u/%"OFF_FMT"u\r\n",
					range_start,
					range_end,
					file_size
			);
			file_size = range_end - range_start + 1;
		}
#endif
		len += sprintf(iobuf + len,
#if ENABLE_FEATURE_HTTPD_RANGES
			"Accept-Ranges: bytes\r\n"
#endif
			"Last-Modified: %s\r\n"
			"%s-Length: %"OFF_FMT"u\r\n",
				date_str,
				content_gzip ? "Transfer" : "Content",
				file_size
		);
	}

	if (content_gzip)
		len += sprintf(iobuf + len, "Content-Encoding: gzip\r\n");

	iobuf[len++] = '\r';
	iobuf[len++] = '\n';
	if (infoString) {
		len += sprintf(iobuf + len,
				"<HTML><HEAD><TITLE>%u %s</TITLE></HEAD>\n"
				"<BODY><H1>%u %s</H1>\n"
				"%s\n"
				"</BODY></HTML>\n",
				responseNum, responseString,
				responseNum, responseString,
				infoString
		);
	}
	if (DEBUG) {
		iobuf[len] = '\0';
		fprintf(stderr, "headers: '%s'\n", iobuf);
	}
	if (full_write(STDOUT_FILENO, iobuf, len) != len) {
		if (verbose > 1)
			bb_perror_msg("error");
		log_and_exit();
	}
}

static void send_headers_and_exit(int responseNum) NORETURN;
static void send_headers_and_exit(int responseNum)
{
	IF_FEATURE_HTTPD_GZIP(content_gzip = 0;)
	send_headers(responseNum);
	log_and_exit();
}

/*
 * Read from the socket until '\n' or EOF. '\r' chars are removed.
 * '\n' is replaced with NUL.
 * Return number of characters read or 0 if nothing is read
 * ('\r' and '\n' are not counted).
 * Data is returned in iobuf.
 */
static int get_line(void)
{
	int count = 0;
	char c;

	alarm(HEADER_READ_TIMEOUT);
	while (1) {
		if (hdr_cnt <= 0) {
			hdr_cnt = safe_read(STDIN_FILENO, hdr_buf, sizeof_hdr_buf);
			if (hdr_cnt <= 0)
				break;
			hdr_ptr = hdr_buf;
		}
		iobuf[count] = c = *hdr_ptr++;
		hdr_cnt--;

		if (c == '\r')
			continue;
		if (c == '\n') {
			iobuf[count] = '\0';
			break;
		}
		if (count < (IOBUF_SIZE - 1))      /* check overflow */
			count++;
	}
	return count;
}

#if ENABLE_FEATURE_HTTPD_CGI || ENABLE_FEATURE_HTTPD_PROXY

/* gcc 4.2.1 fares better with NOINLINE */
static NOINLINE void cgi_io_loop_and_exit(int fromCgi_rd, int toCgi_wr, int post_len) NORETURN;
static NOINLINE void cgi_io_loop_and_exit(int fromCgi_rd, int toCgi_wr, int post_len)
{
	enum { FROM_CGI = 1, TO_CGI = 2 }; /* indexes in pfd[] */
	struct pollfd pfd[3];
	int out_cnt; /* we buffer a bit of initial CGI output */
	int count;

	/* iobuf is used for CGI -> network data,
	 * hdr_buf is for network -> CGI data (POSTDATA) */

	/* If CGI dies, we still want to correctly finish reading its output
	 * and send it to the peer. So please no SIGPIPEs! */
	signal(SIGPIPE, SIG_IGN);

	// We inconsistently handle a case when more POSTDATA from network
	// is coming than we expected. We may give *some part* of that
	// extra data to CGI.

	//if (hdr_cnt > post_len) {
	//	/* We got more POSTDATA from network than we expected */
	//	hdr_cnt = post_len;
	//}
	post_len -= hdr_cnt;
	/* post_len - number of POST bytes not yet read from network */

	/* NB: breaking out of this loop jumps to log_and_exit() */
	out_cnt = 0;
	pfd[FROM_CGI].fd = fromCgi_rd;
	pfd[FROM_CGI].events = POLLIN;
	pfd[TO_CGI].fd = toCgi_wr;
	while (1) {
		/* Note: even pfd[0].events == 0 won't prevent
		 * revents == POLLHUP|POLLERR reports from closed stdin.
		 * Setting fd to -1 works: */
		pfd[0].fd = -1;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0; /* probably not needed, paranoia */

		/* We always poll this fd, thus kernel always sets revents: */
		/*pfd[FROM_CGI].events = POLLIN; - moved out of loop */
		/*pfd[FROM_CGI].revents = 0; - not needed */

		/* gcc-4.8.0 still doesnt fill two shorts with one insn :( */
		/* http://gcc.gnu.org/bugzilla/show_bug.cgi?id=47059 */
		/* hopefully one day it will... */
		pfd[TO_CGI].events = POLLOUT;
		pfd[TO_CGI].revents = 0; /* needed! */

		if (toCgi_wr && hdr_cnt <= 0) {
			if (post_len > 0) {
				/* Expect more POST data from network */
				pfd[0].fd = 0;
			} else {
				/* post_len <= 0 && hdr_cnt <= 0:
				 * no more POST data to CGI,
				 * let CGI see EOF on CGI's stdin */
				if (toCgi_wr != fromCgi_rd)
					close(toCgi_wr);
				toCgi_wr = 0;
			}
		}

		/* Now wait on the set of sockets */
		count = safe_poll(pfd, hdr_cnt > 0 ? TO_CGI+1 : FROM_CGI+1, -1);
		if (count <= 0) {
#if 0
			if (safe_waitpid(pid, &status, WNOHANG) <= 0) {
				/* Weird. CGI didn't exit and no fd's
				 * are ready, yet poll returned?! */
				continue;
			}
			if (DEBUG && WIFEXITED(status))
				bb_error_msg("CGI exited, status=%u", WEXITSTATUS(status));
			if (DEBUG && WIFSIGNALED(status))
				bb_error_msg("CGI killed, signal=%u", WTERMSIG(status));
#endif
			break;
		}

		if (pfd[TO_CGI].revents) {
			/* hdr_cnt > 0 here due to the way poll() called */
			/* Have data from peer and can write to CGI */
			count = safe_write(toCgi_wr, hdr_ptr, hdr_cnt);
			/* Doesn't happen, we dont use nonblocking IO here
			 *if (count < 0 && errno == EAGAIN) {
			 *	...
			 *} else */
			if (count > 0) {
				hdr_ptr += count;
				hdr_cnt -= count;
			} else {
				/* EOF/broken pipe to CGI, stop piping POST data */
				hdr_cnt = post_len = 0;
			}
		}

		if (pfd[0].revents) {
			/* post_len > 0 && hdr_cnt == 0 here */
			/* We expect data, prev data portion is eaten by CGI
			 * and there *is* data to read from the peer
			 * (POSTDATA) */
			//count = post_len > (int)sizeof_hdr_buf ? (int)sizeof_hdr_buf : post_len;
			//count = safe_read(STDIN_FILENO, hdr_buf, count);
			count = safe_read(STDIN_FILENO, hdr_buf, sizeof_hdr_buf);
			if (count > 0) {
				hdr_cnt = count;
				hdr_ptr = hdr_buf;
				post_len -= count;
			} else {
				/* no more POST data can be read */
				post_len = 0;
			}
		}

		if (pfd[FROM_CGI].revents) {
			/* There is something to read from CGI */
			char *rbuf = iobuf;

			/* Are we still buffering CGI output? */
			if (out_cnt >= 0) {
				/* HTTP_200[] has single "\r\n" at the end.
				 * According to http://hoohoo.ncsa.uiuc.edu/cgi/out.html,
				 * CGI scripts MUST send their own header terminated by
				 * empty line, then data. That's why we have only one
				 * <cr><lf> pair here. We will output "200 OK" line
				 * if needed, but CGI still has to provide blank line
				 * between header and body */

				/* Must use safe_read, not full_read, because
				 * CGI may output a few first bytes and then wait
				 * for POSTDATA without closing stdout.
				 * With full_read we may wait here forever. */
				count = safe_read(fromCgi_rd, rbuf + out_cnt, PIPE_BUF - 8);
				if (count <= 0) {
					/* eof (or error) and there was no "HTTP",
					 * so write it, then write received data */
					if (out_cnt) {
						full_write(STDOUT_FILENO, HTTP_200, sizeof(HTTP_200)-1);
						full_write(STDOUT_FILENO, rbuf, out_cnt);
					}
					break; /* CGI stdout is closed, exiting */
				}
				out_cnt += count;
				count = 0;
				/* "Status" header format is: "Status: 302 Redirected\r\n" */
				if (out_cnt >= 8 && memcmp(rbuf, "Status: ", 8) == 0) {
					/* send "HTTP/1.0 " */
					if (full_write(STDOUT_FILENO, HTTP_200, 9) != 9)
						break;
					/* skip "Status: " (including space, sending "HTTP/1.0  NNN" is wrong) */
					rbuf += 8;
					count = out_cnt - 8;
					out_cnt = -1; /* buffering off */
				} else if (out_cnt >= 4) {
					/* Did CGI add "HTTP"? */
					if (memcmp(rbuf, HTTP_200, 4) != 0) {
						/* there is no "HTTP", do it ourself */
						if (full_write(STDOUT_FILENO, HTTP_200, sizeof(HTTP_200)-1) != sizeof(HTTP_200)-1)
							break;
					}
					/* Commented out:
					if (!strstr(rbuf, "ontent-")) {
						full_write(s, "Content-type: text/plain\r\n\r\n", 28);
					}
					 * Counter-example of valid CGI without Content-type:
					 * echo -en "HTTP/1.0 302 Found\r\n"
					 * echo -en "Location: http://www.busybox.net\r\n"
					 * echo -en "\r\n"
					 */
					count = out_cnt;
					out_cnt = -1; /* buffering off */
				}
			} else {
				count = safe_read(fromCgi_rd, rbuf, PIPE_BUF);
				if (count <= 0)
					break;  /* eof (or error) */
			}
			if (full_write(STDOUT_FILENO, rbuf, count) != count)
				break;
			if (DEBUG)
				fprintf(stderr, "cgi read %d bytes: '%.*s'\n", count, count, rbuf);
		} /* if (pfd[FROM_CGI].revents) */
	} /* while (1) */
	log_and_exit();
}
#endif

#if ENABLE_FEATURE_HTTPD_CGI

static void setenv1(const char *name, const char *value)
{
	setenv(name, value ? value : "", 1);
}

/*
 * Spawn CGI script, forward CGI's stdin/out <=> network
 *
 * Environment variables are set up and the script is invoked with pipes
 * for stdin/stdout.  If a POST is being done the script is fed the POST
 * data in addition to setting the QUERY_STRING variable (for GETs or POSTs).
 *
 * Parameters:
 * const char *url              The requested URL (with leading /).
 * const char *orig_uri         The original URI before rewriting (if any)
 * int post_len                 Length of the POST body.
 * const char *cookie           For set HTTP_COOKIE.
 * const char *content_type     For set CONTENT_TYPE.
 */
static void send_cgi_and_exit(
		const char *url,
		const char *orig_uri,
		const char *request,
		int post_len,
		const char *cookie,
		const char *content_type) NORETURN;
static void send_cgi_and_exit(
		const char *url,
		const char *orig_uri,
		const char *request,
		int post_len,
		const char *cookie,
		const char *content_type)
{
	struct fd_pair fromCgi;  /* CGI -> httpd pipe */
	struct fd_pair toCgi;    /* httpd -> CGI pipe */
	char *script, *last_slash;
	int pid;

	/* Make a copy. NB: caller guarantees:
	 * url[0] == '/', url[1] != '/' */
	url = xstrdup(url);

	/*
	 * We are mucking with environment _first_ and then vfork/exec,
	 * this allows us to use vfork safely. Parent doesn't care about
	 * these environment changes anyway.
	 */

	/* Check for [dirs/]script.cgi/PATH_INFO */
	last_slash = script = (char*)url;
	while ((script = strchr(script + 1, '/')) != NULL) {
		int dir;
		*script = '\0';
		dir = is_directory(url + 1, /*followlinks:*/ 1);
		*script = '/';
		if (!dir) {
			/* not directory, found script.cgi/PATH_INFO */
			break;
		}
		/* is directory, find next '/' */
		last_slash = script;
	}
	setenv1("PATH_INFO", script);   /* set to /PATH_INFO or "" */
	setenv1("REQUEST_METHOD", request);
	if (g_query) {
		putenv(xasprintf("%s=%s?%s", "REQUEST_URI", orig_uri, g_query));
	} else {
		setenv1("REQUEST_URI", orig_uri);
	}
	if (script != NULL)
		*script = '\0';         /* cut off /PATH_INFO */

	/* SCRIPT_FILENAME is required by PHP in CGI mode */
	if (home_httpd[0] == '/') {
		char *fullpath = concat_path_file(home_httpd, url);
		setenv1("SCRIPT_FILENAME", fullpath);
	}
	/* set SCRIPT_NAME as full path: /cgi-bin/dirs/script.cgi */
	setenv1("SCRIPT_NAME", url);
	/* http://hoohoo.ncsa.uiuc.edu/cgi/env.html:
	 * QUERY_STRING: The information which follows the ? in the URL
	 * which referenced this script. This is the query information.
	 * It should not be decoded in any fashion. This variable
	 * should always be set when there is query information,
	 * regardless of command line decoding. */
	/* (Older versions of bbox seem to do some decoding) */
	setenv1("QUERY_STRING", g_query);
	putenv((char*)"SERVER_SOFTWARE=busybox httpd/"BB_VER);
	putenv((char*)"SERVER_PROTOCOL=HTTP/1.0");
	putenv((char*)"GATEWAY_INTERFACE=CGI/1.1");
	/* Having _separate_ variables for IP and port defeats
	 * the purpose of having socket abstraction. Which "port"
	 * are you using on Unix domain socket?
	 * IOW - REMOTE_PEER="1.2.3.4:56" makes much more sense.
	 * Oh well... */
	{
		char *p = rmt_ip_str ? rmt_ip_str : (char*)"";
		char *cp = strrchr(p, ':');
		if (ENABLE_FEATURE_IPV6 && cp && strchr(cp, ']'))
			cp = NULL;
		if (cp) *cp = '\0'; /* delete :PORT */
		setenv1("REMOTE_ADDR", p);
		if (cp) {
			*cp = ':';
#if ENABLE_FEATURE_HTTPD_SET_REMOTE_PORT_TO_ENV
			setenv1("REMOTE_PORT", cp + 1);
#endif
		}
	}
	setenv1("HTTP_USER_AGENT", G.user_agent);
	if (G.http_accept)
		setenv1("HTTP_ACCEPT", G.http_accept);
	if (G.http_accept_language)
		setenv1("HTTP_ACCEPT_LANGUAGE", G.http_accept_language);
	if (post_len)
		putenv(xasprintf("CONTENT_LENGTH=%u", post_len));
	if (cookie)
		setenv1("HTTP_COOKIE", cookie);
	if (content_type)
		setenv1("CONTENT_TYPE", content_type);
#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	if (remoteuser) {
		setenv1("REMOTE_USER", remoteuser);
		putenv((char*)"AUTH_TYPE=Basic");
	}
#endif
	if (G.referer)
		setenv1("HTTP_REFERER", G.referer);
	setenv1("HTTP_HOST", G.host); /* set to "" if NULL */
	/* setenv1("SERVER_NAME", safe_gethostname()); - don't do this,
	 * just run "env SERVER_NAME=xyz httpd ..." instead */

	xpiped_pair(fromCgi);
	xpiped_pair(toCgi);

	pid = vfork();
	if (pid < 0) {
		/* TODO: log perror? */
		log_and_exit();
	}

	if (pid == 0) {
		/* Child process */
		char *argv[3];

		xfunc_error_retval = 242;

		/* NB: close _first_, then move fds! */
		close(toCgi.wr);
		close(fromCgi.rd);
		xmove_fd(toCgi.rd, 0);  /* replace stdin with the pipe */
		xmove_fd(fromCgi.wr, 1);  /* replace stdout with the pipe */
		/* User seeing stderr output can be a security problem.
		 * If CGI really wants that, it can always do dup itself. */
		/* dup2(1, 2); */

		/* Chdiring to script's dir */
		script = last_slash;
		if (script != url) { /* paranoia */
			*script = '\0';
			if (chdir(url + 1) != 0) {
				bb_perror_msg("can't change directory to '%s'", url + 1);
				goto error_execing_cgi;
			}
			// not needed: *script = '/';
		}
		script++;

		/* set argv[0] to name without path */
		argv[0] = script;
		argv[1] = NULL;

#if ENABLE_FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
		{
			char *suffix = strrchr(script, '.');

			if (suffix) {
				Htaccess *cur;
				for (cur = script_i; cur; cur = cur->next) {
					if (strcmp(cur->before_colon + 1, suffix) == 0) {
						/* found interpreter name */
						argv[0] = cur->after_colon;
						argv[1] = script;
						argv[2] = NULL;
						break;
					}
				}
			}
		}
#endif
		/* restore default signal dispositions for CGI process */
		bb_signals(0
			| (1 << SIGCHLD)
			| (1 << SIGPIPE)
			| (1 << SIGHUP)
			, SIG_DFL);

		/* _NOT_ execvp. We do not search PATH. argv[0] is a filename
		 * without any dir components and will only match a file
		 * in the current directory */
		execv(argv[0], argv);
		if (verbose)
			bb_perror_msg("can't execute '%s'", argv[0]);
 error_execing_cgi:
		/* send to stdout
		 * (we are CGI here, our stdout is pumped to the net) */
		send_headers_and_exit(HTTP_NOT_FOUND);
	} /* end child */

	/* Parent process */

	/* Restore variables possibly changed by child */
	xfunc_error_retval = 0;

	/* Pump data */
	close(fromCgi.wr);
	close(toCgi.rd);
	cgi_io_loop_and_exit(fromCgi.rd, toCgi.wr, post_len);
}

#endif          /* FEATURE_HTTPD_CGI */

/*
 * Send a file response to a HTTP request, and exit
 *
 * Parameters:
 * const char *url  The requested URL (with leading /).
 * what             What to send (headers/body/both).
 */
static NOINLINE void send_file_and_exit(const char *url, int what)
{
	char *suffix;
	int fd;
	ssize_t count;

	if (content_gzip) {
		/* does <url>.gz exist? Then use it instead */
		char *gzurl = xasprintf("%s.gz", url);
		fd = open(gzurl, O_RDONLY);
		free(gzurl);
		if (fd != -1) {
			struct stat sb;
			fstat(fd, &sb);
			file_size = sb.st_size;
			last_mod = sb.st_mtime;
		} else {
			IF_FEATURE_HTTPD_GZIP(content_gzip = 0;)
			fd = open(url, O_RDONLY);
		}
	} else {
		fd = open(url, O_RDONLY);
	}
	if (fd < 0) {
		if (DEBUG)
			bb_perror_msg("can't open '%s'", url);
		/* Error pages are sent by using send_file_and_exit(SEND_BODY).
		 * IOW: it is unsafe to call send_headers_and_exit
		 * if what is SEND_BODY! Can recurse! */
		if (what != SEND_BODY)
			send_headers_and_exit(HTTP_NOT_FOUND);
		log_and_exit();
	}
	/* If you want to know about EPIPE below
	 * (happens if you abort downloads from local httpd): */
	signal(SIGPIPE, SIG_IGN);

	/* If not found, default is to not send "Content-type:" */
	/*found_mime_type = NULL; - already is */
	suffix = strrchr(url, '.');
	if (suffix) {
		static const char suffixTable[] ALIGN1 =
			/* Shorter suffix must be first:
			 * ".html.htm" will fail for ".htm"
			 */
			".txt.h.c.cc.cpp\0" "text/plain\0"
			/* .htm line must be after .h line */
			".htm.html\0" "text/html\0"
			".jpg.jpeg\0" "image/jpeg\0"
			".gif\0"      "image/gif\0"
			".png\0"      "image/png\0"
			/* .css line must be after .c line */
			".css\0"      "text/css\0"
			".wav\0"      "audio/wav\0"
			".avi\0"      "video/x-msvideo\0"
			".qt.mov\0"   "video/quicktime\0"
			".mpe.mpeg\0" "video/mpeg\0"
			".mid.midi\0" "audio/midi\0"
			".mp3\0"      "audio/mpeg\0"
#if 0  /* unpopular */
			".au\0"       "audio/basic\0"
			".pac\0"      "application/x-ns-proxy-autoconfig\0"
			".vrml.wrl\0" "model/vrml\0"
#endif
			/* compiler adds another "\0" here */
		;
		Htaccess *cur;

		/* Examine built-in table */
		const char *table = suffixTable;
		const char *table_next;
		for (; *table; table = table_next) {
			const char *try_suffix;
			const char *mime_type;
			mime_type  = table + strlen(table) + 1;
			table_next = mime_type + strlen(mime_type) + 1;
			try_suffix = strstr(table, suffix);
			if (!try_suffix)
				continue;
			try_suffix += strlen(suffix);
			if (*try_suffix == '\0' || *try_suffix == '.') {
				found_mime_type = mime_type;
				break;
			}
			/* Example: strstr(table, ".av") != NULL, but it
			 * does not match ".avi" after all and we end up here.
			 * The table is arranged so that in this case we know
			 * that it can't match anything in the following lines,
			 * and we stop the search: */
			break;
		}
		/* ...then user's table */
		for (cur = mime_a; cur; cur = cur->next) {
			if (strcmp(cur->before_colon, suffix) == 0) {
				found_mime_type = cur->after_colon;
				break;
			}
		}
	}

	if (DEBUG)
		bb_error_msg("sending file '%s' content-type: %s",
			url, found_mime_type);

#if ENABLE_FEATURE_HTTPD_RANGES
	if (what == SEND_BODY /* err pages and ranges don't mix */
	 || content_gzip /* we are sending compressed page: can't do ranges */  ///why?
	) {
		range_start = -1;
	}
	range_len = MAXINT(off_t);
	if (range_start >= 0) {
		if (!range_end || range_end > file_size - 1) {
			range_end = file_size - 1;
		}
		if (range_end < range_start
		 || lseek(fd, range_start, SEEK_SET) != range_start
		) {
			lseek(fd, 0, SEEK_SET);
			range_start = -1;
		} else {
			range_len = range_end - range_start + 1;
			send_headers(HTTP_PARTIAL_CONTENT);
			what = SEND_BODY;
		}
	}
#endif
	if (what & SEND_HEADERS)
		send_headers(HTTP_OK);
#if ENABLE_FEATURE_USE_SENDFILE
	{
		off_t offset = range_start;
		while (1) {
			/* sz is rounded down to 64k */
			ssize_t sz = MAXINT(ssize_t) - 0xffff;
			IF_FEATURE_HTTPD_RANGES(if (sz > range_len) sz = range_len;)
			count = sendfile(STDOUT_FILENO, fd, &offset, sz);
			if (count < 0) {
				if (offset == range_start)
					break; /* fall back to read/write loop */
				goto fin;
			}
			IF_FEATURE_HTTPD_RANGES(range_len -= count;)
			if (count == 0 || range_len == 0)
				log_and_exit();
		}
	}
#endif
	while ((count = safe_read(fd, iobuf, IOBUF_SIZE)) > 0) {
		ssize_t n;
		IF_FEATURE_HTTPD_RANGES(if (count > range_len) count = range_len;)
		n = full_write(STDOUT_FILENO, iobuf, count);
		if (count != n)
			break;
		IF_FEATURE_HTTPD_RANGES(range_len -= count;)
		if (range_len == 0)
			break;
	}
	if (count < 0) {
 IF_FEATURE_USE_SENDFILE(fin:)
		if (verbose > 1)
			bb_perror_msg("error");
	}
	log_and_exit();
}

static int checkPermIP(void)
{
	Htaccess_IP *cur;

	for (cur = ip_a_d; cur; cur = cur->next) {
#if DEBUG
		fprintf(stderr,
			"checkPermIP: '%s' ? '%u.%u.%u.%u/%u.%u.%u.%u'\n",
			rmt_ip_str,
			(unsigned char)(cur->ip >> 24),
			(unsigned char)(cur->ip >> 16),
			(unsigned char)(cur->ip >> 8),
			(unsigned char)(cur->ip),
			(unsigned char)(cur->mask >> 24),
			(unsigned char)(cur->mask >> 16),
			(unsigned char)(cur->mask >> 8),
			(unsigned char)(cur->mask)
		);
#endif
		if ((rmt_ip & cur->mask) == cur->ip)
			return (cur->allow_deny == 'A'); /* A -> 1 */
	}

	return !flg_deny_all; /* depends on whether we saw "D:*" */
}

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH

# if ENABLE_PAM
struct pam_userinfo {
	const char *name;
	const char *pw;
};

static int pam_talker(int num_msg,
		const struct pam_message **msg,
		struct pam_response **resp,
		void *appdata_ptr)
{
	int i;
	struct pam_userinfo *userinfo = (struct pam_userinfo *) appdata_ptr;
	struct pam_response *response;

	if (!resp || !msg || !userinfo)
		return PAM_CONV_ERR;

	/* allocate memory to store response */
	response = xzalloc(num_msg * sizeof(*response));

	/* copy values */
	for (i = 0; i < num_msg; i++) {
		const char *s;

		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_ON:
			s = userinfo->name;
			break;
		case PAM_PROMPT_ECHO_OFF:
			s = userinfo->pw;
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			s = "";
			break;
		default:
			free(response);
			return PAM_CONV_ERR;
		}
		response[i].resp = xstrdup(s);
		if (PAM_SUCCESS != 0)
			response[i].resp_retcode = PAM_SUCCESS;
	}
	*resp = response;
	return PAM_SUCCESS;
}
# endif

/*
 * Config file entries are of the form "/<path>:<user>:<passwd>".
 * If config file has no prefix match for path, access is allowed.
 *
 * path                 The file path
 * user_and_passwd      "user:passwd" to validate
 *
 * Returns 1 if user_and_passwd is OK.
 */
static int check_user_passwd(const char *path, char *user_and_passwd)
{
	Htaccess *cur;
	const char *prev = NULL;

	for (cur = g_auth; cur; cur = cur->next) {
		const char *dir_prefix;
		size_t len;
		int r;

		dir_prefix = cur->before_colon;

		/* WHY? */
		/* If already saw a match, don't accept other different matches */
		if (prev && strcmp(prev, dir_prefix) != 0)
			continue;

		if (DEBUG)
			fprintf(stderr, "checkPerm: '%s' ? '%s'\n", dir_prefix, user_and_passwd);

		/* If it's not a prefix match, continue searching */
		len = strlen(dir_prefix);
		if (len != 1 /* dir_prefix "/" matches all, don't need to check */
		 && (strncmp(dir_prefix, path, len) != 0
		    || (path[len] != '/' && path[len] != '\0')
		    )
		) {
			continue;
		}

		/* Path match found */
		prev = dir_prefix;

		if (ENABLE_FEATURE_HTTPD_AUTH_MD5) {
			char *colon_after_user;
			const char *passwd;
# if ENABLE_FEATURE_SHADOWPASSWDS && !ENABLE_PAM
			char sp_buf[256];
# endif

			colon_after_user = strchr(user_and_passwd, ':');
			if (!colon_after_user)
				goto bad_input;

			/* compare "user:" */
			if (cur->after_colon[0] != '*'
			 && strncmp(cur->after_colon, user_and_passwd,
					colon_after_user - user_and_passwd + 1) != 0
			) {
				continue;
			}
			/* this cfg entry is '*' or matches username from peer */

			passwd = strchr(cur->after_colon, ':');
			if (!passwd)
				goto bad_input;
			passwd++;
			if (passwd[0] == '*') {
# if ENABLE_PAM
				struct pam_userinfo userinfo;
				struct pam_conv conv_info = { &pam_talker, (void *) &userinfo };
				pam_handle_t *pamh;

				*colon_after_user = '\0';
				userinfo.name = user_and_passwd;
				userinfo.pw = colon_after_user + 1;
				r = pam_start("httpd", user_and_passwd, &conv_info, &pamh) != PAM_SUCCESS;
				if (r == 0) {
					r = pam_authenticate(pamh, PAM_DISALLOW_NULL_AUTHTOK) != PAM_SUCCESS
					 || pam_acct_mgmt(pamh, PAM_DISALLOW_NULL_AUTHTOK)    != PAM_SUCCESS
					;
					pam_end(pamh, PAM_SUCCESS);
				}
				*colon_after_user = ':';
				goto end_check_passwd;
# else
#  if ENABLE_FEATURE_SHADOWPASSWDS
				/* Using _r function to avoid pulling in static buffers */
				struct spwd spw;
#  endif
				struct passwd *pw;

				*colon_after_user = '\0';
				pw = getpwnam(user_and_passwd);
				*colon_after_user = ':';
				if (!pw || !pw->pw_passwd)
					continue;
				passwd = pw->pw_passwd;
#  if ENABLE_FEATURE_SHADOWPASSWDS
				if ((passwd[0] == 'x' || passwd[0] == '*') && !passwd[1]) {
					/* getspnam_r may return 0 yet set result to NULL.
					 * At least glibc 2.4 does this. Be extra paranoid here. */
					struct spwd *result = NULL;
					r = getspnam_r(pw->pw_name, &spw, sp_buf, sizeof(sp_buf), &result);
					if (r == 0 && result)
						passwd = result->sp_pwdp;
				}
#  endif
				/* In this case, passwd is ALWAYS encrypted:
				 * it came from /etc/passwd or /etc/shadow!
				 */
				goto check_encrypted;
# endif /* ENABLE_PAM */
			}
			/* Else: passwd is from httpd.conf, it is either plaintext or encrypted */

			if (passwd[0] == '$' && isdigit(passwd[1])) {
				char *encrypted;
# if !ENABLE_PAM
 check_encrypted:
# endif
				/* encrypt pwd from peer and check match with local one */
				encrypted = pw_encrypt(
					/* pwd (from peer): */  colon_after_user + 1,
					/* salt: */ passwd,
					/* cleanup: */ 0
				);
				r = strcmp(encrypted, passwd);
				free(encrypted);
			} else {
				/* local passwd is from httpd.conf and it's plaintext */
				r = strcmp(colon_after_user + 1, passwd);
			}
			goto end_check_passwd;
		}
 bad_input:
		/* Comparing plaintext "user:pass" in one go */
		r = strcmp(cur->after_colon, user_and_passwd);
 end_check_passwd:
		if (r == 0) {
			remoteuser = xstrndup(user_and_passwd,
				strchrnul(user_and_passwd, ':') - user_and_passwd
			);
			return 1; /* Ok */
		}
	} /* for */

	/* 0(bad) if prev is set: matches were found but passwd was wrong */
	return (prev == NULL);
}
#endif  /* FEATURE_HTTPD_BASIC_AUTH */

#if ENABLE_FEATURE_HTTPD_PROXY
static Htaccess_Proxy *find_proxy_entry(const char *url)
{
	Htaccess_Proxy *p;
	for (p = proxy; p; p = p->next) {
		if (is_prefixed_with(url, p->url_from))
			return p;
	}
	return NULL;
}
#endif

/*
 * Handle timeouts
 */
static void send_REQUEST_TIMEOUT_and_exit(int sig) NORETURN;
static void send_REQUEST_TIMEOUT_and_exit(int sig UNUSED_PARAM)
{
	send_headers_and_exit(HTTP_REQUEST_TIMEOUT);
}

/*
 * Handle an incoming http request and exit.
 */
static void handle_incoming_and_exit(const len_and_sockaddr *fromAddr) NORETURN;
static void handle_incoming_and_exit(const len_and_sockaddr *fromAddr)
{
	static const char request_GET[] ALIGN1 = "GET";
	struct stat sb;
	char *urlcopy;
	char *urlp;
	char *tptr;
#if ENABLE_FEATURE_HTTPD_CGI
	static const char request_HEAD[] ALIGN1 = "HEAD";
	const char *prequest;
	char *cookie = NULL;
	char *content_type = NULL;
	unsigned long length = 0;
#elif ENABLE_FEATURE_HTTPD_PROXY
#define prequest request_GET
	unsigned long length = 0;
#endif
#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	smallint authorized = -1;
#endif
	smallint ip_allowed;
	char http_major_version;
#if ENABLE_FEATURE_HTTPD_PROXY
	char http_minor_version;
	char *header_buf = header_buf; /* for gcc */
	char *header_ptr = header_ptr;
	Htaccess_Proxy *proxy_entry;
#endif

	/* Allocation of iobuf is postponed until now
	 * (IOW, server process doesn't need to waste 8k) */
	iobuf = xmalloc(IOBUF_SIZE);

	rmt_ip = 0;
	if (fromAddr->u.sa.sa_family == AF_INET) {
		rmt_ip = ntohl(fromAddr->u.sin.sin_addr.s_addr);
	}
#if ENABLE_FEATURE_IPV6
	if (fromAddr->u.sa.sa_family == AF_INET6
	 && fromAddr->u.sin6.sin6_addr.s6_addr32[0] == 0
	 && fromAddr->u.sin6.sin6_addr.s6_addr32[1] == 0
	 && ntohl(fromAddr->u.sin6.sin6_addr.s6_addr32[2]) == 0xffff)
		rmt_ip = ntohl(fromAddr->u.sin6.sin6_addr.s6_addr32[3]);
#endif
	if (ENABLE_FEATURE_HTTPD_CGI || DEBUG || verbose) {
		/* NB: can be NULL (user runs httpd -i by hand?) */
		rmt_ip_str = xmalloc_sockaddr2dotted(&fromAddr->u.sa);
	}
	if (verbose) {
		/* this trick makes -v logging much simpler */
		if (rmt_ip_str)
			applet_name = rmt_ip_str;
		if (verbose > 2)
			bb_error_msg("connected");
	}

	/* Install timeout handler. get_line() needs it. */
	signal(SIGALRM, send_REQUEST_TIMEOUT_and_exit);

	if (!get_line()) /* EOF or error or empty line */
		send_headers_and_exit(HTTP_BAD_REQUEST);

	/* Determine type of request (GET/POST) */
	// rfc2616: method and URI is separated by exactly one space
	//urlp = strpbrk(iobuf, " \t"); - no, tab isn't allowed
	urlp = strchr(iobuf, ' ');
	if (urlp == NULL)
		send_headers_and_exit(HTTP_BAD_REQUEST);
	*urlp++ = '\0';
#if ENABLE_FEATURE_HTTPD_CGI
	prequest = request_GET;
	if (strcasecmp(iobuf, prequest) != 0) {
		prequest = request_HEAD;
		if (strcasecmp(iobuf, prequest) != 0) {
			prequest = "POST";
			if (strcasecmp(iobuf, prequest) != 0)
				send_headers_and_exit(HTTP_NOT_IMPLEMENTED);
		}
	}
#else
	if (strcasecmp(iobuf, request_GET) != 0)
		send_headers_and_exit(HTTP_NOT_IMPLEMENTED);
#endif
	// rfc2616: method and URI is separated by exactly one space
	//urlp = skip_whitespace(urlp); - should not be necessary
	if (urlp[0] != '/')
		send_headers_and_exit(HTTP_BAD_REQUEST);

	/* Find end of URL and parse HTTP version, if any */
	http_major_version = '0';
	IF_FEATURE_HTTPD_PROXY(http_minor_version = '0';)
	tptr = strchrnul(urlp, ' ');
	/* Is it " HTTP/"? */
	if (tptr[0] && strncmp(tptr + 1, HTTP_200, 5) == 0) {
		http_major_version = tptr[6];
		IF_FEATURE_HTTPD_PROXY(http_minor_version = tptr[8];)
	}
	*tptr = '\0';

	/* Copy URL from after "GET "/"POST " to stack-allocated char[] */
	urlcopy = alloca((tptr - urlp) + 2 + strlen(index_page));
	/*if (urlcopy == NULL)
	 *	send_headers_and_exit(HTTP_INTERNAL_SERVER_ERROR);*/
	strcpy(urlcopy, urlp);
	/* NB: urlcopy ptr is never changed after this */

	/* Extract url args if present */
	/* g_query = NULL; - already is */
	tptr = strchr(urlcopy, '?');
	if (tptr) {
		*tptr++ = '\0';
		g_query = tptr;
	}

	/* Decode URL escape sequences */
	tptr = percent_decode_in_place(urlcopy, /*strict:*/ 1);
	if (tptr == NULL)
		send_headers_and_exit(HTTP_BAD_REQUEST);
	if (tptr == urlcopy + 1) {
		/* '/' or NUL is encoded */
		send_headers_and_exit(HTTP_NOT_FOUND);
	}

	/* Canonicalize path */
	/* Algorithm stolen from libbb bb_simplify_path(),
	 * but don't strdup, retain trailing slash, protect root */
	urlp = tptr = urlcopy;
	for (;;) {
		if (*urlp == '/') {
			/* skip duplicate (or initial) slash */
			if (*tptr == '/') {
				goto next_char;
			}
			if (*tptr == '.') {
				if (tptr[1] == '.' && (tptr[2] == '/' || tptr[2] == '\0')) {
					/* "..": be careful */
					/* protect root */
					if (urlp == urlcopy)
						send_headers_and_exit(HTTP_BAD_REQUEST);
					/* omit previous dir */
					while (*--urlp != '/')
						continue;
					/* skip to "./" or ".<NUL>" */
					tptr++;
				}
				if (tptr[1] == '/' || tptr[1] == '\0') {
					/* skip extra "/./" */
					goto next_char;
				}
			}
		}
		*++urlp = *tptr;
		if (*urlp == '\0')
			break;
 next_char:
		tptr++;
	}

	/* If URL is a directory, add '/' */
	if (urlp[-1] != '/') {
		if (is_directory(urlcopy + 1, /*followlinks:*/ 1)) {
			found_moved_temporarily = urlcopy;
		}
	}

	/* Log it */
	if (verbose > 1)
		bb_error_msg("url:%s", urlcopy);

	tptr = urlcopy;
	ip_allowed = checkPermIP();
	while (ip_allowed && (tptr = strchr(tptr + 1, '/')) != NULL) {
		/* have path1/path2 */
		*tptr = '\0';
		if (is_directory(urlcopy + 1, /*followlinks:*/ 1)) {
			/* may have subdir config */
			parse_conf(urlcopy + 1, SUBDIR_PARSE);
			ip_allowed = checkPermIP();
		}
		*tptr = '/';
	}

#if ENABLE_FEATURE_HTTPD_PROXY
	proxy_entry = find_proxy_entry(urlcopy);
	if (proxy_entry)
		header_buf = header_ptr = xmalloc(IOBUF_SIZE);
#endif

	if (http_major_version >= '0') {
		/* Request was with "... HTTP/nXXX", and n >= 0 */

		/* Read until blank line */
		while (1) {
			if (!get_line())
				break; /* EOF or error or empty line */
			if (DEBUG)
				bb_error_msg("header: '%s'", iobuf);

#if ENABLE_FEATURE_HTTPD_PROXY
			/* We need 2 more bytes for yet another "\r\n" -
			 * see near fdprintf(proxy_fd...) further below */
			if (proxy_entry && (header_ptr - header_buf) < IOBUF_SIZE - 4) {
				int len = strnlen(iobuf, IOBUF_SIZE - (header_ptr - header_buf) - 4);
				memcpy(header_ptr, iobuf, len);
				header_ptr += len;
				header_ptr[0] = '\r';
				header_ptr[1] = '\n';
				header_ptr += 2;
			}
#endif

#if ENABLE_FEATURE_HTTPD_CGI || ENABLE_FEATURE_HTTPD_PROXY
			/* Try and do our best to parse more lines */
			if ((STRNCASECMP(iobuf, "Content-Length:") == 0)) {
				/* extra read only for POST */
				if (prequest != request_GET
# if ENABLE_FEATURE_HTTPD_CGI
				 && prequest != request_HEAD
# endif
				) {
					tptr = skip_whitespace(iobuf + sizeof("Content-Length:") - 1);
					if (!tptr[0])
						send_headers_and_exit(HTTP_BAD_REQUEST);
					/* not using strtoul: it ignores leading minus! */
					length = bb_strtou(tptr, NULL, 10);
					/* length is "ulong", but we need to pass it to int later */
					if (errno || length > INT_MAX)
						send_headers_and_exit(HTTP_BAD_REQUEST);
				}
			}
#endif
#if ENABLE_FEATURE_HTTPD_CGI
			else if (STRNCASECMP(iobuf, "Cookie:") == 0) {
				if (!cookie) /* in case they send millions of these, do not OOM */
					cookie = xstrdup(skip_whitespace(iobuf + sizeof("Cookie:")-1));
			} else if (STRNCASECMP(iobuf, "Content-Type:") == 0) {
				if (!content_type)
					content_type = xstrdup(skip_whitespace(iobuf + sizeof("Content-Type:")-1));
			} else if (STRNCASECMP(iobuf, "Referer:") == 0) {
				if (!G.referer)
					G.referer = xstrdup(skip_whitespace(iobuf + sizeof("Referer:")-1));
			} else if (STRNCASECMP(iobuf, "User-Agent:") == 0) {
				if (!G.user_agent)
					G.user_agent = xstrdup(skip_whitespace(iobuf + sizeof("User-Agent:")-1));
			} else if (STRNCASECMP(iobuf, "Host:") == 0) {
				if (!G.host)
					G.host = xstrdup(skip_whitespace(iobuf + sizeof("Host:")-1));
			} else if (STRNCASECMP(iobuf, "Accept:") == 0) {
				if (!G.http_accept)
					G.http_accept = xstrdup(skip_whitespace(iobuf + sizeof("Accept:")-1));
			} else if (STRNCASECMP(iobuf, "Accept-Language:") == 0) {
				if (!G.http_accept_language)
					G.http_accept_language = xstrdup(skip_whitespace(iobuf + sizeof("Accept-Language:")-1));
			}
#endif
#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
			if (STRNCASECMP(iobuf, "Authorization:") == 0) {
				/* We only allow Basic credentials.
				 * It shows up as "Authorization: Basic <user>:<passwd>" where
				 * "<user>:<passwd>" is base64 encoded.
				 */
				tptr = skip_whitespace(iobuf + sizeof("Authorization:")-1);
				if (STRNCASECMP(tptr, "Basic") != 0)
					continue;
				tptr += sizeof("Basic")-1;
				/* decodeBase64() skips whitespace itself */
				decodeBase64(tptr);
				authorized = check_user_passwd(urlcopy, tptr);
			}
#endif
#if ENABLE_FEATURE_HTTPD_RANGES
			if (STRNCASECMP(iobuf, "Range:") == 0) {
				/* We know only bytes=NNN-[MMM] */
				char *s = skip_whitespace(iobuf + sizeof("Range:")-1);
				if (is_prefixed_with(s, "bytes=")) {
					s += sizeof("bytes=")-1;
					range_start = BB_STRTOOFF(s, &s, 10);
					if (s[0] != '-' || range_start < 0) {
						range_start = -1;
					} else if (s[1]) {
						range_end = BB_STRTOOFF(s+1, NULL, 10);
						if (errno || range_end < range_start)
							range_start = -1;
					}
				}
			}
#endif
#if ENABLE_FEATURE_HTTPD_GZIP
			if (STRNCASECMP(iobuf, "Accept-Encoding:") == 0) {
				/* Note: we do not support "gzip;q=0"
				 * method of _disabling_ gzip
				 * delivery. No one uses that, though */
				const char *s = strstr(iobuf, "gzip");
				if (s) {
					// want more thorough checks?
					//if (s[-1] == ' '
					// || s[-1] == ','
					// || s[-1] == ':'
					//) {
						content_gzip = 1;
					//}
				}
			}
#endif
		} /* while extra header reading */
	}

	/* We are done reading headers, disable peer timeout */
	alarm(0);

	if (strcmp(bb_basename(urlcopy), HTTPD_CONF) == 0 || !ip_allowed) {
		/* protect listing [/path]/httpd.conf or IP deny */
		send_headers_and_exit(HTTP_FORBIDDEN);
	}

#if ENABLE_FEATURE_HTTPD_BASIC_AUTH
	/* Case: no "Authorization:" was seen, but page might require passwd.
	 * Check that with dummy user:pass */
	if (authorized < 0)
		authorized = check_user_passwd(urlcopy, (char *) "");
	if (!authorized)
		send_headers_and_exit(HTTP_UNAUTHORIZED);
#endif

	if (found_moved_temporarily) {
		send_headers_and_exit(HTTP_MOVED_TEMPORARILY);
	}

#if ENABLE_FEATURE_HTTPD_PROXY
	if (proxy_entry != NULL) {
		int proxy_fd;
		len_and_sockaddr *lsa;

		lsa = host2sockaddr(proxy_entry->host_port, 80);
		if (lsa == NULL)
			send_headers_and_exit(HTTP_INTERNAL_SERVER_ERROR);
		proxy_fd = socket(lsa->u.sa.sa_family, SOCK_STREAM, 0);
		if (proxy_fd < 0)
			send_headers_and_exit(HTTP_INTERNAL_SERVER_ERROR);
		if (connect(proxy_fd, &lsa->u.sa, lsa->len) < 0)
			send_headers_and_exit(HTTP_INTERNAL_SERVER_ERROR);
		fdprintf(proxy_fd, "%s %s%s%s%s HTTP/%c.%c\r\n",
				prequest, /* GET or POST */
				proxy_entry->url_to, /* url part 1 */
				urlcopy + strlen(proxy_entry->url_from), /* url part 2 */
				(g_query ? "?" : ""), /* "?" (maybe) */
				(g_query ? g_query : ""), /* query string (maybe) */
				http_major_version, http_minor_version);
		header_ptr[0] = '\r';
		header_ptr[1] = '\n';
		header_ptr += 2;
		write(proxy_fd, header_buf, header_ptr - header_buf);
		free(header_buf); /* on the order of 8k, free it */
		cgi_io_loop_and_exit(proxy_fd, proxy_fd, length);
	}
#endif

	tptr = urlcopy + 1;      /* skip first '/' */

#if ENABLE_FEATURE_HTTPD_CGI
	if (is_prefixed_with(tptr, "cgi-bin/")) {
		if (tptr[8] == '\0') {
			/* protect listing "cgi-bin/" */
			send_headers_and_exit(HTTP_FORBIDDEN);
		}
		send_cgi_and_exit(urlcopy, urlcopy, prequest, length, cookie, content_type);
	}
#endif

	if (urlp[-1] == '/') {
		/* When index_page string is appended to <dir>/ URL, it overwrites
		 * the query string. If we fall back to call /cgi-bin/index.cgi,
		 * query string would be lost and not available to the CGI.
		 * Work around it by making a deep copy.
		 */
		if (ENABLE_FEATURE_HTTPD_CGI)
			g_query = xstrdup(g_query); /* ok for NULL too */
		strcpy(urlp, index_page);
	}
	if (stat(tptr, &sb) == 0) {
#if ENABLE_FEATURE_HTTPD_CONFIG_WITH_SCRIPT_INTERPR
		char *suffix = strrchr(tptr, '.');
		if (suffix) {
			Htaccess *cur;
			for (cur = script_i; cur; cur = cur->next) {
				if (strcmp(cur->before_colon + 1, suffix) == 0) {
					send_cgi_and_exit(urlcopy, urlcopy, prequest, length, cookie, content_type);
				}
			}
		}
#endif
		file_size = sb.st_size;
		last_mod = sb.st_mtime;
	}
#if ENABLE_FEATURE_HTTPD_CGI
	else if (urlp[-1] == '/') {
		/* It's a dir URL and there is no index.html
		 * Try cgi-bin/index.cgi */
		if (access("/cgi-bin/index.cgi"+1, X_OK) == 0) {
			urlp[0] = '\0'; /* remove index_page */
			send_cgi_and_exit("/cgi-bin/index.cgi", urlcopy, prequest, length, cookie, content_type);
		}
	}
	/* else fall through to send_file, it errors out if open fails: */

	if (prequest != request_GET && prequest != request_HEAD) {
		/* POST for files does not make sense */
		send_headers_and_exit(HTTP_NOT_IMPLEMENTED);
	}
	send_file_and_exit(tptr,
		(prequest != request_HEAD ? SEND_HEADERS_AND_BODY : SEND_HEADERS)
	);
#else
	send_file_and_exit(tptr, SEND_HEADERS_AND_BODY);
#endif
}

/*
 * The main http server function.
 * Given a socket, listen for new connections and farm out
 * the processing as a [v]forked process.
 * Never returns.
 */
#if BB_MMU
static void mini_httpd(int server_socket) NORETURN;
static void mini_httpd(int server_socket)
{
	/* NB: it's best to not use xfuncs in this loop before fork().
	 * Otherwise server may die on transient errors (temporary
	 * out-of-memory condition, etc), which is Bad(tm).
	 * Try to do any dangerous calls after fork.
	 */
	while (1) {
		int n;
		len_and_sockaddr fromAddr;

		/* Wait for connections... */
		fromAddr.len = LSA_SIZEOF_SA;
		n = accept(server_socket, &fromAddr.u.sa, &fromAddr.len);
		if (n < 0)
			continue;

		/* set the KEEPALIVE option to cull dead connections */
		setsockopt_keepalive(n);

		if (fork() == 0) {
			/* child */
			/* Do not reload config on HUP */
			signal(SIGHUP, SIG_IGN);
			close(server_socket);
			xmove_fd(n, 0);
			xdup2(0, 1);

			handle_incoming_and_exit(&fromAddr);
		}
		/* parent, or fork failed */
		close(n);
	} /* while (1) */
	/* never reached */
}
#else
static void mini_httpd_nommu(int server_socket, int argc, char **argv) NORETURN;
static void mini_httpd_nommu(int server_socket, int argc, char **argv)
{
	char *argv_copy[argc + 2];

	argv_copy[0] = argv[0];
	argv_copy[1] = (char*)"-i";
	memcpy(&argv_copy[2], &argv[1], argc * sizeof(argv[0]));

	/* NB: it's best to not use xfuncs in this loop before vfork().
	 * Otherwise server may die on transient errors (temporary
	 * out-of-memory condition, etc), which is Bad(tm).
	 * Try to do any dangerous calls after fork.
	 */
	while (1) {
		int n;

		/* Wait for connections... */
		n = accept(server_socket, NULL, NULL);
		if (n < 0)
			continue;

		/* set the KEEPALIVE option to cull dead connections */
		setsockopt_keepalive(n);

		if (vfork() == 0) {
			/* child */
			/* Do not reload config on HUP */
			signal(SIGHUP, SIG_IGN);
			close(server_socket);
			xmove_fd(n, 0);
			xdup2(0, 1);

			/* Run a copy of ourself in inetd mode */
			re_exec(argv_copy);
		}
		argv_copy[0][0] &= 0x7f;
		/* parent, or vfork failed */
		close(n);
	} /* while (1) */
	/* never reached */
}
#endif

/*
 * Process a HTTP connection on stdin/out.
 * Never returns.
 */
static void mini_httpd_inetd(void) NORETURN;
static void mini_httpd_inetd(void)
{
	len_and_sockaddr fromAddr;

	memset(&fromAddr, 0, sizeof(fromAddr));
	fromAddr.len = LSA_SIZEOF_SA;
	/* NB: can fail if user runs it by hand and types in http cmds */
	getpeername(0, &fromAddr.u.sa, &fromAddr.len);
	handle_incoming_and_exit(&fromAddr);
}

static void sighup_handler(int sig UNUSED_PARAM)
{
	parse_conf(DEFAULT_PATH_HTTPD_CONF, SIGNALED_PARSE);
}

enum {
	c_opt_config_file = 0,
	d_opt_decode_url,
	h_opt_home_httpd,
	IF_FEATURE_HTTPD_ENCODE_URL_STR(e_opt_encode_url,)
	IF_FEATURE_HTTPD_BASIC_AUTH(    r_opt_realm     ,)
	IF_FEATURE_HTTPD_AUTH_MD5(      m_opt_md5       ,)
	IF_FEATURE_HTTPD_SETUID(        u_opt_setuid    ,)
	p_opt_port      ,
	p_opt_inetd     ,
	p_opt_foreground,
	p_opt_verbose   ,
	OPT_CONFIG_FILE = 1 << c_opt_config_file,
	OPT_DECODE_URL  = 1 << d_opt_decode_url,
	OPT_HOME_HTTPD  = 1 << h_opt_home_httpd,
	OPT_ENCODE_URL  = IF_FEATURE_HTTPD_ENCODE_URL_STR((1 << e_opt_encode_url)) + 0,
	OPT_REALM       = IF_FEATURE_HTTPD_BASIC_AUTH(    (1 << r_opt_realm     )) + 0,
	OPT_MD5         = IF_FEATURE_HTTPD_AUTH_MD5(      (1 << m_opt_md5       )) + 0,
	OPT_SETUID      = IF_FEATURE_HTTPD_SETUID(        (1 << u_opt_setuid    )) + 0,
	OPT_PORT        = 1 << p_opt_port,
	OPT_INETD       = 1 << p_opt_inetd,
	OPT_FOREGROUND  = 1 << p_opt_foreground,
	OPT_VERBOSE     = 1 << p_opt_verbose,
};


int httpd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int httpd_main(int argc UNUSED_PARAM, char **argv)
{
	int server_socket = server_socket; /* for gcc */
	unsigned opt;
	char *url_for_decode;
	IF_FEATURE_HTTPD_ENCODE_URL_STR(const char *url_for_encode;)
	IF_FEATURE_HTTPD_SETUID(const char *s_ugid = NULL;)
	IF_FEATURE_HTTPD_SETUID(struct bb_uidgid_t ugid;)
	IF_FEATURE_HTTPD_AUTH_MD5(const char *pass;)

	INIT_G();

#if ENABLE_LOCALE_SUPPORT
	/* Undo busybox.c: we want to speak English in http (dates etc) */
	setlocale(LC_TIME, "C");
#endif

	home_httpd = xrealloc_getcwd_or_warn(NULL);
	/* We do not "absolutize" path given by -h (home) opt.
	 * If user gives relative path in -h,
	 * $SCRIPT_FILENAME will not be set. */
	opt = getopt32(argv, "^"
			"c:d:h:"
			IF_FEATURE_HTTPD_ENCODE_URL_STR("e:")
			IF_FEATURE_HTTPD_BASIC_AUTH("r:")
			IF_FEATURE_HTTPD_AUTH_MD5("m:")
			IF_FEATURE_HTTPD_SETUID("u:")
			"p:ifv"
			"\0"
			/* -v counts, -i implies -f */
			"vv:if",
			&opt_c_configFile, &url_for_decode, &home_httpd
			IF_FEATURE_HTTPD_ENCODE_URL_STR(, &url_for_encode)
			IF_FEATURE_HTTPD_BASIC_AUTH(, &g_realm)
			IF_FEATURE_HTTPD_AUTH_MD5(, &pass)
			IF_FEATURE_HTTPD_SETUID(, &s_ugid)
			, &bind_addr_or_port
			, &verbose
		);
	if (opt & OPT_DECODE_URL) {
		fputs(percent_decode_in_place(url_for_decode, /*strict:*/ 0), stdout);
		return 0;
	}
#if ENABLE_FEATURE_HTTPD_ENCODE_URL_STR
	if (opt & OPT_ENCODE_URL) {
		fputs(encodeString(url_for_encode), stdout);
		return 0;
	}
#endif
#if ENABLE_FEATURE_HTTPD_AUTH_MD5
	if (opt & OPT_MD5) {
		char salt[sizeof("$1$XXXXXXXX")];
		salt[0] = '$';
		salt[1] = '1';
		salt[2] = '$';
		crypt_make_salt(salt + 3, 4);
		puts(pw_encrypt(pass, salt, /*cleanup:*/ 0));
		return 0;
	}
#endif
#if ENABLE_FEATURE_HTTPD_SETUID
	if (opt & OPT_SETUID) {
		xget_uidgid(&ugid, s_ugid);
	}
#endif

#if !BB_MMU
	if (!(opt & OPT_FOREGROUND)) {
		bb_daemonize_or_rexec(0, argv); /* don't change current directory */
	}
#endif

	xchdir(home_httpd);
	if (!(opt & OPT_INETD)) {
		signal(SIGCHLD, SIG_IGN);
		server_socket = openServer();
#if ENABLE_FEATURE_HTTPD_SETUID
		/* drop privileges */
		if (opt & OPT_SETUID) {
			if (ugid.gid != (gid_t)-1) {
				if (setgroups(1, &ugid.gid) == -1)
					bb_perror_msg_and_die("setgroups");
				xsetgid(ugid.gid);
			}
			xsetuid(ugid.uid);
		}
#endif
	}

#if 0
	/* User can do it himself: 'env - PATH="$PATH" httpd'
	 * We don't do it because we don't want to screw users
	 * which want to do
	 * 'env - VAR1=val1 VAR2=val2 httpd'
	 * and have VAR1 and VAR2 values visible in their CGIs.
	 * Besides, it is also smaller. */
	{
		char *p = getenv("PATH");
		/* env strings themself are not freed, no need to xstrdup(p): */
		clearenv();
		if (p)
			putenv(p - 5);
//		if (!(opt & OPT_INETD))
//			setenv_long("SERVER_PORT", ???);
	}
#endif

	parse_conf(DEFAULT_PATH_HTTPD_CONF, FIRST_PARSE);
	if (!(opt & OPT_INETD))
		signal(SIGHUP, sighup_handler);

	xfunc_error_retval = 0;
	if (opt & OPT_INETD)
		mini_httpd_inetd(); /* never returns */
#if BB_MMU
	if (!(opt & OPT_FOREGROUND))
		bb_daemonize(0); /* don't change current directory */
	mini_httpd(server_socket); /* never returns */
#else
	mini_httpd_nommu(server_socket, argc, argv); /* never returns */
#endif
	/* return 0; */
}
