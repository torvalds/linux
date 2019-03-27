/* $OpenBSD: ssh-keyscan.c,v 1.120 2018/06/06 18:29:18 markus Exp $ */
/*
 * Copyright 1995, 1996 by David Mazieres <dm@lcs.mit.edu>.
 *
 * Modification and redistribution in source and binary forms is
 * permitted provided that due credit is given to the author and the
 * OpenBSD project by leaving this copyright notice intact.
 */

#include "includes.h"
 
#include <sys/types.h>
#include "openbsd-compat/sys-queue.h"
#include <sys/resource.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/bn.h>

#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "xmalloc.h"
#include "ssh.h"
#include "sshbuf.h"
#include "sshkey.h"
#include "cipher.h"
#include "kex.h"
#include "compat.h"
#include "myproposal.h"
#include "packet.h"
#include "dispatch.h"
#include "log.h"
#include "atomicio.h"
#include "misc.h"
#include "hostfile.h"
#include "ssherr.h"
#include "ssh_api.h"
#include "dns.h"

/* Flag indicating whether IPv4 or IPv6.  This can be set on the command line.
   Default value is AF_UNSPEC means both IPv4 and IPv6. */
int IPv4or6 = AF_UNSPEC;

int ssh_port = SSH_DEFAULT_PORT;

#define KT_DSA		(1)
#define KT_RSA		(1<<1)
#define KT_ECDSA	(1<<2)
#define KT_ED25519	(1<<3)
#define KT_XMSS		(1<<4)

#define KT_MIN		KT_DSA
#define KT_MAX		KT_XMSS

int get_cert = 0;
int get_keytypes = KT_RSA|KT_ECDSA|KT_ED25519;

int hash_hosts = 0;		/* Hash hostname on output */

int print_sshfp = 0;		/* Print SSHFP records instead of known_hosts */

#define MAXMAXFD 256

/* The number of seconds after which to give up on a TCP connection */
int timeout = 5;

int maxfd;
#define MAXCON (maxfd - 10)

extern char *__progname;
fd_set *read_wait;
size_t read_wait_nfdset;
int ncon;

struct ssh *active_state = NULL; /* XXX needed for linking */

/*
 * Keep a connection structure for each file descriptor.  The state
 * associated with file descriptor n is held in fdcon[n].
 */
typedef struct Connection {
	u_char c_status;	/* State of connection on this file desc. */
#define CS_UNUSED 0		/* File descriptor unused */
#define CS_CON 1		/* Waiting to connect/read greeting */
#define CS_SIZE 2		/* Waiting to read initial packet size */
#define CS_KEYS 3		/* Waiting to read public key packet */
	int c_fd;		/* Quick lookup: c->c_fd == c - fdcon */
	int c_plen;		/* Packet length field for ssh packet */
	int c_len;		/* Total bytes which must be read. */
	int c_off;		/* Length of data read so far. */
	int c_keytype;		/* Only one of KT_* */
	sig_atomic_t c_done;	/* SSH2 done */
	char *c_namebase;	/* Address to free for c_name and c_namelist */
	char *c_name;		/* Hostname of connection for errors */
	char *c_namelist;	/* Pointer to other possible addresses */
	char *c_output_name;	/* Hostname of connection for output */
	char *c_data;		/* Data read from this fd */
	struct ssh *c_ssh;	/* SSH-connection */
	struct timeval c_tv;	/* Time at which connection gets aborted */
	TAILQ_ENTRY(Connection) c_link;	/* List of connections in timeout order. */
} con;

TAILQ_HEAD(conlist, Connection) tq;	/* Timeout Queue */
con *fdcon;

static void keyprint(con *c, struct sshkey *key);

static int
fdlim_get(int hard)
{
#if defined(HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE)
	struct rlimit rlfd;

	if (getrlimit(RLIMIT_NOFILE, &rlfd) < 0)
		return (-1);
	if ((hard ? rlfd.rlim_max : rlfd.rlim_cur) == RLIM_INFINITY)
		return SSH_SYSFDMAX;
	else
		return hard ? rlfd.rlim_max : rlfd.rlim_cur;
#else
	return SSH_SYSFDMAX;
#endif
}

static int
fdlim_set(int lim)
{
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
	struct rlimit rlfd;
#endif

	if (lim <= 0)
		return (-1);
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
	if (getrlimit(RLIMIT_NOFILE, &rlfd) < 0)
		return (-1);
	rlfd.rlim_cur = lim;
	if (setrlimit(RLIMIT_NOFILE, &rlfd) < 0)
		return (-1);
#elif defined (HAVE_SETDTABLESIZE)
	setdtablesize(lim);
#endif
	return (0);
}

/*
 * This is an strsep function that returns a null field for adjacent
 * separators.  This is the same as the 4.4BSD strsep, but different from the
 * one in the GNU libc.
 */
static char *
xstrsep(char **str, const char *delim)
{
	char *s, *e;

	if (!**str)
		return (NULL);

	s = *str;
	e = s + strcspn(s, delim);

	if (*e != '\0')
		*e++ = '\0';
	*str = e;

	return (s);
}

/*
 * Get the next non-null token (like GNU strsep).  Strsep() will return a
 * null token for two adjacent separators, so we may have to loop.
 */
static char *
strnnsep(char **stringp, char *delim)
{
	char *tok;

	do {
		tok = xstrsep(stringp, delim);
	} while (tok && *tok == '\0');
	return (tok);
}


static int
key_print_wrapper(struct sshkey *hostkey, struct ssh *ssh)
{
	con *c;

	if ((c = ssh_get_app_data(ssh)) != NULL)
		keyprint(c, hostkey);
	/* always abort key exchange */
	return -1;
}

static int
ssh2_capable(int remote_major, int remote_minor)
{
	switch (remote_major) {
	case 1:
		if (remote_minor == 99)
			return 1;
		break;
	case 2:
		return 1;
	default:
		break;
	}
	return 0;
}

static void
keygrab_ssh2(con *c)
{
	char *myproposal[PROPOSAL_MAX] = { KEX_CLIENT };
	int r;

	switch (c->c_keytype) {
	case KT_DSA:
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] = get_cert ?
		    "ssh-dss-cert-v01@openssh.com" : "ssh-dss";
		break;
	case KT_RSA:
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] = get_cert ?
		    "ssh-rsa-cert-v01@openssh.com" : "ssh-rsa";
		break;
	case KT_ED25519:
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] = get_cert ?
		    "ssh-ed25519-cert-v01@openssh.com" : "ssh-ed25519";
		break;
	case KT_XMSS:
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] = get_cert ?
		    "ssh-xmss-cert-v01@openssh.com" : "ssh-xmss@openssh.com";
		break;
	case KT_ECDSA:
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] = get_cert ?
		    "ecdsa-sha2-nistp256-cert-v01@openssh.com,"
		    "ecdsa-sha2-nistp384-cert-v01@openssh.com,"
		    "ecdsa-sha2-nistp521-cert-v01@openssh.com" :
		    "ecdsa-sha2-nistp256,"
		    "ecdsa-sha2-nistp384,"
		    "ecdsa-sha2-nistp521";
		break;
	default:
		fatal("unknown key type %d", c->c_keytype);
		break;
	}
	if ((r = kex_setup(c->c_ssh, myproposal)) != 0) {
		free(c->c_ssh);
		fprintf(stderr, "kex_setup: %s\n", ssh_err(r));
		exit(1);
	}
#ifdef WITH_OPENSSL
	c->c_ssh->kex->kex[KEX_DH_GRP1_SHA1] = kexdh_client;
	c->c_ssh->kex->kex[KEX_DH_GRP14_SHA1] = kexdh_client;
	c->c_ssh->kex->kex[KEX_DH_GRP14_SHA256] = kexdh_client;
	c->c_ssh->kex->kex[KEX_DH_GRP16_SHA512] = kexdh_client;
	c->c_ssh->kex->kex[KEX_DH_GRP18_SHA512] = kexdh_client;
	c->c_ssh->kex->kex[KEX_DH_GEX_SHA1] = kexgex_client;
	c->c_ssh->kex->kex[KEX_DH_GEX_SHA256] = kexgex_client;
# ifdef OPENSSL_HAS_ECC
	c->c_ssh->kex->kex[KEX_ECDH_SHA2] = kexecdh_client;
# endif
#endif
	c->c_ssh->kex->kex[KEX_C25519_SHA256] = kexc25519_client;
	ssh_set_verify_host_key_callback(c->c_ssh, key_print_wrapper);
	/*
	 * do the key-exchange until an error occurs or until
	 * the key_print_wrapper() callback sets c_done.
	 */
	ssh_dispatch_run(c->c_ssh, DISPATCH_BLOCK, &c->c_done);
}

static void
keyprint_one(const char *host, struct sshkey *key)
{
	char *hostport;
	const char *known_host, *hashed;

	if (print_sshfp) {
		export_dns_rr(host, key, stdout, 0);
		return;
	}

	hostport = put_host_port(host, ssh_port);
	lowercase(hostport);
	if (hash_hosts && (hashed = host_hash(host, NULL, 0)) == NULL)
		fatal("host_hash failed");
	known_host = hash_hosts ? hashed : hostport;
	if (!get_cert)
		fprintf(stdout, "%s ", known_host);
	sshkey_write(key, stdout);
	fputs("\n", stdout);
	free(hostport);
}

static void
keyprint(con *c, struct sshkey *key)
{
	char *hosts = c->c_output_name ? c->c_output_name : c->c_name;
	char *host, *ohosts;

	if (key == NULL)
		return;
	if (get_cert || (!hash_hosts && ssh_port == SSH_DEFAULT_PORT)) {
		keyprint_one(hosts, key);
		return;
	}
	ohosts = hosts = xstrdup(hosts);
	while ((host = strsep(&hosts, ",")) != NULL)
		keyprint_one(host, key);
	free(ohosts);
}

static int
tcpconnect(char *host)
{
	struct addrinfo hints, *ai, *aitop;
	char strport[NI_MAXSERV];
	int gaierr, s = -1;

	snprintf(strport, sizeof strport, "%d", ssh_port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = IPv4or6;
	hints.ai_socktype = SOCK_STREAM;
	if ((gaierr = getaddrinfo(host, strport, &hints, &aitop)) != 0) {
		error("getaddrinfo %s: %s", host, ssh_gai_strerror(gaierr));
		return -1;
	}
	for (ai = aitop; ai; ai = ai->ai_next) {
		s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s < 0) {
			error("socket: %s", strerror(errno));
			continue;
		}
		if (set_nonblock(s) == -1)
			fatal("%s: set_nonblock(%d)", __func__, s);
		if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0 &&
		    errno != EINPROGRESS)
			error("connect (`%s'): %s", host, strerror(errno));
		else
			break;
		close(s);
		s = -1;
	}
	freeaddrinfo(aitop);
	return s;
}

static int
conalloc(char *iname, char *oname, int keytype)
{
	char *namebase, *name, *namelist;
	int s;

	namebase = namelist = xstrdup(iname);

	do {
		name = xstrsep(&namelist, ",");
		if (!name) {
			free(namebase);
			return (-1);
		}
	} while ((s = tcpconnect(name)) < 0);

	if (s >= maxfd)
		fatal("conalloc: fdno %d too high", s);
	if (fdcon[s].c_status)
		fatal("conalloc: attempt to reuse fdno %d", s);

	debug3("%s: oname %s kt %d", __func__, oname, keytype);
	fdcon[s].c_fd = s;
	fdcon[s].c_status = CS_CON;
	fdcon[s].c_namebase = namebase;
	fdcon[s].c_name = name;
	fdcon[s].c_namelist = namelist;
	fdcon[s].c_output_name = xstrdup(oname);
	fdcon[s].c_data = (char *) &fdcon[s].c_plen;
	fdcon[s].c_len = 4;
	fdcon[s].c_off = 0;
	fdcon[s].c_keytype = keytype;
	monotime_tv(&fdcon[s].c_tv);
	fdcon[s].c_tv.tv_sec += timeout;
	TAILQ_INSERT_TAIL(&tq, &fdcon[s], c_link);
	FD_SET(s, read_wait);
	ncon++;
	return (s);
}

static void
confree(int s)
{
	if (s >= maxfd || fdcon[s].c_status == CS_UNUSED)
		fatal("confree: attempt to free bad fdno %d", s);
	free(fdcon[s].c_namebase);
	free(fdcon[s].c_output_name);
	if (fdcon[s].c_status == CS_KEYS)
		free(fdcon[s].c_data);
	fdcon[s].c_status = CS_UNUSED;
	fdcon[s].c_keytype = 0;
	if (fdcon[s].c_ssh) {
		ssh_packet_close(fdcon[s].c_ssh);
		free(fdcon[s].c_ssh);
		fdcon[s].c_ssh = NULL;
	} else
		close(s);
	TAILQ_REMOVE(&tq, &fdcon[s], c_link);
	FD_CLR(s, read_wait);
	ncon--;
}

static void
contouch(int s)
{
	TAILQ_REMOVE(&tq, &fdcon[s], c_link);
	monotime_tv(&fdcon[s].c_tv);
	fdcon[s].c_tv.tv_sec += timeout;
	TAILQ_INSERT_TAIL(&tq, &fdcon[s], c_link);
}

static int
conrecycle(int s)
{
	con *c = &fdcon[s];
	int ret;

	ret = conalloc(c->c_namelist, c->c_output_name, c->c_keytype);
	confree(s);
	return (ret);
}

static void
congreet(int s)
{
	int n = 0, remote_major = 0, remote_minor = 0;
	char buf[256], *cp;
	char remote_version[sizeof buf];
	size_t bufsiz;
	con *c = &fdcon[s];

	/* send client banner */
	n = snprintf(buf, sizeof buf, "SSH-%d.%d-OpenSSH-keyscan\r\n",
	    PROTOCOL_MAJOR_2, PROTOCOL_MINOR_2);
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		error("snprintf: buffer too small");
		confree(s);
		return;
	}
	if (atomicio(vwrite, s, buf, n) != (size_t)n) {
		error("write (%s): %s", c->c_name, strerror(errno));
		confree(s);
		return;
	}

	for (;;) {
		memset(buf, '\0', sizeof(buf));
		bufsiz = sizeof(buf);
		cp = buf;
		while (bufsiz-- &&
		    (n = atomicio(read, s, cp, 1)) == 1 && *cp != '\n') {
			if (*cp == '\r')
				*cp = '\n';
			cp++;
		}
		if (n != 1 || strncmp(buf, "SSH-", 4) == 0)
			break;
	}
	if (n == 0) {
		switch (errno) {
		case EPIPE:
			error("%s: Connection closed by remote host", c->c_name);
			break;
		case ECONNREFUSED:
			break;
		default:
			error("read (%s): %s", c->c_name, strerror(errno));
			break;
		}
		conrecycle(s);
		return;
	}
	if (*cp != '\n' && *cp != '\r') {
		error("%s: bad greeting", c->c_name);
		confree(s);
		return;
	}
	*cp = '\0';
	if ((c->c_ssh = ssh_packet_set_connection(NULL, s, s)) == NULL)
		fatal("ssh_packet_set_connection failed");
	ssh_packet_set_timeout(c->c_ssh, timeout, 1);
	ssh_set_app_data(c->c_ssh, c);	/* back link */
	if (sscanf(buf, "SSH-%d.%d-%[^\n]\n",
	    &remote_major, &remote_minor, remote_version) == 3)
		c->c_ssh->compat = compat_datafellows(remote_version);
	else
		c->c_ssh->compat = 0;
	if (!ssh2_capable(remote_major, remote_minor)) {
		debug("%s doesn't support ssh2", c->c_name);
		confree(s);
		return;
	}
	fprintf(stderr, "%c %s:%d %s\n", print_sshfp ? ';' : '#',
	    c->c_name, ssh_port, chop(buf));
	keygrab_ssh2(c);
	confree(s);
}

static void
conread(int s)
{
	con *c = &fdcon[s];
	size_t n;

	if (c->c_status == CS_CON) {
		congreet(s);
		return;
	}
	n = atomicio(read, s, c->c_data + c->c_off, c->c_len - c->c_off);
	if (n == 0) {
		error("read (%s): %s", c->c_name, strerror(errno));
		confree(s);
		return;
	}
	c->c_off += n;

	if (c->c_off == c->c_len)
		switch (c->c_status) {
		case CS_SIZE:
			c->c_plen = htonl(c->c_plen);
			c->c_len = c->c_plen + 8 - (c->c_plen & 7);
			c->c_off = 0;
			c->c_data = xmalloc(c->c_len);
			c->c_status = CS_KEYS;
			break;
		default:
			fatal("conread: invalid status %d", c->c_status);
			break;
		}

	contouch(s);
}

static void
conloop(void)
{
	struct timeval seltime, now;
	fd_set *r, *e;
	con *c;
	int i;

	monotime_tv(&now);
	c = TAILQ_FIRST(&tq);

	if (c && (c->c_tv.tv_sec > now.tv_sec ||
	    (c->c_tv.tv_sec == now.tv_sec && c->c_tv.tv_usec > now.tv_usec))) {
		seltime = c->c_tv;
		seltime.tv_sec -= now.tv_sec;
		seltime.tv_usec -= now.tv_usec;
		if (seltime.tv_usec < 0) {
			seltime.tv_usec += 1000000;
			seltime.tv_sec--;
		}
	} else
		timerclear(&seltime);

	r = xcalloc(read_wait_nfdset, sizeof(fd_mask));
	e = xcalloc(read_wait_nfdset, sizeof(fd_mask));
	memcpy(r, read_wait, read_wait_nfdset * sizeof(fd_mask));
	memcpy(e, read_wait, read_wait_nfdset * sizeof(fd_mask));

	while (select(maxfd, r, NULL, e, &seltime) == -1 &&
	    (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK))
		;

	for (i = 0; i < maxfd; i++) {
		if (FD_ISSET(i, e)) {
			error("%s: exception!", fdcon[i].c_name);
			confree(i);
		} else if (FD_ISSET(i, r))
			conread(i);
	}
	free(r);
	free(e);

	c = TAILQ_FIRST(&tq);
	while (c && (c->c_tv.tv_sec < now.tv_sec ||
	    (c->c_tv.tv_sec == now.tv_sec && c->c_tv.tv_usec < now.tv_usec))) {
		int s = c->c_fd;

		c = TAILQ_NEXT(c, c_link);
		conrecycle(s);
	}
}

static void
do_host(char *host)
{
	char *name = strnnsep(&host, " \t\n");
	int j;

	if (name == NULL)
		return;
	for (j = KT_MIN; j <= KT_MAX; j *= 2) {
		if (get_keytypes & j) {
			while (ncon >= MAXCON)
				conloop();
			conalloc(name, *host ? host : name, j);
		}
	}
}

void
fatal(const char *fmt,...)
{
	va_list args;

	va_start(args, fmt);
	do_log(SYSLOG_LEVEL_FATAL, fmt, args);
	va_end(args);
	exit(255);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-46cDHv] [-f file] [-p port] [-T timeout] [-t type]\n"
	    "\t\t   [host | addrlist namelist]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	int debug_flag = 0, log_level = SYSLOG_LEVEL_INFO;
	int opt, fopt_count = 0, j;
	char *tname, *cp, *line = NULL;
	size_t linesize = 0;
	FILE *fp;

	extern int optind;
	extern char *optarg;

	ssh_malloc_init();	/* must be called before any mallocs */
	__progname = ssh_get_progname(argv[0]);
	seed_rng();
	TAILQ_INIT(&tq);

	/* Ensure that fds 0, 1 and 2 are open or directed to /dev/null */
	sanitise_stdfd();

	if (argc <= 1)
		usage();

	while ((opt = getopt(argc, argv, "cDHv46p:T:t:f:")) != -1) {
		switch (opt) {
		case 'H':
			hash_hosts = 1;
			break;
		case 'c':
			get_cert = 1;
			break;
		case 'D':
			print_sshfp = 1;
			break;
		case 'p':
			ssh_port = a2port(optarg);
			if (ssh_port <= 0) {
				fprintf(stderr, "Bad port '%s'\n", optarg);
				exit(1);
			}
			break;
		case 'T':
			timeout = convtime(optarg);
			if (timeout == -1 || timeout == 0) {
				fprintf(stderr, "Bad timeout '%s'\n", optarg);
				usage();
			}
			break;
		case 'v':
			if (!debug_flag) {
				debug_flag = 1;
				log_level = SYSLOG_LEVEL_DEBUG1;
			}
			else if (log_level < SYSLOG_LEVEL_DEBUG3)
				log_level++;
			else
				fatal("Too high debugging level.");
			break;
		case 'f':
			if (strcmp(optarg, "-") == 0)
				optarg = NULL;
			argv[fopt_count++] = optarg;
			break;
		case 't':
			get_keytypes = 0;
			tname = strtok(optarg, ",");
			while (tname) {
				int type = sshkey_type_from_name(tname);

				switch (type) {
				case KEY_DSA:
					get_keytypes |= KT_DSA;
					break;
				case KEY_ECDSA:
					get_keytypes |= KT_ECDSA;
					break;
				case KEY_RSA:
					get_keytypes |= KT_RSA;
					break;
				case KEY_ED25519:
					get_keytypes |= KT_ED25519;
					break;
				case KEY_XMSS:
					get_keytypes |= KT_XMSS;
					break;
				case KEY_UNSPEC:
				default:
					fatal("Unknown key type \"%s\"", tname);
				}
				tname = strtok(NULL, ",");
			}
			break;
		case '4':
			IPv4or6 = AF_INET;
			break;
		case '6':
			IPv4or6 = AF_INET6;
			break;
		case '?':
		default:
			usage();
		}
	}
	if (optind == argc && !fopt_count)
		usage();

	log_init("ssh-keyscan", log_level, SYSLOG_FACILITY_USER, 1);

	maxfd = fdlim_get(1);
	if (maxfd < 0)
		fatal("%s: fdlim_get: bad value", __progname);
	if (maxfd > MAXMAXFD)
		maxfd = MAXMAXFD;
	if (MAXCON <= 0)
		fatal("%s: not enough file descriptors", __progname);
	if (maxfd > fdlim_get(0))
		fdlim_set(maxfd);
	fdcon = xcalloc(maxfd, sizeof(con));

	read_wait_nfdset = howmany(maxfd, NFDBITS);
	read_wait = xcalloc(read_wait_nfdset, sizeof(fd_mask));

	for (j = 0; j < fopt_count; j++) {
		if (argv[j] == NULL)
			fp = stdin;
		else if ((fp = fopen(argv[j], "r")) == NULL)
			fatal("%s: %s: %s", __progname, argv[j],
			    strerror(errno));

		while (getline(&line, &linesize, fp) != -1) {
			/* Chomp off trailing whitespace and comments */
			if ((cp = strchr(line, '#')) == NULL)
				cp = line + strlen(line) - 1;
			while (cp >= line) {
				if (*cp == ' ' || *cp == '\t' ||
				    *cp == '\n' || *cp == '#')
					*cp-- = '\0';
				else
					break;
			}

			/* Skip empty lines */
			if (*line == '\0')
				continue;

			do_host(line);
		}

		if (ferror(fp))
			fatal("%s: %s: %s", __progname, argv[j],
			    strerror(errno));

		fclose(fp);
	}
	free(line);

	while (optind < argc)
		do_host(argv[optind++]);

	while (ncon > 0)
		conloop();

	return (0);
}
