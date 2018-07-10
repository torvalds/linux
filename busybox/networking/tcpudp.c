/* Based on ipsvd utilities written by Gerrit Pape <pape@smarden.org>
 * which are released into public domain by the author.
 * Homepage: http://smarden.sunsite.dk/ipsvd/
 *
 * Copyright (C) 2007 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* Based on ipsvd-0.12.1. This tcpsvd accepts all options
 * which are supported by one from ipsvd-0.12.1, but not all are
 * functional. See help text at the end of this file for details.
 *
 * Code inside "#ifdef SSLSVD" is for sslsvd and is currently unused.
 *
 * Busybox version exports TCPLOCALADDR instead of
 * TCPLOCALIP + TCPLOCALPORT pair. ADDR more closely matches reality
 * (which is "struct sockaddr_XXX". Port is not a separate entity,
 * it's just a part of (AF_INET[6]) sockaddr!).
 *
 * TCPORIGDSTADDR is Busybox-specific addition.
 *
 * udp server is hacked up by reusing TCP code. It has the following
 * limitation inherent in Unix DGRAM sockets implementation:
 * - local IP address is retrieved (using recvmsg voodoo) but
 *   child's socket is not bound to it (bind cannot be called on
 *   already bound socket). Thus it still can emit outgoing packets
 *   with wrong source IP...
 * - don't know how to retrieve ORIGDST for udp.
 */
//config:config TCPSVD
//config:	bool "tcpsvd (13 kb)"
//config:	default y
//config:	help
//config:	tcpsvd listens on a TCP port and runs a program for each new
//config:	connection.
//config:
//config:config UDPSVD
//config:	bool "udpsvd (13 kb)"
//config:	default y
//config:	help
//config:	udpsvd listens on an UDP port and runs a program for each new
//config:	connection.

//applet:IF_TCPSVD(APPLET_ODDNAME(tcpsvd, tcpudpsvd, BB_DIR_USR_BIN, BB_SUID_DROP, tcpsvd))
//applet:IF_UDPSVD(APPLET_ODDNAME(udpsvd, tcpudpsvd, BB_DIR_USR_BIN, BB_SUID_DROP, udpsvd))

//kbuild:lib-$(CONFIG_TCPSVD) += tcpudp.o tcpudp_perhost.o
//kbuild:lib-$(CONFIG_UDPSVD) += tcpudp.o tcpudp_perhost.o

//usage:#define tcpsvd_trivial_usage
//usage:       "[-hEv] [-c N] [-C N[:MSG]] [-b N] [-u USER] [-l NAME] IP PORT PROG"
/* with not-implemented options: */
/* //usage:    "[-hpEvv] [-c N] [-C N[:MSG]] [-b N] [-u USER] [-l NAME] [-i DIR|-x CDB] [-t SEC] IP PORT PROG" */
//usage:#define tcpsvd_full_usage "\n\n"
//usage:       "Create TCP socket, bind to IP:PORT and listen for incoming connections.\n"
//usage:       "Run PROG for each connection.\n"
//usage:     "\n	IP PORT		IP:PORT to listen on"
//usage:     "\n	PROG ARGS	Program to run"
//usage:     "\n	-u USER[:GRP]	Change to user/group after bind"
//usage:     "\n	-c N		Up to N connections simultaneously (default 30)"
//usage:     "\n	-b N		Allow backlog of approximately N TCP SYNs (default 20)"
//usage:     "\n	-C N[:MSG]	Allow only up to N connections from the same IP:"
//usage:     "\n			new connections from this IP address are closed"
//usage:     "\n			immediately, MSG is written to the peer before close"
//usage:     "\n	-E		Don't set up environment"
//usage:     "\n	-h		Look up peer's hostname"
//usage:     "\n	-l NAME		Local hostname (else look up local hostname in DNS)"
//usage:     "\n	-v		Verbose"
//usage:     "\n"
//usage:     "\nEnvironment if no -E:"
//usage:     "\nPROTO='TCP'"
//usage:     "\nTCPREMOTEADDR='ip:port'" IF_FEATURE_IPV6(" ('[ip]:port' for IPv6)")
//usage:     "\nTCPLOCALADDR='ip:port'"
//usage:     "\nTCPORIGDSTADDR='ip:port' of destination before firewall"
//usage:     "\n	Useful for REDIRECTed-to-local connections:"
//usage:     "\n	iptables -t nat -A PREROUTING -p tcp --dport 80 -j REDIRECT --to 8080"
//usage:     "\nTCPCONCURRENCY=num_of_connects_from_this_ip"
//usage:     "\nIf -h:"
//usage:     "\nTCPLOCALHOST='hostname' (-l NAME is used if specified)"
//usage:     "\nTCPREMOTEHOST='hostname'"

//usage:
//usage:#define udpsvd_trivial_usage
//usage:       "[-hEv] [-c N] [-u USER] [-l NAME] IP PORT PROG"
//usage:#define udpsvd_full_usage "\n\n"
//usage:       "Create UDP socket, bind to IP:PORT and wait for incoming packets.\n"
//usage:       "Run PROG for each packet, redirecting all further packets with same\n"
//usage:       "peer ip:port to it.\n"
//usage:     "\n	IP PORT		IP:PORT to listen on"
//usage:     "\n	PROG ARGS	Program to run"
//usage:     "\n	-u USER[:GRP]	Change to user/group after bind"
//usage:     "\n	-c N		Up to N connections simultaneously (default 30)"
//usage:     "\n	-E		Don't set up environment"
//usage:     "\n	-h		Look up peer's hostname"
//usage:     "\n	-l NAME		Local hostname (else look up local hostname in DNS)"
//usage:     "\n	-v		Verbose"
//usage:     "\n"
//usage:     "\nEnvironment if no -E:"
//usage:     "\nPROTO='UDP'"
//usage:     "\nUDPREMOTEADDR='ip:port'" IF_FEATURE_IPV6(" ('[ip]:port' for IPv6)")
//usage:     "\nUDPLOCALADDR='ip:port'"
//usage:     "\nIf -h:"
//usage:     "\nUDPLOCALHOST='hostname' (-l NAME is used if specified)"
//usage:     "\nUDPREMOTEHOST='hostname'"

#include "libbb.h"
#include "common_bufsiz.h"

#ifdef __linux__
/* from linux/netfilter_ipv4.h: */
# undef  SO_ORIGINAL_DST
# define SO_ORIGINAL_DST 80
#endif

// TODO: move into this file:
#include "tcpudp_perhost.h"

#ifdef SSLSVD
#include "matrixSsl.h"
#include "ssl_io.h"
#endif

struct globals {
	unsigned verbose;
	unsigned max_per_host;
	unsigned cur_per_host;
	unsigned cnum;
	unsigned cmax;
	struct hcc *cc;
	char **env_cur;
	char *env_var[1]; /* actually bigger */
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define verbose      (G.verbose     )
#define max_per_host (G.max_per_host)
#define cur_per_host (G.cur_per_host)
#define cnum         (G.cnum        )
#define cmax         (G.cmax        )
#define env_cur      (G.env_cur     )
#define env_var      (G.env_var     )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	cmax = 30; \
	env_cur = &env_var[0]; \
} while (0)


/* We have to be careful about leaking memory in repeated setenv's */
static void xsetenv_plain(const char *n, const char *v)
{
	char *var = xasprintf("%s=%s", n, v);
	*env_cur++ = var;
	putenv(var);
}

static void xsetenv_proto(const char *proto, const char *n, const char *v)
{
	char *var = xasprintf("%s%s=%s", proto, n, v);
	*env_cur++ = var;
	putenv(var);
}

static void undo_xsetenv(void)
{
	char **pp = env_cur = &env_var[0];
	while (*pp) {
		char *var = *pp;
		bb_unsetenv_and_free(var);
		*pp++ = NULL;
	}
}

static void sig_term_handler(int sig)
{
	if (verbose)
		bb_error_msg("got signal %u, exit", sig);
	kill_myself_with_sig(sig);
}

/* Little bloated, but tries to give accurate info how child exited.
 * Makes easier to spot segfaulting children etc... */
static void print_waitstat(unsigned pid, int wstat)
{
	unsigned e = 0;
	const char *cause = "?exit";

	if (WIFEXITED(wstat)) {
		cause++;
		e = WEXITSTATUS(wstat);
	} else if (WIFSIGNALED(wstat)) {
		cause = "signal";
		e = WTERMSIG(wstat);
	}
	bb_error_msg("end %d %s %d", pid, cause, e);
}

/* Must match getopt32 in main! */
enum {
	OPT_c = (1 << 0),
	OPT_C = (1 << 1),
	OPT_i = (1 << 2),
	OPT_x = (1 << 3),
	OPT_u = (1 << 4),
	OPT_l = (1 << 5),
	OPT_E = (1 << 6),
	OPT_b = (1 << 7),
	OPT_h = (1 << 8),
	OPT_p = (1 << 9),
	OPT_t = (1 << 10),
	OPT_v = (1 << 11),
	OPT_V = (1 << 12),
	OPT_U = (1 << 13), /* from here: sslsvd only */
	OPT_slash = (1 << 14),
	OPT_Z = (1 << 15),
	OPT_K = (1 << 16),
};

static void connection_status(void)
{
	/* "only 1 client max" desn't need this */
	if (cmax > 1)
		bb_error_msg("status %u/%u", cnum, cmax);
}

static void sig_child_handler(int sig UNUSED_PARAM)
{
	int wstat;
	pid_t pid;

	while ((pid = wait_any_nohang(&wstat)) > 0) {
		if (max_per_host)
			ipsvd_perhost_remove(G.cc, pid);
		if (cnum)
			cnum--;
		if (verbose)
			print_waitstat(pid, wstat);
	}
	if (verbose)
		connection_status();
}

int tcpudpsvd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tcpudpsvd_main(int argc UNUSED_PARAM, char **argv)
{
	char *str_C, *str_t;
	char *user;
	struct hcc *hccp;
	const char *instructs;
	char *msg_per_host = NULL;
	unsigned len_per_host = len_per_host; /* gcc */
#ifndef SSLSVD
	struct bb_uidgid_t ugid;
#endif
	bool tcp;
	uint16_t local_port;
	char *preset_local_hostname = NULL;
	char *remote_hostname = remote_hostname; /* for compiler */
	char *remote_addr = remote_addr; /* for compiler */
	len_and_sockaddr *lsa;
	len_and_sockaddr local, remote;
	socklen_t sa_len;
	int pid;
	int sock;
	int conn;
	unsigned backlog = 20;
	unsigned opts;

	INIT_G();

	tcp = (applet_name[0] == 't');

	/* "+": stop on first non-option */
#ifdef SSLSVD
	opts = getopt32(argv, "^+"
		"c:+C:i:x:u:l:Eb:+hpt:vU:/:Z:K:" /* -c NUM, -b NUM */
		"\0"
		/* 3+ args, -i at most once, -p implies -h, -v is a counter */
		"-3:i--i:ph:vv",
		&cmax, &str_C, &instructs, &instructs, &user, &preset_local_hostname,
		&backlog, &str_t, &ssluser, &root, &cert, &key, &verbose
	);
#else
	opts = getopt32(argv, "^+"
		"c:+C:i:x:u:l:Eb:+hpt:v" /* -c NUM, -b NUM */
		"\0"
		/* 3+ args, -i at most once, -p implies -h, -v is a counter */
		"-3:i--i:ph:vv",
		&cmax, &str_C, &instructs, &instructs, &user, &preset_local_hostname,
		&backlog, &str_t, &verbose
	);
#endif
	if (opts & OPT_C) { /* -C n[:message] */
		max_per_host = bb_strtou(str_C, &str_C, 10);
		if (str_C[0]) {
			if (str_C[0] != ':')
				bb_show_usage();
			msg_per_host = str_C + 1;
			len_per_host = strlen(msg_per_host);
		}
	}
	if (max_per_host > cmax)
		max_per_host = cmax;
	if (opts & OPT_u) {
		xget_uidgid(&ugid, user);
	}
#ifdef SSLSVD
	if (opts & OPT_U) ssluser = optarg;
	if (opts & OPT_slash) root = optarg;
	if (opts & OPT_Z) cert = optarg;
	if (opts & OPT_K) key = optarg;
#endif
	argv += optind;
	if (!argv[0][0] || LONE_CHAR(argv[0], '0'))
		argv[0] = (char*)"0.0.0.0";

	/* Per-IP flood protection is not thought-out for UDP */
	if (!tcp)
		max_per_host = 0;

	bb_sanitize_stdio(); /* fd# 0,1,2 must be opened */

#ifdef SSLSVD
	sslser = user;
	client = 0;
	if ((getuid() == 0) && !(opts & OPT_u)) {
		xfunc_error_retval = 100;
		bb_error_msg_and_die(bb_msg_you_must_be_root);
	}
	if (opts & OPT_u)
		if (!uidgid_get(&sslugid, ssluser, 1)) {
			if (errno) {
				bb_perror_msg_and_die("can't get user/group: %s", ssluser);
			}
			bb_error_msg_and_die("unknown user/group %s", ssluser);
		}
	if (!cert) cert = "./cert.pem";
	if (!key) key = cert;
	if (matrixSslOpen() < 0)
		fatal("can't initialize ssl");
	if (matrixSslReadKeys(&keys, cert, key, 0, ca) < 0) {
		if (client)
			fatal("can't read cert, key, or ca file");
		fatal("can't read cert or key file");
	}
	if (matrixSslNewSession(&ssl, keys, 0, SSL_FLAGS_SERVER) < 0)
		fatal("can't create ssl session");
#endif

	sig_block(SIGCHLD);
	signal(SIGCHLD, sig_child_handler);
	bb_signals(BB_FATAL_SIGS, sig_term_handler);
	signal(SIGPIPE, SIG_IGN);

	if (max_per_host)
		G.cc = ipsvd_perhost_init(cmax);

	local_port = bb_lookup_port(argv[1], tcp ? "tcp" : "udp", 0);
	lsa = xhost2sockaddr(argv[0], local_port);
	argv += 2;

	sock = xsocket(lsa->u.sa.sa_family, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
	setsockopt_reuseaddr(sock);
	sa_len = lsa->len; /* I presume sockaddr len stays the same */
	xbind(sock, &lsa->u.sa, sa_len);
	if (tcp) {
		xlisten(sock, backlog);
		close_on_exec_on(sock);
	} else { /* udp: needed for recv_from_to to work: */
		socket_want_pktinfo(sock);
	}
	/* ndelay_off(sock); - it is the default I think? */

#ifndef SSLSVD
	if (opts & OPT_u) {
		/* drop permissions */
		xsetgid(ugid.gid);
		xsetuid(ugid.uid);
	}
#endif

	if (verbose) {
		char *addr = xmalloc_sockaddr2dotted(&lsa->u.sa);
		if (opts & OPT_u)
			bb_error_msg("listening on %s, starting, uid %u, gid %u", addr,
				(unsigned)ugid.uid, (unsigned)ugid.gid);
		else
			bb_error_msg("listening on %s, starting", addr);
		free(addr);
	}

	/* Main accept() loop */

 again:
	hccp = NULL;

 again1:
	close(0);
	/* It's important to close(0) _before_ wait loop:
	 * fd#0 can be a shared connection fd.
	 * If kept open by us, peer can't detect PROG closing it.
	 */
	while (cnum >= cmax)
		wait_for_any_sig(); /* expecting SIGCHLD */

 again2:
	sig_unblock(SIGCHLD);
	local.len = remote.len = sa_len;
	if (tcp) {
		/* Accept a connection to fd #0 */
		conn = accept(sock, &remote.u.sa, &remote.len);
	} else {
		/* In case recv_from_to won't be able to recover local addr.
		 * Also sets port - recv_from_to is unable to do it. */
		local = *lsa;
		conn = recv_from_to(sock, NULL, 0, MSG_PEEK,
				&remote.u.sa, &local.u.sa, sa_len);
	}
	sig_block(SIGCHLD);
	if (conn < 0) {
		if (errno != EINTR)
			bb_perror_msg(tcp ? "accept" : "recv");
		goto again2;
	}
	xmove_fd(tcp ? conn : sock, 0);

	if (max_per_host) {
		/* Drop connection immediately if cur_per_host > max_per_host
		 * (minimizing load under SYN flood) */
		remote_addr = xmalloc_sockaddr2dotted_noport(&remote.u.sa);
		cur_per_host = ipsvd_perhost_add(G.cc, remote_addr, max_per_host, &hccp);
		if (cur_per_host > max_per_host) {
			/* ipsvd_perhost_add detected that max is exceeded
			 * (and did not store ip in connection table) */
			free(remote_addr);
			if (msg_per_host) {
				/* don't block or test for errors */
				send(0, msg_per_host, len_per_host, MSG_DONTWAIT);
			}
			goto again1;
		}
		/* NB: remote_addr is not leaked, it is stored in conn table */
	}

	if (!tcp) {
		/* Voodoo magic: making udp sockets each receive its own
		 * packets is not trivial, and I still not sure
		 * I do it 100% right.
		 * 1) we have to do it before fork()
		 * 2) order is important - is it right now? */

		/* Open new non-connected UDP socket for further clients... */
		sock = xsocket(lsa->u.sa.sa_family, SOCK_DGRAM, 0);
		setsockopt_reuseaddr(sock);
		/* Make plain write/send work for old socket by supplying default
		 * destination address. This also restricts incoming packets
		 * to ones coming from this remote IP. */
		xconnect(0, &remote.u.sa, sa_len);
	/* hole? at this point we have no wildcard udp socket...
	 * can this cause clients to get "port unreachable" icmp?
	 * Yup, time window is very small, but it exists (is it?) */
		/* ..."open new socket", continued */
		xbind(sock, &lsa->u.sa, sa_len);
		socket_want_pktinfo(sock);

		/* Doesn't work:
		 * we cannot replace fd #0 - we will lose pending packet
		 * which is already buffered for us! And we cannot use fd #1
		 * instead - it will "intercept" all following packets, but child
		 * does not expect data coming *from fd #1*! */
#if 0
		/* Make it so that local addr is fixed to localp->u.sa
		 * and we don't accidentally accept packets to other local IPs. */
		/* NB: we possibly bind to the _very_ same_ address & port as the one
		 * already bound in parent! This seems to work in Linux.
		 * (otherwise we can move socket to fd #0 only if bind succeeds) */
		close(0);
		set_nport(&localp->u.sa, htons(local_port));
		xmove_fd(xsocket(localp->u.sa.sa_family, SOCK_DGRAM, 0), 0);
		setsockopt_reuseaddr(0); /* crucial */
		xbind(0, &localp->u.sa, localp->len);
#endif
	}

	pid = vfork();
	if (pid == -1) {
		bb_perror_msg("vfork");
		goto again;
	}

	if (pid != 0) {
		/* Parent */
		cnum++;
		if (verbose)
			connection_status();
		if (hccp)
			hccp->pid = pid;
		/* clean up changes done by vforked child */
		undo_xsetenv();
		goto again;
	}

	/* Child: prepare env, log, and exec prog */

	{ /* vfork alert! every xmalloc in this block should be freed! */
		char *local_hostname = local_hostname; /* for compiler */
		char *local_addr = NULL;
		char *free_me0 = NULL;
		char *free_me1 = NULL;
		char *free_me2 = NULL;

		if (verbose || !(opts & OPT_E)) {
			if (!max_per_host) /* remote_addr is not yet known */
				free_me0 = remote_addr = xmalloc_sockaddr2dotted(&remote.u.sa);
			if (opts & OPT_h) {
				free_me1 = remote_hostname = xmalloc_sockaddr2host_noport(&remote.u.sa);
				if (!remote_hostname) {
					bb_error_msg("can't look up hostname for %s", remote_addr);
					remote_hostname = remote_addr;
				}
			}
			/* Find out local IP peer connected to.
			 * Errors ignored (I'm not paranoid enough to imagine kernel
			 * which doesn't know local IP). */
			if (tcp)
				getsockname(0, &local.u.sa, &local.len);
			/* else: for UDP it is done earlier by parent */
			local_addr = xmalloc_sockaddr2dotted(&local.u.sa);
			if (opts & OPT_h) {
				local_hostname = preset_local_hostname;
				if (!local_hostname) {
					free_me2 = local_hostname = xmalloc_sockaddr2host_noport(&local.u.sa);
					if (!local_hostname)
						bb_error_msg_and_die("can't look up hostname for %s", local_addr);
				}
				/* else: local_hostname is not NULL, but is NOT malloced! */
			}
		}
		if (verbose) {
			pid = getpid();
			if (max_per_host) {
				bb_error_msg("concurrency %s %u/%u",
					remote_addr,
					cur_per_host, max_per_host);
			}
			bb_error_msg((opts & OPT_h)
				? "start %u %s-%s (%s-%s)"
				: "start %u %s-%s",
				pid,
				local_addr, remote_addr,
				local_hostname, remote_hostname);
		}

		if (!(opts & OPT_E)) {
			/* setup ucspi env */
			const char *proto = tcp ? "TCP" : "UDP";

#ifdef SO_ORIGINAL_DST
			/* Extract "original" destination addr:port
			 * from Linux firewall. Useful when you redirect
			 * an outbond connection to local handler, and it needs
			 * to know where it originally tried to connect */
			if (tcp && getsockopt(0, SOL_IP, SO_ORIGINAL_DST, &local.u.sa, &local.len) == 0) {
				char *addr = xmalloc_sockaddr2dotted(&local.u.sa);
				xsetenv_plain("TCPORIGDSTADDR", addr);
				free(addr);
			}
#endif
			xsetenv_plain("PROTO", proto);
			xsetenv_proto(proto, "LOCALADDR", local_addr);
			xsetenv_proto(proto, "REMOTEADDR", remote_addr);
			if (opts & OPT_h) {
				xsetenv_proto(proto, "LOCALHOST", local_hostname);
				xsetenv_proto(proto, "REMOTEHOST", remote_hostname);
			}
			//compat? xsetenv_proto(proto, "REMOTEINFO", "");
			/* additional */
			if (cur_per_host > 0) /* can not be true for udp */
				xsetenv_plain("TCPCONCURRENCY", utoa(cur_per_host));
		}
		free(local_addr);
		free(free_me0);
		free(free_me1);
		free(free_me2);
	}

	xdup2(0, 1);

	signal(SIGPIPE, SIG_DFL); /* this one was SIG_IGNed */
	/* Non-ignored signals revert to SIG_DFL on exec anyway */
	/*signal(SIGCHLD, SIG_DFL);*/
	sig_unblock(SIGCHLD);

#ifdef SSLSVD
	strcpy(id, utoa(pid));
	ssl_io(0, argv);
	bb_perror_msg_and_die("can't execute '%s'", argv[0]);
#else
	BB_EXECVP_or_die(argv);
#endif
}

/*
tcpsvd [-hpEvv] [-c n] [-C n:msg] [-b n] [-u user] [-l name]
	[-i dir|-x cdb] [ -t sec] host port prog

tcpsvd creates a TCP/IP socket, binds it to the address host:port,
and listens on the socket for incoming connections.

On each incoming connection, tcpsvd conditionally runs a program,
with standard input reading from the socket, and standard output
writing to the socket, to handle this connection. tcpsvd keeps
listening on the socket for new connections, and can handle
multiple connections simultaneously.

tcpsvd optionally checks for special instructions depending
on the IP address or hostname of the client that initiated
the connection, see ipsvd-instruct(5).

host
    host either is a hostname, or a dotted-decimal IP address,
    or 0. If host is 0, tcpsvd accepts connections to any local
    IP address.
    * busybox accepts IPv6 addresses and host:port pairs too
      In this case second parameter is ignored
port
    tcpsvd accepts connections to host:port. port may be a name
    from /etc/services or a number.
prog
    prog consists of one or more arguments. For each connection,
    tcpsvd normally runs prog, with file descriptor 0 reading from
    the network, and file descriptor 1 writing to the network.
    By default it also sets up TCP-related environment variables,
    see tcp-environ(5)
-i dir
    read instructions for handling new connections from the instructions
    directory dir. See ipsvd-instruct(5) for details.
    * ignored by busyboxed version
-x cdb
    read instructions for handling new connections from the constant database
    cdb. The constant database normally is created from an instructions
    directory by running ipsvd-cdb(8).
    * ignored by busyboxed version
-t sec
    timeout. This option only takes effect if the -i option is given.
    While checking the instructions directory, check the time of last access
    of the file that matches the clients address or hostname if any, discard
    and remove the file if it wasn't accessed within the last sec seconds;
    tcpsvd does not discard or remove a file if the user's write permission
    is not set, for those files the timeout is disabled. Default is 0,
    which means that the timeout is disabled.
    * ignored by busyboxed version
-l name
    local hostname. Do not look up the local hostname in DNS, but use name
    as hostname. This option must be set if tcpsvd listens on port 53
    to avoid loops.
-u user[:group]
    drop permissions. Switch user ID to user's UID, and group ID to user's
    primary GID after creating and binding to the socket. If user is followed
    by a colon and a group name, the group ID is switched to the GID of group
    instead. All supplementary groups are removed.
-c n
    concurrency. Handle up to n connections simultaneously. Default is 30.
    If there are n connections active, tcpsvd defers acceptance of a new
    connection until an active connection is closed.
-C n[:msg]
    per host concurrency. Allow only up to n connections from the same IP
    address simultaneously. If there are n active connections from one IP
    address, new incoming connections from this IP address are closed
    immediately. If n is followed by :msg, the message msg is written
    to the client if possible, before closing the connection. By default
    msg is empty. See ipsvd-instruct(5) for supported escape sequences in msg.

    For each accepted connection, the current per host concurrency is
    available through the environment variable TCPCONCURRENCY. n and msg
    can be overwritten by ipsvd(7) instructions, see ipsvd-instruct(5).
    By default tcpsvd doesn't keep track of connections.
-h
    Look up the client's hostname in DNS.
-p
    paranoid. After looking up the client's hostname in DNS, look up the IP
    addresses in DNS for that hostname, and forget about the hostname
    if none of the addresses match the client's IP address. You should
    set this option if you use hostname based instructions. The -p option
    implies the -h option.
    * ignored by busyboxed version
-b n
    backlog. Allow a backlog of approximately n TCP SYNs. On some systems n
    is silently limited. Default is 20.
-E
    no special environment. Do not set up TCP-related environment variables.
-v
    verbose. Print verbose messages to standard output.
-vv
    more verbose. Print more verbose messages to standard output.
    * no difference between -v and -vv in busyboxed version
*/
