/*	$OpenBSD: ftp-proxy.c,v 1.19 2008/06/13 07:25:26 claudio Exp $ */

/*
 * Copyright (c) 2004, 2005 Camiel Dobbelaar, <cd@sentia.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <vis.h>

#include "filter.h"

#define CONNECT_TIMEOUT	30
#define MIN_PORT	1024
#define MAX_LINE	500
#define MAX_LOGLINE	300
#define NTOP_BUFS	3
#define TCP_BACKLOG	10

#define CHROOT_DIR	"/var/empty"
#define NOPRIV_USER	"proxy"

/* pfctl standard NAT range. */
#define PF_NAT_PROXY_PORT_LOW	50001
#define PF_NAT_PROXY_PORT_HIGH	65535

#ifndef LIST_END
#define LIST_END(a)     NULL
#endif

#ifndef getrtable
#define getrtable(a)    0
#endif

#define	sstosa(ss)	((struct sockaddr *)(ss))

enum { CMD_NONE = 0, CMD_PORT, CMD_EPRT, CMD_PASV, CMD_EPSV };

struct session {
	u_int32_t		 id;
	struct sockaddr_storage  client_ss;
	struct sockaddr_storage  proxy_ss;
	struct sockaddr_storage  server_ss;
	struct sockaddr_storage  orig_server_ss;
	struct bufferevent	*client_bufev;
	struct bufferevent	*server_bufev;
	int			 client_fd;
	int			 server_fd;
	char			 cbuf[MAX_LINE];
	size_t			 cbuf_valid;
	char			 sbuf[MAX_LINE];
	size_t			 sbuf_valid;
	int			 cmd;
	u_int16_t		 port;
	u_int16_t		 proxy_port;
	LIST_ENTRY(session)	 entry;
};

LIST_HEAD(, session) sessions = LIST_HEAD_INITIALIZER(sessions);

void	client_error(struct bufferevent *, short, void *);
int	client_parse(struct session *s);
int	client_parse_anon(struct session *s);
int	client_parse_cmd(struct session *s);
void	client_read(struct bufferevent *, void *);
int	drop_privs(void);
void	end_session(struct session *);
void	exit_daemon(void);
int	get_line(char *, size_t *);
void	handle_connection(const int, short, void *);
void	handle_signal(int, short, void *);
struct session * init_session(void);
void	logmsg(int, const char *, ...);
u_int16_t parse_port(int);
u_int16_t pick_proxy_port(void);
void	proxy_reply(int, struct sockaddr *, u_int16_t);
void	server_error(struct bufferevent *, short, void *);
int	server_parse(struct session *s);
int	allow_data_connection(struct session *s);
void	server_read(struct bufferevent *, void *);
const char *sock_ntop(struct sockaddr *);
void	usage(void);

char linebuf[MAX_LINE + 1];
size_t linelen;

char ntop_buf[NTOP_BUFS][INET6_ADDRSTRLEN];

struct sockaddr_storage fixed_server_ss, fixed_proxy_ss;
const char *fixed_server, *fixed_server_port, *fixed_proxy, *listen_ip, *listen_port,
    *qname, *tagname;
int anonymous_only, daemonize, id_count, ipv6_mode, loglevel, max_sessions,
    rfc_mode, session_count, timeout, verbose;
extern char *__progname;

void
client_error(struct bufferevent *bufev __unused, short what, void *arg)
{
	struct session *s = arg;

	if (what & EVBUFFER_EOF)
		logmsg(LOG_INFO, "#%d client close", s->id);
	else if (what == (EVBUFFER_ERROR | EVBUFFER_READ))
		logmsg(LOG_ERR, "#%d client reset connection", s->id);
	else if (what & EVBUFFER_TIMEOUT)
		logmsg(LOG_ERR, "#%d client timeout", s->id);
	else if (what & EVBUFFER_WRITE)
		logmsg(LOG_ERR, "#%d client write error: %d", s->id, what);
	else
		logmsg(LOG_ERR, "#%d abnormal client error: %d", s->id, what);

	end_session(s);
}

int
client_parse(struct session *s)
{
	/* Reset any previous command. */
	s->cmd = CMD_NONE;
	s->port = 0;

	/* Commands we are looking for are at least 4 chars long. */
	if (linelen < 4)
		return (1);

	if (linebuf[0] == 'P' || linebuf[0] == 'p' ||
	    linebuf[0] == 'E' || linebuf[0] == 'e') {
		if (!client_parse_cmd(s))
			return (0);

		/*
		 * Allow active mode connections immediately, instead of
		 * waiting for a positive reply from the server.  Some
		 * rare servers/proxies try to probe or setup the data
		 * connection before an actual transfer request.
		 */
		if (s->cmd == CMD_PORT || s->cmd == CMD_EPRT)
			return (allow_data_connection(s));
	}
	
	if (anonymous_only && (linebuf[0] == 'U' || linebuf[0] == 'u'))
		return (client_parse_anon(s));

	return (1);
}

int
client_parse_anon(struct session *s)
{
	if (strcasecmp("USER ftp\r\n", linebuf) != 0 &&
	    strcasecmp("USER anonymous\r\n", linebuf) != 0) {
		snprintf(linebuf, sizeof linebuf,
		    "500 Only anonymous FTP allowed\r\n");
		logmsg(LOG_DEBUG, "#%d proxy: %s", s->id, linebuf);

		/* Talk back to the client ourself. */
		linelen = strlen(linebuf);
		bufferevent_write(s->client_bufev, linebuf, linelen);

		/* Clear buffer so it's not sent to the server. */
		linebuf[0] = '\0';
		linelen = 0;
	}

	return (1);
}

int
client_parse_cmd(struct session *s)
{
	if (strncasecmp("PASV", linebuf, 4) == 0)
		s->cmd = CMD_PASV;
	else if (strncasecmp("PORT ", linebuf, 5) == 0)
		s->cmd = CMD_PORT;
	else if (strncasecmp("EPSV", linebuf, 4) == 0)
		s->cmd = CMD_EPSV;
	else if (strncasecmp("EPRT ", linebuf, 5) == 0)
		s->cmd = CMD_EPRT;
	else
		return (1);

	if (ipv6_mode && (s->cmd == CMD_PASV || s->cmd == CMD_PORT)) {
		logmsg(LOG_CRIT, "PASV and PORT not allowed with IPv6");
		return (0);
	}

	if (s->cmd == CMD_PORT || s->cmd == CMD_EPRT) {
		s->port = parse_port(s->cmd);
		if (s->port < MIN_PORT) {
			logmsg(LOG_CRIT, "#%d bad port in '%s'", s->id,
			    linebuf);
			return (0);
		}
		s->proxy_port = pick_proxy_port();
		proxy_reply(s->cmd, sstosa(&s->proxy_ss), s->proxy_port);
		logmsg(LOG_DEBUG, "#%d proxy: %s", s->id, linebuf);
	}

	return (1);
}

void
client_read(struct bufferevent *bufev, void *arg)
{
	struct session	*s = arg;
	size_t		 buf_avail, clientread;
	int		 n;

	do {
		buf_avail = sizeof s->cbuf - s->cbuf_valid;
		clientread = bufferevent_read(bufev, s->cbuf + s->cbuf_valid,
		    buf_avail);
		s->cbuf_valid += clientread;

		while ((n = get_line(s->cbuf, &s->cbuf_valid)) > 0) {
			logmsg(LOG_DEBUG, "#%d client: %s", s->id, linebuf);
			if (!client_parse(s)) {
				end_session(s);
				return;
			}
			bufferevent_write(s->server_bufev, linebuf, linelen);
		}

		if (n == -1) {
			logmsg(LOG_ERR, "#%d client command too long or not"
			    " clean", s->id);
			end_session(s);
			return;
		}
	} while (clientread == buf_avail);
}

int
drop_privs(void)
{
	struct passwd *pw;

	pw = getpwnam(NOPRIV_USER);
	if (pw == NULL)
		return (0);

	tzset();
	if (chroot(CHROOT_DIR) != 0 || chdir("/") != 0 ||
	    setgroups(1, &pw->pw_gid) != 0 ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0 ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0)
		return (0);

	return (1);
}

void
end_session(struct session *s)
{
	int serr;

	logmsg(LOG_INFO, "#%d ending session", s->id);

	/* Flush output buffers. */
	if (s->client_bufev && s->client_fd != -1)
		evbuffer_write(s->client_bufev->output, s->client_fd);
	if (s->server_bufev && s->server_fd != -1)
		evbuffer_write(s->server_bufev->output, s->server_fd);

	if (s->client_fd != -1)
		close(s->client_fd);
	if (s->server_fd != -1)
		close(s->server_fd);

	if (s->client_bufev)
		bufferevent_free(s->client_bufev);
	if (s->server_bufev)
		bufferevent_free(s->server_bufev);

	/* Remove rulesets by commiting empty ones. */
	serr = 0;
	if (prepare_commit(s->id) == -1)
		serr = errno;
	else if (do_commit() == -1) {
		serr = errno;
		do_rollback();
	}
	if (serr)
		logmsg(LOG_ERR, "#%d pf rule removal failed: %s", s->id,
		    strerror(serr));

	LIST_REMOVE(s, entry);
	free(s);
	session_count--;
}

void
exit_daemon(void)
{
	struct session *s, *next;

	for (s = LIST_FIRST(&sessions); s != LIST_END(&sessions); s = next) {
		next = LIST_NEXT(s, entry);
		end_session(s);
	}

	if (daemonize)
		closelog();

	exit(0);
}

int
get_line(char *buf, size_t *valid)
{
	size_t i;

	if (*valid > MAX_LINE)
		return (-1);

	/* Copy to linebuf while searching for a newline. */
	for (i = 0; i < *valid; i++) {
		linebuf[i] = buf[i];
		if (buf[i] == '\0')
			return (-1);
		if (buf[i] == '\n')
			break;
	}

	if (i == *valid) {
		/* No newline found. */
		linebuf[0] = '\0';
		linelen = 0;
		if (i < MAX_LINE)
			return (0);
		return (-1);
	}

	linelen = i + 1;
	linebuf[linelen] = '\0';
	*valid -= linelen;
	
	/* Move leftovers to the start. */
	if (*valid != 0)
		bcopy(buf + linelen, buf, *valid);

	return ((int)linelen);
}

void
handle_connection(const int listen_fd, short event __unused, void *ev __unused)
{
	struct sockaddr_storage tmp_ss;
	struct sockaddr *client_sa, *server_sa, *fixed_server_sa;
	struct sockaddr *client_to_proxy_sa, *proxy_to_server_sa;
	struct session *s;
	socklen_t len;
	int client_fd, fc, on;

	/*
	 * We _must_ accept the connection, otherwise libevent will keep
	 * coming back, and we will chew up all CPU.
	 */
	client_sa = sstosa(&tmp_ss);
	len = sizeof(struct sockaddr_storage);
	if ((client_fd = accept(listen_fd, client_sa, &len)) < 0) {
		logmsg(LOG_CRIT, "accept failed: %s", strerror(errno));
		return;
	}

	/* Refuse connection if the maximum is reached. */
	if (session_count >= max_sessions) {
		logmsg(LOG_ERR, "client limit (%d) reached, refusing "
		    "connection from %s", max_sessions, sock_ntop(client_sa));
		close(client_fd);
		return;
	}

	/* Allocate session and copy back the info from the accept(). */
	s = init_session();
	if (s == NULL) {
		logmsg(LOG_CRIT, "init_session failed");
		close(client_fd);
		return;
	}
	s->client_fd = client_fd;
	memcpy(sstosa(&s->client_ss), client_sa, client_sa->sa_len);

	/* Cast it once, and be done with it. */
	client_sa = sstosa(&s->client_ss);
	server_sa = sstosa(&s->server_ss);
	client_to_proxy_sa = sstosa(&tmp_ss);
	proxy_to_server_sa = sstosa(&s->proxy_ss);
	fixed_server_sa = sstosa(&fixed_server_ss);

	/* Log id/client early to ease debugging. */
	logmsg(LOG_DEBUG, "#%d accepted connection from %s", s->id,
	    sock_ntop(client_sa));

	/*
	 * Find out the real server and port that the client wanted.
	 */
	len = sizeof(struct sockaddr_storage);
	if ((getsockname(s->client_fd, client_to_proxy_sa, &len)) < 0) {
		logmsg(LOG_CRIT, "#%d getsockname failed: %s", s->id,
		    strerror(errno));
		goto fail;
	}
	if (server_lookup(client_sa, client_to_proxy_sa, server_sa) != 0) {
	    	logmsg(LOG_CRIT, "#%d server lookup failed (no rdr?)", s->id);
		goto fail;
	}
	if (fixed_server) {
		memcpy(sstosa(&s->orig_server_ss), server_sa,
		    server_sa->sa_len);
		memcpy(server_sa, fixed_server_sa, fixed_server_sa->sa_len);
	}

	/* XXX: check we are not connecting to ourself. */

	/*
	 * Setup socket and connect to server.
	 */
	if ((s->server_fd = socket(server_sa->sa_family, SOCK_STREAM,
	    IPPROTO_TCP)) < 0) {
		logmsg(LOG_CRIT, "#%d server socket failed: %s", s->id,
		    strerror(errno));
		goto fail;
	}
	if (fixed_proxy && bind(s->server_fd, sstosa(&fixed_proxy_ss),
	    fixed_proxy_ss.ss_len) != 0) {
		logmsg(LOG_CRIT, "#%d cannot bind fixed proxy address: %s",
		    s->id, strerror(errno));
		goto fail;
	}

	/* Use non-blocking connect(), see CONNECT_TIMEOUT below. */
	if ((fc = fcntl(s->server_fd, F_GETFL)) == -1 ||
	    fcntl(s->server_fd, F_SETFL, fc | O_NONBLOCK) == -1) {
		logmsg(LOG_CRIT, "#%d cannot mark socket non-blocking: %s",
		    s->id, strerror(errno));
		goto fail;
	}
	if (connect(s->server_fd, server_sa, server_sa->sa_len) < 0 &&
	    errno != EINPROGRESS) {
		logmsg(LOG_CRIT, "#%d proxy cannot connect to server %s: %s",
		    s->id, sock_ntop(server_sa), strerror(errno));
		goto fail;
	}

	len = sizeof(struct sockaddr_storage);
	if ((getsockname(s->server_fd, proxy_to_server_sa, &len)) < 0) {
		logmsg(LOG_CRIT, "#%d getsockname failed: %s", s->id,
		    strerror(errno));
		goto fail;
	}

	logmsg(LOG_INFO, "#%d FTP session %d/%d started: client %s to server "
	    "%s via proxy %s ", s->id, session_count, max_sessions,
	    sock_ntop(client_sa), sock_ntop(server_sa),
	    sock_ntop(proxy_to_server_sa));

	/* Keepalive is nice, but don't care if it fails. */
	on = 1;
	setsockopt(s->client_fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on,
	    sizeof on);
	setsockopt(s->server_fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on,
	    sizeof on);

	/*
	 * Setup buffered events.
	 */
	s->client_bufev = bufferevent_new(s->client_fd, &client_read, NULL,
	    &client_error, s);
	if (s->client_bufev == NULL) {
		logmsg(LOG_CRIT, "#%d bufferevent_new client failed", s->id);
		goto fail;
	}
	bufferevent_settimeout(s->client_bufev, timeout, 0);
	bufferevent_enable(s->client_bufev, EV_READ | EV_TIMEOUT);

	s->server_bufev = bufferevent_new(s->server_fd, &server_read, NULL,
	    &server_error, s);
	if (s->server_bufev == NULL) {
		logmsg(LOG_CRIT, "#%d bufferevent_new server failed", s->id);
		goto fail;
	}
	bufferevent_settimeout(s->server_bufev, CONNECT_TIMEOUT, 0);
	bufferevent_enable(s->server_bufev, EV_READ | EV_TIMEOUT);

	return;

 fail:
	end_session(s);
}

void
handle_signal(int sig, short event __unused, void *arg __unused)
{
	/*
	 * Signal handler rules don't apply, libevent decouples for us.
	 */

	logmsg(LOG_ERR, "exiting on signal %d", sig);

	exit_daemon();
}
	

struct session *
init_session(void)
{
	struct session *s;

	s = calloc(1, sizeof(struct session));
	if (s == NULL)
		return (NULL);

	s->id = id_count++;
	s->client_fd = -1;
	s->server_fd = -1;
	s->cbuf[0] = '\0';
	s->cbuf_valid = 0;
	s->sbuf[0] = '\0';
	s->sbuf_valid = 0;
	s->client_bufev = NULL;
	s->server_bufev = NULL;
	s->cmd = CMD_NONE;
	s->port = 0;

	LIST_INSERT_HEAD(&sessions, s, entry);
	session_count++;

	return (s);
}

void
logmsg(int pri, const char *message, ...)
{
	va_list	ap;

	if (pri > loglevel)
		return;

	va_start(ap, message);

	if (daemonize)
		/* syslog does its own vissing. */
		vsyslog(pri, message, ap);
	else {
		char buf[MAX_LOGLINE];
		char visbuf[2 * MAX_LOGLINE];

		/* We don't care about truncation. */
		vsnprintf(buf, sizeof buf, message, ap);
#ifdef __FreeBSD__
		strvis(visbuf, buf, VIS_CSTYLE | VIS_NL);
#else
		strnvis(visbuf, buf, sizeof visbuf, VIS_CSTYLE | VIS_NL);
#endif
		fprintf(stderr, "%s\n", visbuf);
	}

	va_end(ap);
}

int
main(int argc, char *argv[])
{
	struct rlimit rlp;
	struct addrinfo hints, *res;
	struct event ev, ev_sighup, ev_sigint, ev_sigterm;
	int ch, error, listenfd, on;
	const char *errstr;

	/* Defaults. */
	anonymous_only	= 0;
	daemonize	= 1;
	fixed_proxy	= NULL;
	fixed_server	= NULL;
	fixed_server_port = "21";
	ipv6_mode	= 0;
	listen_ip	= NULL;
	listen_port	= "8021";
	loglevel	= LOG_NOTICE;
	max_sessions	= 100;
	qname		= NULL;
	rfc_mode	= 0;
	tagname		= NULL;
	timeout		= 24 * 3600;
	verbose		= 0;

	/* Other initialization. */
	id_count	= 1;
	session_count	= 0;

	while ((ch = getopt(argc, argv, "6Aa:b:D:dm:P:p:q:R:rT:t:v")) != -1) {
		switch (ch) {
		case '6':
			ipv6_mode = 1;
			break;
		case 'A':
			anonymous_only = 1;
			break;
		case 'a':
			fixed_proxy = optarg;
			break;
		case 'b':
			listen_ip = optarg;
			break;
		case 'D':
			loglevel = strtonum(optarg, LOG_EMERG, LOG_DEBUG,
			    &errstr);
			if (errstr)
				errx(1, "loglevel %s", errstr);
			break;
		case 'd':
			daemonize = 0;
			break;
		case 'm':
			max_sessions = strtonum(optarg, 1, 500, &errstr);
			if (errstr)
				errx(1, "max sessions %s", errstr);
			break;
		case 'P':
			fixed_server_port = optarg;
			break;
		case 'p':
			listen_port = optarg;
			break;
		case 'q':
			if (strlen(optarg) >= PF_QNAME_SIZE)
				errx(1, "queuename too long");
			qname = optarg;
			break;
		case 'R':
			fixed_server = optarg;
			break;
		case 'r':
			rfc_mode = 1;
			break;
		case 'T':
			if (strlen(optarg) >= PF_TAG_NAME_SIZE)
				errx(1, "tagname too long");
			tagname = optarg;
			break;
		case 't':
			timeout = strtonum(optarg, 0, 86400, &errstr);
			if (errstr)
				errx(1, "timeout %s", errstr);
			break;
		case 'v':
			verbose++;
			if (verbose > 2)
				usage();
			break;
		default:
			usage();
		}
	}

	if (listen_ip == NULL)
		listen_ip = ipv6_mode ? "::1" : "127.0.0.1";

	/* Check for root to save the user from cryptic failure messages. */
	if (getuid() != 0)
		errx(1, "needs to start as root");

	/* Raise max. open files limit to satisfy max. sessions. */
	rlp.rlim_cur = rlp.rlim_max = (2 * max_sessions) + 10;
	if (setrlimit(RLIMIT_NOFILE, &rlp) == -1)
		err(1, "setrlimit");

	if (fixed_proxy) {
		memset(&hints, 0, sizeof hints);
		hints.ai_flags = AI_NUMERICHOST;
		hints.ai_family = ipv6_mode ? AF_INET6 : AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		error = getaddrinfo(fixed_proxy, NULL, &hints, &res);
		if (error)
			errx(1, "getaddrinfo fixed proxy address failed: %s",
			    gai_strerror(error));
		memcpy(&fixed_proxy_ss, res->ai_addr, res->ai_addrlen);
		logmsg(LOG_INFO, "using %s to connect to servers",
		    sock_ntop(sstosa(&fixed_proxy_ss)));
		freeaddrinfo(res);
	}

	if (fixed_server) {
		memset(&hints, 0, sizeof hints);
		hints.ai_family = ipv6_mode ? AF_INET6 : AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		error = getaddrinfo(fixed_server, fixed_server_port, &hints,
		    &res);
		if (error)
			errx(1, "getaddrinfo fixed server address failed: %s",
			    gai_strerror(error));
		memcpy(&fixed_server_ss, res->ai_addr, res->ai_addrlen);
		logmsg(LOG_INFO, "using fixed server %s",
		    sock_ntop(sstosa(&fixed_server_ss)));
		freeaddrinfo(res);
	}

	/* Setup listener. */
	memset(&hints, 0, sizeof hints);
	hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
	hints.ai_family = ipv6_mode ? AF_INET6 : AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(listen_ip, listen_port, &hints, &res);
	if (error)
		errx(1, "getaddrinfo listen address failed: %s",
		    gai_strerror(error));
	if ((listenfd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP)) == -1)
		errx(1, "socket failed");
	on = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on,
	    sizeof on) != 0)
		err(1, "setsockopt failed");
	if (bind(listenfd, (struct sockaddr *)res->ai_addr,
	    (socklen_t)res->ai_addrlen) != 0)
	    	err(1, "bind failed");
	if (listen(listenfd, TCP_BACKLOG) != 0)
		err(1, "listen failed");
	freeaddrinfo(res);

	/* Initialize pf. */
	init_filter(qname, tagname, verbose);

	if (daemonize) {
		if (daemon(0, 0) == -1)
			err(1, "cannot daemonize");
		openlog(__progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);
	}

	/* Use logmsg for output from here on. */

	if (!drop_privs()) {
		logmsg(LOG_ERR, "cannot drop privileges: %s", strerror(errno));
		exit(1);
	}
	
	event_init();

	/* Setup signal handler. */
	signal(SIGPIPE, SIG_IGN);
	signal_set(&ev_sighup, SIGHUP, handle_signal, NULL);
	signal_set(&ev_sigint, SIGINT, handle_signal, NULL);
	signal_set(&ev_sigterm, SIGTERM, handle_signal, NULL);
	signal_add(&ev_sighup, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);

	event_set(&ev, listenfd, EV_READ | EV_PERSIST, handle_connection, &ev);
	event_add(&ev, NULL);

	logmsg(LOG_NOTICE, "listening on %s port %s", listen_ip, listen_port);

	/*  Vroom, vroom.  */
	event_dispatch();

	logmsg(LOG_ERR, "event_dispatch error: %s", strerror(errno));
	exit_daemon();

	/* NOTREACHED */
	return (1);
}

u_int16_t
parse_port(int mode)
{
	unsigned int	 port, v[6];
	int		 n;
	char		*p;

	/* Find the last space or left-parenthesis. */
	for (p = linebuf + linelen; p > linebuf; p--)
		if (*p == ' ' || *p == '(')
			break;
	if (p == linebuf)
		return (0);

	switch (mode) {
	case CMD_PORT:
		n = sscanf(p, " %u,%u,%u,%u,%u,%u", &v[0], &v[1], &v[2],
		    &v[3], &v[4], &v[5]);
		if (n == 6 && v[0] < 256 && v[1] < 256 && v[2] < 256 &&
		    v[3] < 256 && v[4] < 256 && v[5] < 256)
			return ((v[4] << 8) | v[5]);
		break;
	case CMD_PASV:
		n = sscanf(p, "(%u,%u,%u,%u,%u,%u)", &v[0], &v[1], &v[2],
		    &v[3], &v[4], &v[5]);
		if (n == 6 && v[0] < 256 && v[1] < 256 && v[2] < 256 &&
		    v[3] < 256 && v[4] < 256 && v[5] < 256)
			return ((v[4] << 8) | v[5]);
		break;
	case CMD_EPSV:
		n = sscanf(p, "(|||%u|)", &port);
		if (n == 1 && port < 65536)
			return (port);
		break;
	case CMD_EPRT:
		n = sscanf(p, " |1|%u.%u.%u.%u|%u|", &v[0], &v[1], &v[2],
		    &v[3], &port);
		if (n == 5 && v[0] < 256 && v[1] < 256 && v[2] < 256 &&
		    v[3] < 256 && port < 65536)
			return (port);
		n = sscanf(p, " |2|%*[a-fA-F0-9:]|%u|", &port);
		if (n == 1 && port < 65536)
			return (port);
		break;
	default:
		return (0);
	}

	return (0);
}

u_int16_t
pick_proxy_port(void)
{
	/* Random should be good enough for avoiding port collisions. */
	return (IPPORT_HIFIRSTAUTO +
	    arc4random_uniform(IPPORT_HILASTAUTO - IPPORT_HIFIRSTAUTO));
}

void
proxy_reply(int cmd, struct sockaddr *sa, u_int16_t port)
{
	u_int i;
	int r = 0;

	switch (cmd) {
	case CMD_PORT:
		r = snprintf(linebuf, sizeof linebuf,
		    "PORT %s,%u,%u\r\n", sock_ntop(sa), port / 256,
		    port % 256);
		break;
	case CMD_PASV:
		r = snprintf(linebuf, sizeof linebuf,
		    "227 Entering Passive Mode (%s,%u,%u)\r\n", sock_ntop(sa),
		        port / 256, port % 256);
		break;
	case CMD_EPRT:
		if (sa->sa_family == AF_INET)
			r = snprintf(linebuf, sizeof linebuf,
			    "EPRT |1|%s|%u|\r\n", sock_ntop(sa), port);
		else if (sa->sa_family == AF_INET6)
			r = snprintf(linebuf, sizeof linebuf,
			    "EPRT |2|%s|%u|\r\n", sock_ntop(sa), port);
		break;
	case CMD_EPSV:
		r = snprintf(linebuf, sizeof linebuf,
		    "229 Entering Extended Passive Mode (|||%u|)\r\n", port);
		break;
	}

	if (r < 0 || ((u_int)r) >= sizeof linebuf) {
		logmsg(LOG_ERR, "proxy_reply failed: %d", r);
		linebuf[0] = '\0';
		linelen = 0;
		return;
	}
	linelen = (size_t)r;

	if (cmd == CMD_PORT || cmd == CMD_PASV) {
		/* Replace dots in IP address with commas. */
		for (i = 0; i < linelen; i++)
			if (linebuf[i] == '.')
				linebuf[i] = ',';
	}
}

void
server_error(struct bufferevent *bufev __unused, short what, void *arg)
{
	struct session *s = arg;

	if (what & EVBUFFER_EOF)
		logmsg(LOG_INFO, "#%d server close", s->id);
	else if (what == (EVBUFFER_ERROR | EVBUFFER_READ))
		logmsg(LOG_ERR, "#%d server refused connection", s->id);
	else if (what & EVBUFFER_WRITE)
		logmsg(LOG_ERR, "#%d server write error: %d", s->id, what);
	else if (what & EVBUFFER_TIMEOUT)
		logmsg(LOG_NOTICE, "#%d server timeout", s->id);
	else
		logmsg(LOG_ERR, "#%d abnormal server error: %d", s->id, what);

	end_session(s);
}

int
server_parse(struct session *s)
{
	if (s->cmd == CMD_NONE || linelen < 4 || linebuf[0] != '2')
		goto out;

	if ((s->cmd == CMD_PASV && strncmp("227 ", linebuf, 4) == 0) ||
	    (s->cmd == CMD_EPSV && strncmp("229 ", linebuf, 4) == 0))
		return (allow_data_connection(s));

 out:
	s->cmd = CMD_NONE;
	s->port = 0;

	return (1);
}

int
allow_data_connection(struct session *s)
{
	struct sockaddr *client_sa, *orig_sa, *proxy_sa, *server_sa;
	int prepared = 0;

	/*
	 * The pf rules below do quite some NAT rewriting, to keep up
	 * appearances.  Points to keep in mind:
	 * 1)  The client must think it's talking to the real server,
	 *     for both control and data connections.  Transparently.
	 * 2)  The server must think that the proxy is the client.
	 * 3)  Source and destination ports are rewritten to minimize
	 *     port collisions, to aid security (some systems pick weak
	 *     ports) or to satisfy RFC requirements (source port 20).
	 */
	
	/* Cast this once, to make code below it more readable. */
	client_sa = sstosa(&s->client_ss);
	server_sa = sstosa(&s->server_ss);
	proxy_sa = sstosa(&s->proxy_ss);
	if (fixed_server)
		/* Fixed server: data connections must appear to come
		   from / go to the original server, not the fixed one. */
		orig_sa = sstosa(&s->orig_server_ss);
	else
		/* Server not fixed: orig_server == server. */
		orig_sa = sstosa(&s->server_ss);

	/* Passive modes. */
	if (s->cmd == CMD_PASV || s->cmd == CMD_EPSV) {
		s->port = parse_port(s->cmd);
		if (s->port < MIN_PORT) {
			logmsg(LOG_CRIT, "#%d bad port in '%s'", s->id,
			    linebuf);
			return (0);
		}
		s->proxy_port = pick_proxy_port();
		logmsg(LOG_INFO, "#%d passive: client to server port %d"
		    " via port %d", s->id, s->port, s->proxy_port);

		if (prepare_commit(s->id) == -1)
			goto fail;
		prepared = 1;

		proxy_reply(s->cmd, orig_sa, s->proxy_port);
		logmsg(LOG_DEBUG, "#%d proxy: %s", s->id, linebuf);

		/* rdr from $client to $orig_server port $proxy_port -> $server
		    port $port */
		if (add_rdr(s->id, client_sa, orig_sa, s->proxy_port,
		    server_sa, s->port) == -1)
			goto fail;

		/* nat from $client to $server port $port -> $proxy */
		if (add_nat(s->id, client_sa, server_sa, s->port, proxy_sa,
		    PF_NAT_PROXY_PORT_LOW, PF_NAT_PROXY_PORT_HIGH) == -1)
			goto fail;

		/* pass in from $client to $server port $port */
		if (add_filter(s->id, PF_IN, client_sa, server_sa,
		    s->port) == -1)
			goto fail;

		/* pass out from $proxy to $server port $port */
		if (add_filter(s->id, PF_OUT, proxy_sa, server_sa,
		    s->port) == -1)
			goto fail;
	}

	/* Active modes. */
	if (s->cmd == CMD_PORT || s->cmd == CMD_EPRT) {
		logmsg(LOG_INFO, "#%d active: server to client port %d"
		    " via port %d", s->id, s->port, s->proxy_port);

		if (prepare_commit(s->id) == -1)
			goto fail;
		prepared = 1;

		/* rdr from $server to $proxy port $proxy_port -> $client port
		    $port */
		if (add_rdr(s->id, server_sa, proxy_sa, s->proxy_port,
		    client_sa, s->port) == -1)
			goto fail;

		/* nat from $server to $client port $port -> $orig_server port
		    $natport */
		if (rfc_mode && s->cmd == CMD_PORT) {
			/* Rewrite sourceport to RFC mandated 20. */
			if (add_nat(s->id, server_sa, client_sa, s->port,
			    orig_sa, 20, 20) == -1)
				goto fail;
		} else {
			/* Let pf pick a source port from the standard range. */
			if (add_nat(s->id, server_sa, client_sa, s->port,
			    orig_sa, PF_NAT_PROXY_PORT_LOW,
			    PF_NAT_PROXY_PORT_HIGH) == -1)
			    	goto fail;
		}

		/* pass in from $server to $client port $port */
		if (add_filter(s->id, PF_IN, server_sa, client_sa, s->port) ==
		    -1)
			goto fail;

		/* pass out from $orig_server to $client port $port */
		if (add_filter(s->id, PF_OUT, orig_sa, client_sa, s->port) ==
		    -1)
			goto fail;
	}

	/* Commit rules if they were prepared. */
	if (prepared && (do_commit() == -1)) {
		if (errno != EBUSY)
			goto fail;
		/* One more try if busy. */
		usleep(5000);
		if (do_commit() == -1)
			goto fail;
	}

	s->cmd = CMD_NONE;
	s->port = 0;

	return (1);

 fail:
	logmsg(LOG_CRIT, "#%d pf operation failed: %s", s->id, strerror(errno));
	if (prepared)
		do_rollback();
	return (0);
}
	
void
server_read(struct bufferevent *bufev, void *arg)
{
	struct session	*s = arg;
	size_t		 buf_avail, srvread;
	int		 n;

	bufferevent_settimeout(bufev, timeout, 0);

	do {
		buf_avail = sizeof s->sbuf - s->sbuf_valid;
		srvread = bufferevent_read(bufev, s->sbuf + s->sbuf_valid,
		    buf_avail);
		s->sbuf_valid += srvread;

		while ((n = get_line(s->sbuf, &s->sbuf_valid)) > 0) {
			logmsg(LOG_DEBUG, "#%d server: %s", s->id, linebuf);
			if (!server_parse(s)) {
				end_session(s);
				return;
			}
			bufferevent_write(s->client_bufev, linebuf, linelen);
		}

		if (n == -1) {
			logmsg(LOG_ERR, "#%d server reply too long or not"
			    " clean", s->id);
			end_session(s);
			return;
		}
	} while (srvread == buf_avail);
}

const char *
sock_ntop(struct sockaddr *sa)
{
	static int n = 0;

	/* Cycle to next buffer. */
	n = (n + 1) % NTOP_BUFS;
	ntop_buf[n][0] = '\0';

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		return (inet_ntop(AF_INET, &sin->sin_addr, ntop_buf[n],
		    sizeof ntop_buf[0]));
	}

	if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		return (inet_ntop(AF_INET6, &sin6->sin6_addr, ntop_buf[n],
		    sizeof ntop_buf[0]));
	}

	return (NULL);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-6Adrv] [-a address] [-b address]"
	    " [-D level] [-m maxsessions]\n                 [-P port]"
	    " [-p port] [-q queue] [-R address] [-T tag]\n"
            "                 [-t timeout]\n", __progname);
	exit(1);
}
