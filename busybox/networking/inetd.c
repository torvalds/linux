/* vi: set sw=4 ts=4: */
/*      $Slackware: inetd.c 1.79s 2001/02/06 13:18:00 volkerdi Exp $    */
/*      $OpenBSD: inetd.c,v 1.79 2001/01/30 08:30:57 deraadt Exp $      */
/*      $NetBSD: inetd.c,v 1.11 1996/02/22 11:14:41 mycroft Exp $       */
/* Busybox port by Vladimir Oleynik (C) 2001-2005 <dzo@simtreas.ru>     */
/* IPv6 support, many bug fixes by Denys Vlasenko (c) 2008 */
/*
 * Copyright (c) 1983,1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Inetd - Internet super-server
 *
 * This program invokes configured services when a connection
 * from a peer is established or a datagram arrives.
 * Connection-oriented services are invoked each time a
 * connection is made, by creating a process.  This process
 * is passed the connection as file descriptor 0 and is
 * expected to do a getpeername to find out peer's host
 * and port.
 * Datagram oriented services are invoked when a datagram
 * arrives; a process is created and passed a pending message
 * on file descriptor 0. peer's address can be obtained
 * using recvfrom.
 *
 * Inetd uses a configuration file which is read at startup
 * and, possibly, at some later time in response to a hangup signal.
 * The configuration file is "free format" with fields given in the
 * order shown below.  Continuation lines for an entry must begin with
 * a space or tab.  All fields must be present in each entry.
 *
 *      service_name                    must be in /etc/services
 *      socket_type                     stream/dgram/raw/rdm/seqpacket
 *      protocol                        must be in /etc/protocols
 *                                      (usually "tcp" or "udp")
 *      wait/nowait[.max]               single-threaded/multi-threaded, max #
 *      user[.group] or user[:group]    user/group to run daemon as
 *      server_program                  full path name
 *      server_program_arguments        maximum of MAXARGS (20)
 *
 * For RPC services
 *      service_name/version            must be in /etc/rpc
 *      socket_type                     stream/dgram/raw/rdm/seqpacket
 *      rpc/protocol                    "rpc/tcp" etc
 *      wait/nowait[.max]               single-threaded/multi-threaded
 *      user[.group] or user[:group]    user to run daemon as
 *      server_program                  full path name
 *      server_program_arguments        maximum of MAXARGS (20)
 *
 * For non-RPC services, the "service name" can be of the form
 * hostaddress:servicename, in which case the hostaddress is used
 * as the host portion of the address to listen on.  If hostaddress
 * consists of a single '*' character, INADDR_ANY is used.
 *
 * A line can also consist of just
 *      hostaddress:
 * where hostaddress is as in the preceding paragraph.  Such a line must
 * have no further fields; the specified hostaddress is remembered and
 * used for all further lines that have no hostaddress specified,
 * until the next such line (or EOF).  (This is why * is provided to
 * allow explicit specification of INADDR_ANY.)  A line
 *      *:
 * is implicitly in effect at the beginning of the file.
 *
 * The hostaddress specifier may (and often will) contain dots;
 * the service name must not.
 *
 * For RPC services, host-address specifiers are accepted and will
 * work to some extent; however, because of limitations in the
 * portmapper interface, it will not work to try to give more than
 * one line for any given RPC service, even if the host-address
 * specifiers are different.
 *
 * Comment lines are indicated by a '#' in column 1.
 */

/* inetd rules for passing file descriptors to children
 * (http://www.freebsd.org/cgi/man.cgi?query=inetd):
 *
 * The wait/nowait entry specifies whether the server that is invoked by
 * inetd will take over the socket associated with the service access point,
 * and thus whether inetd should wait for the server to exit before listen-
 * ing for new service requests.  Datagram servers must use "wait", as
 * they are always invoked with the original datagram socket bound to the
 * specified service address.  These servers must read at least one datagram
 * from the socket before exiting.  If a datagram server connects to its
 * peer, freeing the socket so inetd can receive further messages on the
 * socket, it is said to be a "multi-threaded" server; it should read one
 * datagram from the socket and create a new socket connected to the peer.
 * It should fork, and the parent should then exit to allow inetd to check
 * for new service requests to spawn new servers.  Datagram servers which
 * process all incoming datagrams on a socket and eventually time out are
 * said to be "single-threaded".  The comsat(8), biff(1) and talkd(8)
 * utilities are both examples of the latter type of datagram server.  The
 * tftpd(8) utility is an example of a multi-threaded datagram server.
 *
 * Servers using stream sockets generally are multi-threaded and use the
 * "nowait" entry. Connection requests for these services are accepted by
 * inetd, and the server is given only the newly-accepted socket connected
 * to a client of the service.  Most stream-based services operate in this
 * manner.  Stream-based servers that use "wait" are started with the lis-
 * tening service socket, and must accept at least one connection request
 * before exiting.  Such a server would normally accept and process incoming
 * connection requests until a timeout.
 */

/* Despite of above doc saying that dgram services must use "wait",
 * "udp nowait" servers are implemented in busyboxed inetd.
 * IPv6 addresses are also implemented. However, they may look ugly -
 * ":::service..." means "address '::' (IPv6 wildcard addr)":"service"...
 * You have to put "tcp6"/"udp6" in protocol field to select IPv6.
 */

/* Here's the scoop concerning the user[:group] feature:
 * 1) group is not specified:
 *      a) user = root: NO setuid() or setgid() is done
 *      b) other:       initgroups(name, primary group)
 *                      setgid(primary group as found in passwd)
 *                      setuid()
 * 2) group is specified:
 *      a) user = root: setgid(specified group)
 *                      NO initgroups()
 *                      NO setuid()
 *      b) other:       initgroups(name, specified group)
 *                      setgid(specified group)
 *                      setuid()
 */
//config:config INETD
//config:	bool "inetd (18 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	Internet superserver daemon
//config:
//config:config FEATURE_INETD_SUPPORT_BUILTIN_ECHO
//config:	bool "Support echo service on port 7"
//config:	default y
//config:	depends on INETD
//config:	help
//config:	Internal service which echoes data back.
//config:	Activated by configuration lines like these:
//config:		echo stream tcp nowait root internal
//config:		echo dgram  udp wait   root internal
//config:
//config:config FEATURE_INETD_SUPPORT_BUILTIN_DISCARD
//config:	bool "Support discard service on port 8"
//config:	default y
//config:	depends on INETD
//config:	help
//config:	Internal service which discards all input.
//config:	Activated by configuration lines like these:
//config:		discard stream tcp nowait root internal
//config:		discard dgram  udp wait   root internal
//config:
//config:config FEATURE_INETD_SUPPORT_BUILTIN_TIME
//config:	bool "Support time service on port 37"
//config:	default y
//config:	depends on INETD
//config:	help
//config:	Internal service which returns big-endian 32-bit number
//config:	of seconds passed since 1900-01-01. The number wraps around
//config:	on overflow.
//config:	Activated by configuration lines like these:
//config:		time stream tcp nowait root internal
//config:		time dgram  udp wait   root internal
//config:
//config:config FEATURE_INETD_SUPPORT_BUILTIN_DAYTIME
//config:	bool "Support daytime service on port 13"
//config:	default y
//config:	depends on INETD
//config:	help
//config:	Internal service which returns human-readable time.
//config:	Activated by configuration lines like these:
//config:		daytime stream tcp nowait root internal
//config:		daytime dgram  udp wait   root internal
//config:
//config:config FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN
//config:	bool "Support chargen service on port 19"
//config:	default y
//config:	depends on INETD
//config:	help
//config:	Internal service which generates endless stream
//config:	of all ASCII chars beetween space and char 126.
//config:	Activated by configuration lines like these:
//config:		chargen stream tcp nowait root internal
//config:		chargen dgram  udp wait   root internal
//config:
//config:config FEATURE_INETD_RPC
//config:	bool "Support RPC services"
//config:	default n  # very rarely used, and needs Sun RPC support in libc
//config:	depends on INETD
//config:	help
//config:	Support Sun-RPC based services

//applet:IF_INETD(APPLET(inetd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_INETD) += inetd.o

//usage:#define inetd_trivial_usage
//usage:       "[-fe] [-q N] [-R N] [CONFFILE]"
//usage:#define inetd_full_usage "\n\n"
//usage:       "Listen for network connections and launch programs\n"
//usage:     "\n	-f	Run in foreground"
//usage:     "\n	-e	Log to stderr"
//usage:     "\n	-q N	Socket listen queue (default 128)"
//usage:     "\n	-R N	Pause services after N connects/min"
//usage:     "\n		(default 0 - disabled)"
//usage:     "\n	Default CONFFILE is /etc/inetd.conf"

#include <syslog.h>
#include <sys/resource.h> /* setrlimit */
#include <sys/socket.h> /* un.h may need this */
#include <sys/un.h>

#include "libbb.h"
#include "common_bufsiz.h"

#if ENABLE_FEATURE_INETD_RPC
# if defined(__UCLIBC__) && ! defined(__UCLIBC_HAS_RPC__)
#  warning "You probably need to build uClibc with UCLIBC_HAS_RPC for NFS support"
   /* not #error, since user may be using e.g. libtirpc instead.
    * This might work:
    * CONFIG_EXTRA_CFLAGS="-I/usr/include/tirpc"
    * CONFIG_EXTRA_LDLIBS="tirpc"
    */
# endif
# include <rpc/rpc.h>
# include <rpc/pmap_clnt.h>
#endif

#if !BB_MMU
/* stream version of chargen is forking but not execing,
 * can't do that (easily) on NOMMU */
#undef  ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN
#define ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN 0
#endif

#define CNT_INTERVAL    60      /* servers in CNT_INTERVAL sec. */
#define RETRYTIME       60      /* retry after bind or server fail */

// TODO: explain, or get rid of setrlimit games

#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE   RLIMIT_OFILE
#endif

#ifndef OPEN_MAX
#define OPEN_MAX        64
#endif

/* Reserve some descriptors, 3 stdio + at least: 1 log, 1 conf. file */
#define FD_MARGIN       8

#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DISCARD \
 || ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_ECHO    \
 || ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN \
 || ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_TIME    \
 || ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DAYTIME
# define INETD_BUILTINS_ENABLED
#endif

typedef struct servtab_t {
	/* The most frequently referenced one: */
	int se_fd;                            /* open descriptor */
	/* NB: 'biggest fields last' saves on code size (~250 bytes) */
	/* [addr:]service socktype proto wait user[:group] prog [args] */
	char *se_local_hostname;              /* addr to listen on */
	char *se_service;                     /* "80" or "www" or "mount/2[-3]" */
	/* socktype is in se_socktype */      /* "stream" "dgram" "raw" "rdm" "seqpacket" */
	char *se_proto;                       /* "unix" or "[rpc/]tcp[6]" */
#if ENABLE_FEATURE_INETD_RPC
	int se_rpcprog;                       /* rpc program number */
	int se_rpcver_lo;                     /* rpc program lowest version */
	int se_rpcver_hi;                     /* rpc program highest version */
#define is_rpc_service(sep)       ((sep)->se_rpcver_lo != 0)
#else
#define is_rpc_service(sep)       0
#endif
	pid_t se_wait;                        /* 0:"nowait", 1:"wait", >1:"wait" */
	                                      /* and waiting for this pid */
	socktype_t se_socktype;               /* SOCK_STREAM/DGRAM/RDM/... */
	family_t se_family;                   /* AF_UNIX/INET[6] */
	/* se_proto_no is used by RPC code only... hmm */
	smallint se_proto_no;                 /* IPPROTO_TCP/UDP, n/a for AF_UNIX */
	smallint se_checked;                  /* looked at during merge */
	unsigned se_max;                      /* allowed instances per minute */
	unsigned se_count;                    /* number started since se_time */
	unsigned se_time;                     /* when we started counting */
	char *se_user;                        /* user name to run as */
	char *se_group;                       /* group name to run as, can be NULL */
#ifdef INETD_BUILTINS_ENABLED
	const struct builtin *se_builtin;     /* if built-in, description */
#endif
	struct servtab_t *se_next;
	len_and_sockaddr *se_lsa;
	char *se_program;                     /* server program */
#define MAXARGV 20
	char *se_argv[MAXARGV + 1];           /* program arguments */
} servtab_t;

#ifdef INETD_BUILTINS_ENABLED
/* Echo received data */
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_ECHO
static void FAST_FUNC echo_stream(int, servtab_t *);
static void FAST_FUNC echo_dg(int, servtab_t *);
#endif
/* Internet /dev/null */
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DISCARD
static void FAST_FUNC discard_stream(int, servtab_t *);
static void FAST_FUNC discard_dg(int, servtab_t *);
#endif
/* Return 32 bit time since 1900 */
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_TIME
static void FAST_FUNC machtime_stream(int, servtab_t *);
static void FAST_FUNC machtime_dg(int, servtab_t *);
#endif
/* Return human-readable time */
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DAYTIME
static void FAST_FUNC daytime_stream(int, servtab_t *);
static void FAST_FUNC daytime_dg(int, servtab_t *);
#endif
/* Familiar character generator */
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN
static void FAST_FUNC chargen_stream(int, servtab_t *);
static void FAST_FUNC chargen_dg(int, servtab_t *);
#endif

struct builtin {
	/* NB: not necessarily NUL terminated */
	char bi_service7[7];      /* internally provided service name */
	uint8_t bi_fork;          /* 1 if stream fn should run in child */
	void (*bi_stream_fn)(int, servtab_t *) FAST_FUNC;
	void (*bi_dgram_fn)(int, servtab_t *) FAST_FUNC;
};

static const struct builtin builtins[] = {
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_ECHO
	{ "echo", 1, echo_stream, echo_dg },
#endif
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DISCARD
	{ "discard", 1, discard_stream, discard_dg },
#endif
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN
	{ "chargen", 1, chargen_stream, chargen_dg },
#endif
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_TIME
	{ "time", 0, machtime_stream, machtime_dg },
#endif
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DAYTIME
	{ "daytime", 0, daytime_stream, daytime_dg },
#endif
};
#endif /* INETD_BUILTINS_ENABLED */

struct globals {
	rlim_t rlim_ofile_cur;
	struct rlimit rlim_ofile;
	servtab_t *serv_list;
	int global_queuelen;
	int maxsock;         /* max fd# in allsock, -1: unknown */
	/* whenever maxsock grows, prev_maxsock is set to new maxsock,
	 * but if maxsock is set to -1, prev_maxsock is not changed */
	int prev_maxsock;
	unsigned max_concurrency;
	smallint alarm_armed;
	uid_t real_uid; /* user ID who ran us */
	const char *config_filename;
	parser_t *parser;
	char *default_local_hostname;
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN
	char *end_ring;
	char *ring_pos;
	char ring[128];
#endif
	fd_set allsock;
	/* Used in next_line(), and as scratch read buffer */
	char line[256];          /* _at least_ 256, see LINE_SIZE */
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
enum { LINE_SIZE = COMMON_BUFSIZE - offsetof(struct globals, line) };
#define rlim_ofile_cur  (G.rlim_ofile_cur )
#define rlim_ofile      (G.rlim_ofile     )
#define serv_list       (G.serv_list      )
#define global_queuelen (G.global_queuelen)
#define maxsock         (G.maxsock        )
#define prev_maxsock    (G.prev_maxsock   )
#define max_concurrency (G.max_concurrency)
#define alarm_armed     (G.alarm_armed    )
#define real_uid        (G.real_uid       )
#define config_filename (G.config_filename)
#define parser          (G.parser         )
#define default_local_hostname (G.default_local_hostname)
#define first_ps_byte   (G.first_ps_byte  )
#define last_ps_byte    (G.last_ps_byte   )
#define end_ring        (G.end_ring       )
#define ring_pos        (G.ring_pos       )
#define ring            (G.ring           )
#define allsock         (G.allsock        )
#define line            (G.line           )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
	rlim_ofile_cur = OPEN_MAX; \
	global_queuelen = 128; \
	config_filename = "/etc/inetd.conf"; \
} while (0)

#if 1
# define dbg(...) ((void)0)
#else
# define dbg(...) \
do { \
	int dbg_fd = open("inetd_debug.log", O_WRONLY | O_CREAT | O_APPEND, 0666); \
	if (dbg_fd >= 0) { \
		fdprintf(dbg_fd, "%d: ", getpid()); \
		fdprintf(dbg_fd, __VA_ARGS__); \
		close(dbg_fd); \
	} \
} while (0)
#endif

static void maybe_close(int fd)
{
	if (fd >= 0) {
		close(fd);
		dbg("closed fd:%d\n", fd);
	}
}

// TODO: move to libbb?
static len_and_sockaddr *xzalloc_lsa(int family)
{
	len_and_sockaddr *lsa;
	int sz;

	sz = sizeof(struct sockaddr_in);
	if (family == AF_UNIX)
		sz = sizeof(struct sockaddr_un);
#if ENABLE_FEATURE_IPV6
	if (family == AF_INET6)
		sz = sizeof(struct sockaddr_in6);
#endif
	lsa = xzalloc(LSA_LEN_SIZE + sz);
	lsa->len = sz;
	lsa->u.sa.sa_family = family;
	return lsa;
}

static void rearm_alarm(void)
{
	if (!alarm_armed) {
		alarm_armed = 1;
		alarm(RETRYTIME);
	}
}

static void block_CHLD_HUP_ALRM(sigset_t *m)
{
	sigemptyset(m);
	sigaddset(m, SIGCHLD);
	sigaddset(m, SIGHUP);
	sigaddset(m, SIGALRM);
	sigprocmask(SIG_BLOCK, m, m); /* old sigmask is stored in m */
}

static void restore_sigmask(sigset_t *m)
{
	sigprocmask(SIG_SETMASK, m, NULL);
}

#if ENABLE_FEATURE_INETD_RPC
static void register_rpc(servtab_t *sep)
{
	int n;
	struct sockaddr_in ir_sin;

	if (bb_getsockname(sep->se_fd, (struct sockaddr *) &ir_sin, sizeof(ir_sin)) < 0) {
//TODO: verify that such failure is even possible in Linux kernel
		bb_perror_msg("getsockname");
		return;
	}

	for (n = sep->se_rpcver_lo; n <= sep->se_rpcver_hi; n++) {
		pmap_unset(sep->se_rpcprog, n);
		if (!pmap_set(sep->se_rpcprog, n, sep->se_proto_no, ntohs(ir_sin.sin_port)))
			bb_perror_msg("%s %s: pmap_set(%u,%u,%u,%u)",
				sep->se_service, sep->se_proto,
				sep->se_rpcprog, n, sep->se_proto_no, ntohs(ir_sin.sin_port));
	}
}

static void unregister_rpc(servtab_t *sep)
{
	int n;

	for (n = sep->se_rpcver_lo; n <= sep->se_rpcver_hi; n++) {
		if (!pmap_unset(sep->se_rpcprog, n))
			bb_perror_msg("pmap_unset(%u,%u)", sep->se_rpcprog, n);
	}
}
#endif /* FEATURE_INETD_RPC */

static void bump_nofile(void)
{
	enum { FD_CHUNK = 32 };
	struct rlimit rl;

	/* Never fails under Linux (except if you pass it bad arguments) */
	getrlimit(RLIMIT_NOFILE, &rl);
	rl.rlim_cur = MIN(rl.rlim_max, rl.rlim_cur + FD_CHUNK);
	rl.rlim_cur = MIN(FD_SETSIZE, rl.rlim_cur + FD_CHUNK);
	if (rl.rlim_cur <= rlim_ofile_cur) {
		bb_error_msg("can't extend file limit, max = %d",
						(int) rl.rlim_cur);
		return;
	}

	if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
		bb_perror_msg("setrlimit");
		return;
	}

	rlim_ofile_cur = rl.rlim_cur;
}

static void remove_fd_from_set(int fd)
{
	if (fd >= 0) {
		FD_CLR(fd, &allsock);
		dbg("stopped listening on fd:%d\n", fd);
		maxsock = -1;
		dbg("maxsock:%d\n", maxsock);
	}
}

static void add_fd_to_set(int fd)
{
	if (fd >= 0) {
		FD_SET(fd, &allsock);
		dbg("started listening on fd:%d\n", fd);
		if (maxsock >= 0 && fd > maxsock) {
			prev_maxsock = maxsock = fd;
			dbg("maxsock:%d\n", maxsock);
			if ((rlim_t)fd > rlim_ofile_cur - FD_MARGIN)
				bump_nofile();
		}
	}
}

static void recalculate_maxsock(void)
{
	int fd = 0;

	/* We may have no services, in this case maxsock should still be >= 0
	 * (code elsewhere is not happy with maxsock == -1) */
	maxsock = 0;
	while (fd <= prev_maxsock) {
		if (FD_ISSET(fd, &allsock))
			maxsock = fd;
		fd++;
	}
	dbg("recalculated maxsock:%d\n", maxsock);
	prev_maxsock = maxsock;
	if ((rlim_t)maxsock > rlim_ofile_cur - FD_MARGIN)
		bump_nofile();
}

static void prepare_socket_fd(servtab_t *sep)
{
	int r, fd;

	fd = socket(sep->se_family, sep->se_socktype, 0);
	if (fd < 0) {
		bb_perror_msg("socket");
		return;
	}
	setsockopt_reuseaddr(fd);

#if ENABLE_FEATURE_INETD_RPC
	if (is_rpc_service(sep)) {
		struct passwd *pwd;

		/* zero out the port for all RPC services; let bind()
		 * find one. */
		set_nport(&sep->se_lsa->u.sa, 0);

		/* for RPC services, attempt to use a reserved port
		 * if they are going to be running as root. */
		if (real_uid == 0 && sep->se_family == AF_INET
		 && (pwd = getpwnam(sep->se_user)) != NULL
		 && pwd->pw_uid == 0
		) {
			r = bindresvport(fd, &sep->se_lsa->u.sin);
		} else {
			r = bind(fd, &sep->se_lsa->u.sa, sep->se_lsa->len);
		}
		if (r == 0) {
			int saveerrno = errno;
			/* update lsa with port# */
			getsockname(fd, &sep->se_lsa->u.sa, &sep->se_lsa->len);
			errno = saveerrno;
		}
	} else
#endif
	{
		if (sep->se_family == AF_UNIX) {
			struct sockaddr_un *sun;
			sun = (struct sockaddr_un*)&(sep->se_lsa->u.sa);
			unlink(sun->sun_path);
		}
		r = bind(fd, &sep->se_lsa->u.sa, sep->se_lsa->len);
	}
	if (r < 0) {
		bb_perror_msg("%s/%s: bind",
				sep->se_service, sep->se_proto);
		close(fd);
		rearm_alarm();
		return;
	}

	if (sep->se_socktype == SOCK_STREAM) {
		listen(fd, global_queuelen);
		dbg("new sep->se_fd:%d (stream)\n", fd);
	} else {
		dbg("new sep->se_fd:%d (!stream)\n", fd);
	}

	add_fd_to_set(fd);
	sep->se_fd = fd;
}

static int reopen_config_file(void)
{
	free(default_local_hostname);
	default_local_hostname = xstrdup("*");
	if (parser != NULL)
		config_close(parser);
	parser = config_open(config_filename);
	return (parser != NULL);
}

static void close_config_file(void)
{
	if (parser) {
		config_close(parser);
		parser = NULL;
	}
}

static void free_servtab_strings(servtab_t *cp)
{
	int i;

	free(cp->se_local_hostname);
	free(cp->se_service);
	free(cp->se_proto);
	free(cp->se_user);
	free(cp->se_group);
	free(cp->se_lsa); /* not a string in fact */
	free(cp->se_program);
	for (i = 0; i < MAXARGV; i++)
		free(cp->se_argv[i]);
}

static servtab_t *new_servtab(void)
{
	servtab_t *newtab = xzalloc(sizeof(servtab_t));
	newtab->se_fd = -1; /* paranoia */
	return newtab;
}

static servtab_t *dup_servtab(servtab_t *sep)
{
	servtab_t *newtab;
	int argc;

	newtab = new_servtab();
	*newtab = *sep; /* struct copy */
	/* deep-copying strings */
	newtab->se_service = xstrdup(newtab->se_service);
	newtab->se_proto = xstrdup(newtab->se_proto);
	newtab->se_user = xstrdup(newtab->se_user);
	newtab->se_group = xstrdup(newtab->se_group);
	newtab->se_program = xstrdup(newtab->se_program);
	for (argc = 0; argc <= MAXARGV; argc++)
		newtab->se_argv[argc] = xstrdup(newtab->se_argv[argc]);
	/* NB: se_fd, se_hostaddr and se_next are always
	 * overwrittend by callers, so we don't bother resetting them
	 * to NULL/0/-1 etc */

	return newtab;
}

/* gcc generates much more code if this is inlined */
static NOINLINE servtab_t *parse_one_line(void)
{
	int argc;
	char *token[6+MAXARGV];
	char *p, *arg;
	char *hostdelim;
	servtab_t *sep;
	servtab_t *nsep;
 new:
	sep = new_servtab();
 more:
	argc = config_read(parser, token, 6+MAXARGV, 1, "# \t", PARSE_NORMAL);
	if (!argc) {
		free(sep);
		return NULL;
	}

	/* [host:]service socktype proto wait user[:group] prog [args] */
	/* Check for "host:...." line */
	arg = token[0];
	hostdelim = strrchr(arg, ':');
	if (hostdelim) {
		*hostdelim = '\0';
		sep->se_local_hostname = xstrdup(arg);
		arg = hostdelim + 1;
		if (*arg == '\0' && argc == 1) {
			/* Line has just "host:", change the
			 * default host for the following lines. */
			free(default_local_hostname);
			default_local_hostname = sep->se_local_hostname;
			/*sep->se_local_hostname = NULL; - redundant */
			/* (we'll overwrite this field anyway) */
			goto more;
		}
	} else
		sep->se_local_hostname = xstrdup(default_local_hostname);

	/* service socktype proto wait user[:group] prog [args] */
	sep->se_service = xstrdup(arg);

	/* socktype proto wait user[:group] prog [args] */
	if (argc < 6) {
 parse_err:
		bb_error_msg("parse error on line %u, line is ignored",
				parser->lineno);
		/* Just "goto more" can make sep to carry over e.g.
		 * "rpc"-ness (by having se_rpcver_lo != 0).
		 * We will be more paranoid: */
		free_servtab_strings(sep);
		free(sep);
		goto new;
	}

	{
		static const int8_t SOCK_xxx[] ALIGN1 = {
			-1,
			SOCK_STREAM, SOCK_DGRAM, SOCK_RDM,
			SOCK_SEQPACKET, SOCK_RAW
		};
		sep->se_socktype = SOCK_xxx[1 + index_in_strings(
			"stream""\0" "dgram""\0" "rdm""\0"
			"seqpacket""\0" "raw""\0"
			, token[1])];
	}

	/* {unix,[rpc/]{tcp,udp}[6]} wait user[:group] prog [args] */
	sep->se_proto = arg = xstrdup(token[2]);
	if (strcmp(arg, "unix") == 0) {
		sep->se_family = AF_UNIX;
	} else {
		char *six;
		sep->se_family = AF_INET;
		six = last_char_is(arg, '6');
		if (six) {
#if ENABLE_FEATURE_IPV6
			*six = '\0';
			sep->se_family = AF_INET6;
#else
			bb_error_msg("%s: no support for IPv6", sep->se_proto);
			goto parse_err;
#endif
		}
		if (is_prefixed_with(arg, "rpc/")) {
#if ENABLE_FEATURE_INETD_RPC
			unsigned n;
			arg += 4;
			p = strchr(sep->se_service, '/');
			if (p == NULL) {
				bb_error_msg("no rpc version: '%s'", sep->se_service);
				goto parse_err;
			}
			*p++ = '\0';
			n = bb_strtou(p, &p, 10);
			if (n > INT_MAX) {
 bad_ver_spec:
				bb_error_msg("bad rpc version");
				goto parse_err;
			}
			sep->se_rpcver_lo = sep->se_rpcver_hi = n;
			if (*p == '-') {
				p++;
				n = bb_strtou(p, &p, 10);
				if (n > INT_MAX || (int)n < sep->se_rpcver_lo)
					goto bad_ver_spec;
				sep->se_rpcver_hi = n;
			}
			if (*p != '\0')
				goto bad_ver_spec;
#else
			bb_error_msg("no support for rpc services");
			goto parse_err;
#endif
		}
		/* we don't really need getprotobyname()! */
		if (strcmp(arg, "tcp") == 0)
			sep->se_proto_no = IPPROTO_TCP; /* = 6 */
		if (strcmp(arg, "udp") == 0)
			sep->se_proto_no = IPPROTO_UDP; /* = 17 */
		if (six)
			*six = '6';
		if (!sep->se_proto_no) /* not tcp/udp?? */
			goto parse_err;
	}

	/* [no]wait[.max] user[:group] prog [args] */
	arg = token[3];
	sep->se_max = max_concurrency;
	p = strchr(arg, '.');
	if (p) {
		*p++ = '\0';
		sep->se_max = bb_strtou(p, NULL, 10);
		if (errno)
			goto parse_err;
	}
	sep->se_wait = (arg[0] != 'n' || arg[1] != 'o');
	if (!sep->se_wait) /* "no" seen */
		arg += 2;
	if (strcmp(arg, "wait") != 0)
		goto parse_err;

	/* user[:group] prog [args] */
	sep->se_user = xstrdup(token[4]);
	arg = strchr(sep->se_user, '.');
	if (arg == NULL)
		arg = strchr(sep->se_user, ':');
	if (arg) {
		*arg++ = '\0';
		sep->se_group = xstrdup(arg);
	}

	/* prog [args] */
	sep->se_program = xstrdup(token[5]);
#ifdef INETD_BUILTINS_ENABLED
	if (strcmp(sep->se_program, "internal") == 0
	 && strlen(sep->se_service) <= 7
	 && (sep->se_socktype == SOCK_STREAM
	     || sep->se_socktype == SOCK_DGRAM)
	) {
		unsigned i;
		for (i = 0; i < ARRAY_SIZE(builtins); i++)
			if (strncmp(builtins[i].bi_service7, sep->se_service, 7) == 0)
				goto found_bi;
		bb_error_msg("unknown internal service %s", sep->se_service);
		goto parse_err;
 found_bi:
		sep->se_builtin = &builtins[i];
		/* stream builtins must be "nowait", dgram must be "wait" */
		if (sep->se_wait != (sep->se_socktype == SOCK_DGRAM))
			goto parse_err;
	}
#endif
	argc = 0;
	while (argc < MAXARGV && (arg = token[6+argc]) != NULL)
		sep->se_argv[argc++] = xstrdup(arg);
	/* Some inetd.conf files have no argv's, not even argv[0].
	 * Fix them up.
	 * (Technically, programs can be execed with argv[0] = NULL,
	 * but many programs do not like that at all) */
	if (argc == 0)
		sep->se_argv[0] = xstrdup(sep->se_program);

	/* catch mixups. "<service> stream udp ..." == wtf */
	if (sep->se_socktype == SOCK_STREAM) {
		if (sep->se_proto_no == IPPROTO_UDP)
			goto parse_err;
	}
	if (sep->se_socktype == SOCK_DGRAM) {
		if (sep->se_proto_no == IPPROTO_TCP)
			goto parse_err;
	}

	//bb_error_msg(
	//	"ENTRY[%s][%s][%s][%d][%d][%d][%d][%d][%s][%s][%s]",
	//	sep->se_local_hostname, sep->se_service, sep->se_proto, sep->se_wait, sep->se_proto_no,
	//	sep->se_max, sep->se_count, sep->se_time, sep->se_user, sep->se_group, sep->se_program);

	/* check if the hostname specifier is a comma separated list
	 * of hostnames. we'll make new entries for each address. */
	while ((hostdelim = strrchr(sep->se_local_hostname, ',')) != NULL) {
		nsep = dup_servtab(sep);
		/* NUL terminate the hostname field of the existing entry,
		 * and make a dup for the new entry. */
		*hostdelim++ = '\0';
		nsep->se_local_hostname = xstrdup(hostdelim);
		nsep->se_next = sep->se_next;
		sep->se_next = nsep;
	}

	/* was doing it here: */
	/* DNS resolution, create copies for each IP address */
	/* IPv6-ization destroyed it :( */

	return sep;
}

static servtab_t *insert_in_servlist(servtab_t *cp)
{
	servtab_t *sep;
	sigset_t omask;

	sep = new_servtab();
	*sep = *cp; /* struct copy */
	sep->se_fd = -1;
#if ENABLE_FEATURE_INETD_RPC
	sep->se_rpcprog = -1;
#endif
	block_CHLD_HUP_ALRM(&omask);
	sep->se_next = serv_list;
	serv_list = sep;
	restore_sigmask(&omask);
	return sep;
}

static int same_serv_addr_proto(servtab_t *old, servtab_t *new)
{
	if (strcmp(old->se_local_hostname, new->se_local_hostname) != 0)
		return 0;
	if (strcmp(old->se_service, new->se_service) != 0)
		return 0;
	if (strcmp(old->se_proto, new->se_proto) != 0)
		return 0;
	return 1;
}

static void reread_config_file(int sig UNUSED_PARAM)
{
	servtab_t *sep, *cp, **sepp;
	len_and_sockaddr *lsa;
	sigset_t omask;
	unsigned n;
	uint16_t port;
	int save_errno = errno;

	if (!reopen_config_file())
		goto ret;
	for (sep = serv_list; sep; sep = sep->se_next)
		sep->se_checked = 0;

	goto first_line;
	while (1) {
		if (cp == NULL) {
 first_line:
			cp = parse_one_line();
			if (cp == NULL)
				break;
		}
		for (sep = serv_list; sep; sep = sep->se_next)
			if (same_serv_addr_proto(sep, cp))
				goto equal_servtab;
		/* not an "equal" servtab */
		sep = insert_in_servlist(cp);
		goto after_check;
 equal_servtab:
		{
			int i;

			block_CHLD_HUP_ALRM(&omask);
#if ENABLE_FEATURE_INETD_RPC
			if (is_rpc_service(sep))
				unregister_rpc(sep);
			sep->se_rpcver_lo = cp->se_rpcver_lo;
			sep->se_rpcver_hi = cp->se_rpcver_hi;
#endif
			if (cp->se_wait == 0) {
				/* New config says "nowait". If old one
				 * was "wait", we currently may be waiting
				 * for a child (and not accepting connects).
				 * Stop waiting, start listening again.
				 * (if it's not true, this op is harmless) */
				add_fd_to_set(sep->se_fd);
			}
			sep->se_wait = cp->se_wait;
			sep->se_max = cp->se_max;
			/* string fields need more love - we don't want to leak them */
#define SWAP(type, a, b) do { type c = (type)a; a = (type)b; b = (type)c; } while (0)
			SWAP(char*, sep->se_user, cp->se_user);
			SWAP(char*, sep->se_group, cp->se_group);
			SWAP(char*, sep->se_program, cp->se_program);
			for (i = 0; i < MAXARGV; i++)
				SWAP(char*, sep->se_argv[i], cp->se_argv[i]);
#undef SWAP
			restore_sigmask(&omask);
			free_servtab_strings(cp);
		}
 after_check:
		/* cp->string_fields are consumed by insert_in_servlist()
		 * or freed at this point, cp itself is not yet freed. */
		sep->se_checked = 1;

		/* create new len_and_sockaddr */
		switch (sep->se_family) {
			struct sockaddr_un *sun;
		case AF_UNIX:
			lsa = xzalloc_lsa(AF_UNIX);
			sun = (struct sockaddr_un*)&lsa->u.sa;
			safe_strncpy(sun->sun_path, sep->se_service, sizeof(sun->sun_path));
			break;

		default: /* case AF_INET, case AF_INET6 */
			n = bb_strtou(sep->se_service, NULL, 10);
#if ENABLE_FEATURE_INETD_RPC
			if (is_rpc_service(sep)) {
				sep->se_rpcprog = n;
				if (errno) { /* se_service is not numeric */
					struct rpcent *rp = getrpcbyname(sep->se_service);
					if (rp == NULL) {
						bb_error_msg("%s: unknown rpc service", sep->se_service);
						goto next_cp;
					}
					sep->se_rpcprog = rp->r_number;
				}
				if (sep->se_fd == -1)
					prepare_socket_fd(sep);
				if (sep->se_fd != -1)
					register_rpc(sep);
				goto next_cp;
			}
#endif
			/* what port to listen on? */
			port = htons(n);
			if (errno || n > 0xffff) { /* se_service is not numeric */
				char protoname[4];
				struct servent *sp;
				/* can result only in "tcp" or "udp": */
				safe_strncpy(protoname, sep->se_proto, 4);
				sp = getservbyname(sep->se_service, protoname);
				if (sp == NULL) {
					bb_error_msg("%s/%s: unknown service",
							sep->se_service, sep->se_proto);
					goto next_cp;
				}
				port = sp->s_port;
			}
			if (LONE_CHAR(sep->se_local_hostname, '*')) {
				lsa = xzalloc_lsa(sep->se_family);
				set_nport(&lsa->u.sa, port);
			} else {
				lsa = host_and_af2sockaddr(sep->se_local_hostname,
						ntohs(port), sep->se_family);
				if (!lsa) {
					bb_error_msg("%s/%s: unknown host '%s'",
						sep->se_service, sep->se_proto,
						sep->se_local_hostname);
					goto next_cp;
				}
			}
			break;
		} /* end of "switch (sep->se_family)" */

		/* did lsa change? Then close/open */
		if (sep->se_lsa == NULL
		 || lsa->len != sep->se_lsa->len
		 || memcmp(&lsa->u.sa, &sep->se_lsa->u.sa, lsa->len) != 0
		) {
			remove_fd_from_set(sep->se_fd);
			maybe_close(sep->se_fd);
			free(sep->se_lsa);
			sep->se_lsa = lsa;
			sep->se_fd = -1;
		} else {
			free(lsa);
		}
		if (sep->se_fd == -1)
			prepare_socket_fd(sep);
 next_cp:
		sep = cp->se_next;
		free(cp);
		cp = sep;
	} /* end of "while (1) parse lines" */
	close_config_file();

	/* Purge anything not looked at above - these are stale entries,
	 * new config file doesnt have them. */
	block_CHLD_HUP_ALRM(&omask);
	sepp = &serv_list;
	while ((sep = *sepp) != NULL) {
		if (sep->se_checked) {
			sepp = &sep->se_next;
			continue;
		}
		*sepp = sep->se_next;
		remove_fd_from_set(sep->se_fd);
		maybe_close(sep->se_fd);
#if ENABLE_FEATURE_INETD_RPC
		if (is_rpc_service(sep))
			unregister_rpc(sep);
#endif
		if (sep->se_family == AF_UNIX)
			unlink(sep->se_service);
		free_servtab_strings(sep);
		free(sep);
	}
	restore_sigmask(&omask);
 ret:
	errno = save_errno;
}

static void reap_child(int sig UNUSED_PARAM)
{
	pid_t pid;
	int status;
	servtab_t *sep;
	int save_errno = errno;

	for (;;) {
		pid = wait_any_nohang(&status);
		if (pid <= 0)
			break;
		for (sep = serv_list; sep; sep = sep->se_next) {
			if (sep->se_wait != pid)
				continue;
			/* One of our "wait" services */
			if (WIFEXITED(status) && WEXITSTATUS(status))
				bb_error_msg("%s: exit status %u",
						sep->se_program, WEXITSTATUS(status));
			else if (WIFSIGNALED(status))
				bb_error_msg("%s: exit signal %u",
						sep->se_program, WTERMSIG(status));
			sep->se_wait = 1;
			add_fd_to_set(sep->se_fd);
			break;
		}
	}
	errno = save_errno;
}

static void retry_network_setup(int sig UNUSED_PARAM)
{
	int save_errno = errno;
	servtab_t *sep;

	alarm_armed = 0;
	for (sep = serv_list; sep; sep = sep->se_next) {
		if (sep->se_fd == -1) {
			prepare_socket_fd(sep);
#if ENABLE_FEATURE_INETD_RPC
			if (sep->se_fd != -1 && is_rpc_service(sep))
				register_rpc(sep);
#endif
		}
	}
	errno = save_errno;
}

static void clean_up_and_exit(int sig UNUSED_PARAM)
{
	servtab_t *sep;

	/* XXX signal race walking sep list */
	for (sep = serv_list; sep; sep = sep->se_next) {
		if (sep->se_fd == -1)
			continue;

		switch (sep->se_family) {
		case AF_UNIX:
			unlink(sep->se_service);
			break;
		default: /* case AF_INET, AF_INET6 */
#if ENABLE_FEATURE_INETD_RPC
			if (sep->se_wait == 1 && is_rpc_service(sep))
				unregister_rpc(sep);   /* XXX signal race */
#endif
			break;
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			close(sep->se_fd);
	}
	remove_pidfile(CONFIG_PID_FILE_PATH "/inetd.pid");
	exit(EXIT_SUCCESS);
}

int inetd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int inetd_main(int argc UNUSED_PARAM, char **argv)
{
	struct sigaction sa, saved_pipe_handler;
	servtab_t *sep, *sep2;
	struct passwd *pwd;
	struct group *grp = grp; /* for compiler */
	int opt;
	pid_t pid;
	sigset_t omask;

	INIT_G();

	real_uid = getuid();
	if (real_uid != 0) /* run by non-root user */
		config_filename = NULL;

	/* -q N, -R N */
	opt = getopt32(argv, "R:+feq:+", &max_concurrency, &global_queuelen);
	argv += optind;
	//argc -= optind;
	if (argv[0])
		config_filename = argv[0];
	if (config_filename == NULL)
		bb_error_msg_and_die("non-root must specify config file");
	if (!(opt & 2))
		bb_daemonize_or_rexec(0, argv - optind);
	else
		bb_sanitize_stdio();
	if (!(opt & 4)) {
		/* LOG_NDELAY: connect to syslog daemon NOW.
		 * Otherwise, we may open syslog socket
		 * in vforked child, making opened fds and syslog()
		 * internal state inconsistent.
		 * This was observed to leak file descriptors. */
		openlog(applet_name, LOG_PID | LOG_NDELAY, LOG_DAEMON);
		logmode = LOGMODE_SYSLOG;
	}

	if (real_uid == 0) {
		/* run by root, ensure groups vector gets trashed */
		gid_t gid = getgid();
		setgroups(1, &gid);
	}

	write_pidfile(CONFIG_PID_FILE_PATH "/inetd.pid");

	/* never fails under Linux (except if you pass it bad arguments) */
	getrlimit(RLIMIT_NOFILE, &rlim_ofile);
	rlim_ofile_cur = rlim_ofile.rlim_cur;
	if (rlim_ofile_cur == RLIM_INFINITY)    /* ! */
		rlim_ofile_cur = OPEN_MAX;

	memset(&sa, 0, sizeof(sa));
	/*sigemptyset(&sa.sa_mask); - memset did it */
	sigaddset(&sa.sa_mask, SIGALRM);
	sigaddset(&sa.sa_mask, SIGCHLD);
	sigaddset(&sa.sa_mask, SIGHUP);
//FIXME: explain why no SA_RESTART
//FIXME: retry_network_setup is unsafe to run in signal handler (many reasons)!
	sa.sa_handler = retry_network_setup;
	sigaction_set(SIGALRM, &sa);
//FIXME: reread_config_file is unsafe to run in signal handler(many reasons)!
	sa.sa_handler = reread_config_file;
	sigaction_set(SIGHUP, &sa);
//FIXME: reap_child is unsafe to run in signal handler (uses stdio)!
	sa.sa_handler = reap_child;
	sigaction_set(SIGCHLD, &sa);
//FIXME: clean_up_and_exit is unsafe to run in signal handler (uses stdio)!
	sa.sa_handler = clean_up_and_exit;
	sigaction_set(SIGTERM, &sa);
	sa.sa_handler = clean_up_and_exit;
	sigaction_set(SIGINT, &sa);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, &saved_pipe_handler);

	reread_config_file(SIGHUP); /* load config from file */

	for (;;) {
		int ready_fd_cnt;
		int ctrl, accepted_fd, new_udp_fd;
		fd_set readable;

		if (maxsock < 0)
			recalculate_maxsock();

		readable = allsock; /* struct copy */
		/* if there are no fds to wait on, we will block
		 * until signal wakes us up (maxsock == 0, but readable
		 * never contains fds 0 and 1...) */
		ready_fd_cnt = select(maxsock + 1, &readable, NULL, NULL, NULL);
		if (ready_fd_cnt < 0) {
			if (errno != EINTR) {
				bb_perror_msg("select");
				sleep(1);
			}
			continue;
		}
		dbg("ready_fd_cnt:%d\n", ready_fd_cnt);

		for (sep = serv_list; ready_fd_cnt && sep; sep = sep->se_next) {
			if (sep->se_fd == -1 || !FD_ISSET(sep->se_fd, &readable))
				continue;

			dbg("ready fd:%d\n", sep->se_fd);
			ready_fd_cnt--;
			ctrl = sep->se_fd;
			accepted_fd = -1;
			new_udp_fd = -1;
			if (!sep->se_wait) {
				if (sep->se_socktype == SOCK_STREAM) {
					ctrl = accepted_fd = accept(sep->se_fd, NULL, NULL);
					dbg("accepted_fd:%d\n", accepted_fd);
					if (ctrl < 0) {
						if (errno != EINTR)
							bb_perror_msg("accept (for %s)", sep->se_service);
						continue;
					}
				}
				/* "nowait" udp */
				if (sep->se_socktype == SOCK_DGRAM
				 && sep->se_family != AF_UNIX
				) {
/* How udp "nowait" works:
 * child peeks at (received and buffered by kernel) UDP packet,
 * performs connect() on the socket so that it is linked only
 * to this peer. But this also affects parent, because descriptors
 * are shared after fork() a-la dup(). When parent performs
 * select(), it will see this descriptor connected to the peer (!)
 * and still readable, will act on it and mess things up
 * (can create many copies of same child, etc).
 * Parent must create and use new socket instead. */
					new_udp_fd = socket(sep->se_family, SOCK_DGRAM, 0);
					dbg("new_udp_fd:%d\n", new_udp_fd);
					if (new_udp_fd < 0) { /* error: eat packet, forget about it */
 udp_err:
						recv(sep->se_fd, line, LINE_SIZE, MSG_DONTWAIT);
						continue;
					}
					setsockopt_reuseaddr(new_udp_fd);
					/* TODO: better do bind after fork in parent,
					 * so that we don't have two wildcard bound sockets
					 * even for a brief moment? */
					if (bind(new_udp_fd, &sep->se_lsa->u.sa, sep->se_lsa->len) < 0) {
						dbg("bind(new_udp_fd) failed\n");
						close(new_udp_fd);
						goto udp_err;
					}
					dbg("bind(new_udp_fd) succeeded\n");
				}
			}

			block_CHLD_HUP_ALRM(&omask);
			pid = 0;
#ifdef INETD_BUILTINS_ENABLED
			/* do we need to fork? */
			if (sep->se_builtin == NULL
			 || (sep->se_socktype == SOCK_STREAM
			     && sep->se_builtin->bi_fork))
#endif
			{
				if (sep->se_max != 0) {
					if (++sep->se_count == 1)
						sep->se_time = monotonic_sec();
					else if (sep->se_count >= sep->se_max) {
						unsigned now = monotonic_sec();
						/* did we accumulate se_max connects too quickly? */
						if (now - sep->se_time <= CNT_INTERVAL) {
							bb_error_msg("%s/%s: too many connections, pausing",
									sep->se_service, sep->se_proto);
							remove_fd_from_set(sep->se_fd);
							close(sep->se_fd);
							sep->se_fd = -1;
							sep->se_count = 0;
							rearm_alarm(); /* will revive it in RETRYTIME sec */
							restore_sigmask(&omask);
							maybe_close(new_udp_fd);
							maybe_close(accepted_fd);
							continue; /* -> check next fd in fd set */
						}
						sep->se_count = 0;
					}
				}
				/* on NOMMU, streamed chargen
				 * builtin wouldn't work, but it is
				 * not allowed on NOMMU (ifdefed out) */
#ifdef INETD_BUILTINS_ENABLED
				if (BB_MMU && sep->se_builtin)
					pid = fork();
				else
#endif
					pid = vfork();

				if (pid < 0) { /* fork error */
					bb_perror_msg("vfork"+1);
					sleep(1);
					restore_sigmask(&omask);
					maybe_close(new_udp_fd);
					maybe_close(accepted_fd);
					continue; /* -> check next fd in fd set */
				}
				if (pid == 0)
					pid--; /* -1: "we did fork and we are child" */
			}
			/* if pid == 0 here, we didn't fork */

			if (pid > 0) { /* parent */
				if (sep->se_wait) {
					/* wait: we passed socket to child,
					 * will wait for child to terminate */
					sep->se_wait = pid;
					remove_fd_from_set(sep->se_fd);
				}
				if (new_udp_fd >= 0) {
					/* udp nowait: child connected the socket,
					 * we created and will use new, unconnected one */
					xmove_fd(new_udp_fd, sep->se_fd);
					dbg("moved new_udp_fd:%d to sep->se_fd:%d\n", new_udp_fd, sep->se_fd);
				}
				restore_sigmask(&omask);
				maybe_close(accepted_fd);
				continue; /* -> check next fd in fd set */
			}

			/* we are either child or didn't fork at all */
#ifdef INETD_BUILTINS_ENABLED
			if (sep->se_builtin) {
				if (pid) { /* "pid" is -1: we did fork */
					close(sep->se_fd); /* listening socket */
					dbg("closed sep->se_fd:%d\n", sep->se_fd);
					logmode = LOGMODE_NONE; /* make xwrite etc silent */
				}
				restore_sigmask(&omask);
				if (sep->se_socktype == SOCK_STREAM)
					sep->se_builtin->bi_stream_fn(ctrl, sep);
				else
					sep->se_builtin->bi_dgram_fn(ctrl, sep);
				if (pid) /* we did fork */
					_exit(EXIT_FAILURE);
				maybe_close(accepted_fd);
				continue; /* -> check next fd in fd set */
			}
#endif
			/* child */
			setsid();
			/* "nowait" udp */
			if (new_udp_fd >= 0) {
				len_and_sockaddr *lsa;
				int r;

				close(new_udp_fd);
				dbg("closed new_udp_fd:%d\n", new_udp_fd);
				lsa = xzalloc_lsa(sep->se_family);
				/* peek at the packet and remember peer addr */
				r = recvfrom(ctrl, NULL, 0, MSG_PEEK|MSG_DONTWAIT,
					&lsa->u.sa, &lsa->len);
				if (r < 0)
					goto do_exit1;
				/* make this socket "connected" to peer addr:
				 * only packets from this peer will be recv'ed,
				 * and bare write()/send() will work on it */
				connect(ctrl, &lsa->u.sa, lsa->len);
				dbg("connected ctrl:%d to remote peer\n", ctrl);
				free(lsa);
			}
			/* prepare env and exec program */
			pwd = getpwnam(sep->se_user);
			if (pwd == NULL) {
				bb_error_msg("%s: no such %s", sep->se_user, "user");
				goto do_exit1;
			}
			if (sep->se_group && (grp = getgrnam(sep->se_group)) == NULL) {
				bb_error_msg("%s: no such %s", sep->se_group, "group");
				goto do_exit1;
			}
			if (real_uid != 0 && real_uid != pwd->pw_uid) {
				/* a user running private inetd */
				bb_error_msg("non-root must run services as himself");
				goto do_exit1;
			}
			if (pwd->pw_uid != real_uid) {
				if (sep->se_group)
					pwd->pw_gid = grp->gr_gid;
				/* initgroups, setgid, setuid: */
				change_identity(pwd);
			} else if (sep->se_group) {
				xsetgid(grp->gr_gid);
				setgroups(1, &grp->gr_gid);
			}
			if (rlim_ofile.rlim_cur != rlim_ofile_cur)
				if (setrlimit(RLIMIT_NOFILE, &rlim_ofile) < 0)
					bb_perror_msg("setrlimit");

			/* closelog(); - WRONG. we are after vfork,
			 * this may confuse syslog() internal state.
			 * Let's hope libc sets syslog fd to CLOEXEC...
			 */
			xmove_fd(ctrl, STDIN_FILENO);
			xdup2(STDIN_FILENO, STDOUT_FILENO);
			dbg("moved ctrl:%d to fd 0,1[,2]\n", ctrl);
			/* manpages of inetd I managed to find either say
			 * that stderr is also redirected to the network,
			 * or do not talk about redirection at all (!) */
			if (!sep->se_wait) /* only for usual "tcp nowait" */
				xdup2(STDIN_FILENO, STDERR_FILENO);
			/* NB: among others, this loop closes listening sockets
			 * for nowait stream children */
			for (sep2 = serv_list; sep2; sep2 = sep2->se_next)
				if (sep2->se_fd != ctrl)
					maybe_close(sep2->se_fd);
			sigaction_set(SIGPIPE, &saved_pipe_handler);
			restore_sigmask(&omask);
			dbg("execing:'%s'\n", sep->se_program);
			BB_EXECVP(sep->se_program, sep->se_argv);
			bb_perror_msg("can't execute '%s'", sep->se_program);
 do_exit1:
			/* eat packet in udp case */
			if (sep->se_socktype != SOCK_STREAM)
				recv(0, line, LINE_SIZE, MSG_DONTWAIT);
			_exit(EXIT_FAILURE);
		} /* for (sep = servtab...) */
	} /* for (;;) */
}

#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_ECHO \
 || ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DISCARD
# if !BB_MMU
static const char *const cat_args[] = { "cat", NULL };
# endif
#endif

/*
 * Internet services provided internally by inetd:
 */
#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_ECHO
/* Echo service -- echo data back. */
/* ARGSUSED */
static void FAST_FUNC echo_stream(int s, servtab_t *sep UNUSED_PARAM)
{
# if BB_MMU
	while (1) {
		ssize_t sz = safe_read(s, line, LINE_SIZE);
		if (sz <= 0)
			break;
		xwrite(s, line, sz);
	}
# else
	/* We are after vfork here! */
	/* move network socket to stdin/stdout */
	xmove_fd(s, STDIN_FILENO);
	xdup2(STDIN_FILENO, STDOUT_FILENO);
	/* no error messages please... */
	close(STDERR_FILENO);
	xopen(bb_dev_null, O_WRONLY);
	BB_EXECVP("cat", (char**)cat_args);
	/* on failure we return to main, which does exit(EXIT_FAILURE) */
# endif
}
static void FAST_FUNC echo_dg(int s, servtab_t *sep)
{
	enum { BUFSIZE = 12*1024 }; /* for jumbo sized packets! :) */
	char *buf = xmalloc(BUFSIZE); /* too big for stack */
	int sz;
	len_and_sockaddr *lsa = alloca(LSA_LEN_SIZE + sep->se_lsa->len);

	lsa->len = sep->se_lsa->len;
	/* dgram builtins are non-forking - DONT BLOCK! */
	sz = recvfrom(s, buf, BUFSIZE, MSG_DONTWAIT, &lsa->u.sa, &lsa->len);
	if (sz > 0)
		sendto(s, buf, sz, 0, &lsa->u.sa, lsa->len);
	free(buf);
}
#endif  /* FEATURE_INETD_SUPPORT_BUILTIN_ECHO */


#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DISCARD
/* Discard service -- ignore data. */
/* ARGSUSED */
static void FAST_FUNC discard_stream(int s, servtab_t *sep UNUSED_PARAM)
{
# if BB_MMU
	while (safe_read(s, line, LINE_SIZE) > 0)
		continue;
# else
	/* We are after vfork here! */
	/* move network socket to stdin */
	xmove_fd(s, STDIN_FILENO);
	/* discard output */
	close(STDOUT_FILENO);
	xopen(bb_dev_null, O_WRONLY);
	/* no error messages please... */
	xdup2(STDOUT_FILENO, STDERR_FILENO);
	BB_EXECVP("cat", (char**)cat_args);
	/* on failure we return to main, which does exit(EXIT_FAILURE) */
# endif
}
/* ARGSUSED */
static void FAST_FUNC discard_dg(int s, servtab_t *sep UNUSED_PARAM)
{
	/* dgram builtins are non-forking - DONT BLOCK! */
	recv(s, line, LINE_SIZE, MSG_DONTWAIT);
}
#endif /* FEATURE_INETD_SUPPORT_BUILTIN_DISCARD */


#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN
#define LINESIZ 72
static void init_ring(void)
{
	int i;

	end_ring = ring;
	for (i = ' '; i < 127; i++)
		*end_ring++ = i;
}
/* Character generator. MMU arches only. */
/* ARGSUSED */
static void FAST_FUNC chargen_stream(int s, servtab_t *sep UNUSED_PARAM)
{
	char *rs;
	int len;
	char text[LINESIZ + 2];

	if (!end_ring) {
		init_ring();
		rs = ring;
	}

	text[LINESIZ] = '\r';
	text[LINESIZ + 1] = '\n';
	rs = ring;
	for (;;) {
		len = end_ring - rs;
		if (len >= LINESIZ)
			memmove(text, rs, LINESIZ);
		else {
			memmove(text, rs, len);
			memmove(text + len, ring, LINESIZ - len);
		}
		if (++rs == end_ring)
			rs = ring;
		xwrite(s, text, sizeof(text));
	}
}
/* ARGSUSED */
static void FAST_FUNC chargen_dg(int s, servtab_t *sep)
{
	int len;
	char text[LINESIZ + 2];
	len_and_sockaddr *lsa = alloca(LSA_LEN_SIZE + sep->se_lsa->len);

	/* Eat UDP packet which started it all */
	/* dgram builtins are non-forking - DONT BLOCK! */
	lsa->len = sep->se_lsa->len;
	if (recvfrom(s, text, sizeof(text), MSG_DONTWAIT, &lsa->u.sa, &lsa->len) < 0)
		return;

	if (!end_ring) {
		init_ring();
		ring_pos = ring;
	}

	len = end_ring - ring_pos;
	if (len >= LINESIZ)
		memmove(text, ring_pos, LINESIZ);
	else {
		memmove(text, ring_pos, len);
		memmove(text + len, ring, LINESIZ - len);
	}
	if (++ring_pos == end_ring)
		ring_pos = ring;
	text[LINESIZ] = '\r';
	text[LINESIZ + 1] = '\n';
	sendto(s, text, sizeof(text), 0, &lsa->u.sa, lsa->len);
}
#endif /* FEATURE_INETD_SUPPORT_BUILTIN_CHARGEN */


#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_TIME
/*
 * Return a machine readable date and time, in the form of the
 * number of seconds since midnight, Jan 1, 1900.  Since gettimeofday
 * returns the number of seconds since midnight, Jan 1, 1970,
 * we must add 2208988800 seconds to this figure to make up for
 * some seventy years Bell Labs was asleep.
 */
static uint32_t machtime(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return htonl((uint32_t)(tv.tv_sec + 2208988800U));
}
/* ARGSUSED */
static void FAST_FUNC machtime_stream(int s, servtab_t *sep UNUSED_PARAM)
{
	uint32_t result;

	result = machtime();
	full_write(s, &result, sizeof(result));
}
static void FAST_FUNC machtime_dg(int s, servtab_t *sep)
{
	uint32_t result;
	len_and_sockaddr *lsa = alloca(LSA_LEN_SIZE + sep->se_lsa->len);

	lsa->len = sep->se_lsa->len;
	if (recvfrom(s, line, LINE_SIZE, MSG_DONTWAIT, &lsa->u.sa, &lsa->len) < 0)
		return;

	result = machtime();
	sendto(s, &result, sizeof(result), 0, &lsa->u.sa, lsa->len);
}
#endif /* FEATURE_INETD_SUPPORT_BUILTIN_TIME */


#if ENABLE_FEATURE_INETD_SUPPORT_BUILTIN_DAYTIME
/* Return human-readable time of day */
/* ARGSUSED */
static void FAST_FUNC daytime_stream(int s, servtab_t *sep UNUSED_PARAM)
{
	time_t t;

	time(&t);
	fdprintf(s, "%.24s\r\n", ctime(&t));
}
static void FAST_FUNC daytime_dg(int s, servtab_t *sep)
{
	time_t t;
	len_and_sockaddr *lsa = alloca(LSA_LEN_SIZE + sep->se_lsa->len);

	lsa->len = sep->se_lsa->len;
	if (recvfrom(s, line, LINE_SIZE, MSG_DONTWAIT, &lsa->u.sa, &lsa->len) < 0)
		return;

	t = time(NULL);
	sprintf(line, "%.24s\r\n", ctime(&t));
	sendto(s, line, strlen(line), 0, &lsa->u.sa, lsa->len);
}
#endif /* FEATURE_INETD_SUPPORT_BUILTIN_DAYTIME */
