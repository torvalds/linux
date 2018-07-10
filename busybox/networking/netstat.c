/* vi: set sw=4 ts=4: */
/*
 * Mini netstat implementation(s) for busybox
 * based in part on the netstat implementation from net-tools.
 *
 * Copyright (C) 2002 by Bart Visscher <magick@linux-fan.com>
 *
 * 2002-04-20
 * IPV6 support added by Bart Visscher <magick@linux-fan.com>
 *
 * 2008-07-10
 * optional '-p' flag support ported from net-tools by G. Somlo <somlo@cmu.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config NETSTAT
//config:	bool "netstat (10 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	netstat prints information about the Linux networking subsystem.
//config:
//config:config FEATURE_NETSTAT_WIDE
//config:	bool "Enable wide output"
//config:	default y
//config:	depends on NETSTAT
//config:	help
//config:	Add support for wide columns. Useful when displaying IPv6 addresses
//config:	(-W option).
//config:
//config:config FEATURE_NETSTAT_PRG
//config:	bool "Enable PID/Program name output"
//config:	default y
//config:	depends on NETSTAT
//config:	help
//config:	Add support for -p flag to print out PID and program name.
//config:	+700 bytes of code.

//applet:IF_NETSTAT(APPLET(netstat, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_NETSTAT) += netstat.o

#include "libbb.h"
#include "inet_common.h"

//usage:#define netstat_trivial_usage
//usage:       "[-"IF_ROUTE("r")"al] [-tuwx] [-en"IF_FEATURE_NETSTAT_WIDE("W")IF_FEATURE_NETSTAT_PRG("p")"]"
//usage:#define netstat_full_usage "\n\n"
//usage:       "Display networking information\n"
//usage:	IF_ROUTE(
//usage:     "\n	-r	Routing table"
//usage:	)
//usage:     "\n	-a	All sockets"
//usage:     "\n	-l	Listening sockets"
//usage:     "\n		Else: connected sockets"
//usage:     "\n	-t	TCP sockets"
//usage:     "\n	-u	UDP sockets"
//usage:     "\n	-w	Raw sockets"
//usage:     "\n	-x	Unix sockets"
//usage:     "\n		Else: all socket types"
//usage:     "\n	-e	Other/more information"
//usage:     "\n	-n	Don't resolve names"
//usage:	IF_FEATURE_NETSTAT_WIDE(
//usage:     "\n	-W	Wide display"
//usage:	)
//usage:	IF_FEATURE_NETSTAT_PRG(
//usage:     "\n	-p	Show PID/program name for sockets"
//usage:	)

#define NETSTAT_OPTS "laentuwx" \
	IF_ROUTE(               "r") \
	IF_FEATURE_NETSTAT_WIDE("W") \
	IF_FEATURE_NETSTAT_PRG( "p")

enum {
	OPT_sock_listen = 1 << 0, // l
	OPT_sock_all    = 1 << 1, // a
	OPT_extended    = 1 << 2, // e
	OPT_noresolve   = 1 << 3, // n
	OPT_sock_tcp    = 1 << 4, // t
	OPT_sock_udp    = 1 << 5, // u
	OPT_sock_raw    = 1 << 6, // w
	OPT_sock_unix   = 1 << 7, // x
	OPTBIT_x        = 7,
	IF_ROUTE(               OPTBIT_ROUTE,)
	IF_FEATURE_NETSTAT_WIDE(OPTBIT_WIDE ,)
	IF_FEATURE_NETSTAT_PRG( OPTBIT_PRG  ,)
	OPT_route       = IF_ROUTE(               (1 << OPTBIT_ROUTE)) + 0, // r
	OPT_wide        = IF_FEATURE_NETSTAT_WIDE((1 << OPTBIT_WIDE )) + 0, // W
	OPT_prg         = IF_FEATURE_NETSTAT_PRG( (1 << OPTBIT_PRG  )) + 0, // p
};

#define NETSTAT_CONNECTED 0x01
#define NETSTAT_LISTENING 0x02
#define NETSTAT_NUMERIC   0x04
/* Must match getopt32 option string */
#define NETSTAT_TCP       0x10
#define NETSTAT_UDP       0x20
#define NETSTAT_RAW       0x40
#define NETSTAT_UNIX      0x80
#define NETSTAT_ALLPROTO  (NETSTAT_TCP|NETSTAT_UDP|NETSTAT_RAW|NETSTAT_UNIX)


enum {
	TCP_ESTABLISHED = 1,
	TCP_SYN_SENT,
	TCP_SYN_RECV,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_TIME_WAIT,
	TCP_CLOSE,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_LISTEN,
	TCP_CLOSING, /* now a valid state */
};

static const char *const tcp_state[] = {
	"",
	"ESTABLISHED",
	"SYN_SENT",
	"SYN_RECV",
	"FIN_WAIT1",
	"FIN_WAIT2",
	"TIME_WAIT",
	"CLOSE",
	"CLOSE_WAIT",
	"LAST_ACK",
	"LISTEN",
	"CLOSING"
};

typedef enum {
	SS_FREE = 0,     /* not allocated                */
	SS_UNCONNECTED,  /* unconnected to any socket    */
	SS_CONNECTING,   /* in process of connecting     */
	SS_CONNECTED,    /* connected to socket          */
	SS_DISCONNECTING /* in process of disconnecting  */
} socket_state;

#define SO_ACCEPTCON (1<<16)  /* performed a listen           */
#define SO_WAITDATA  (1<<17)  /* wait data to read            */
#define SO_NOSPACE   (1<<18)  /* no space to write            */

#define ADDR_NORMAL_WIDTH        23
/* When there are IPv6 connections the IPv6 addresses will be
 * truncated to none-recognition. The '-W' option makes the
 * address columns wide enough to accommodate for longest possible
 * IPv6 addresses, i.e. addresses of the form
 * xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:ddd.ddd.ddd.ddd
 */
#define ADDR_WIDE                51  /* INET6_ADDRSTRLEN + 5 for the port number */
#if ENABLE_FEATURE_NETSTAT_WIDE
# define FMT_NET_CONN_DATA       "%s   %6lu %6lu %-*s %-*s %-12s"
# define FMT_NET_CONN_HEADER     "\nProto Recv-Q Send-Q %-*s %-*s State       %s\n"
#else
# define FMT_NET_CONN_DATA       "%s   %6lu %6lu %-23s %-23s %-12s"
# define FMT_NET_CONN_HEADER     "\nProto Recv-Q Send-Q %-23s %-23s State       %s\n"
#endif

#define PROGNAME_WIDTH     20
#define PROGNAME_WIDTH_STR "20"
/* PROGNAME_WIDTH chars: 12345678901234567890 */
#define PROGNAME_BANNER "PID/Program name    "

struct prg_node {
	struct prg_node *next;
	long inode;
	char name[PROGNAME_WIDTH];
};

#define PRG_HASH_SIZE 211

struct globals {
	smallint flags;
#if ENABLE_FEATURE_NETSTAT_PRG
	smallint prg_cache_loaded;
	struct prg_node *prg_hash[PRG_HASH_SIZE];
#endif
#if ENABLE_FEATURE_NETSTAT_PRG
	const char *progname_banner;
#endif
#if ENABLE_FEATURE_NETSTAT_WIDE
	unsigned addr_width;
#endif
};
#define G (*ptr_to_globals)
#define flags            (G.flags           )
#define prg_cache_loaded (G.prg_cache_loaded)
#define prg_hash         (G.prg_hash        )
#if ENABLE_FEATURE_NETSTAT_PRG
# define progname_banner (G.progname_banner )
#else
# define progname_banner ""
#endif
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	flags = NETSTAT_CONNECTED | NETSTAT_ALLPROTO; \
} while (0)


#if ENABLE_FEATURE_NETSTAT_PRG

/* Deliberately truncating long to unsigned *int* */
#define PRG_HASHIT(x) ((unsigned)(x) % PRG_HASH_SIZE)

static void prg_cache_add(long inode, char *name)
{
	unsigned hi = PRG_HASHIT(inode);
	struct prg_node **pnp, *pn;

	prg_cache_loaded = 2;
	for (pnp = prg_hash + hi; (pn = *pnp) != NULL; pnp = &pn->next) {
		if (pn->inode == inode) {
			/* Some warning should be appropriate here
			 * as we got multiple processes for one i-node */
			return;
		}
	}
	*pnp = xzalloc(sizeof(struct prg_node));
	pn = *pnp;
	pn->inode = inode;
	safe_strncpy(pn->name, name, PROGNAME_WIDTH);
}

static const char *prg_cache_get(long inode)
{
	unsigned hi = PRG_HASHIT(inode);
	struct prg_node *pn;

	for (pn = prg_hash[hi]; pn; pn = pn->next)
		if (pn->inode == inode)
			return pn->name;
	return "-";
}

#if ENABLE_FEATURE_CLEAN_UP
static void prg_cache_clear(void)
{
	struct prg_node **pnp, *pn;

	for (pnp = prg_hash; pnp < prg_hash + PRG_HASH_SIZE; pnp++) {
		while ((pn = *pnp) != NULL) {
			*pnp = pn->next;
			free(pn);
		}
	}
}
#else
#define prg_cache_clear() ((void)0)
#endif

static long extract_socket_inode(const char *lname)
{
	long inode = -1;

	if (is_prefixed_with(lname, "socket:[")) {
		/* "socket:[12345]", extract the "12345" as inode */
		inode = bb_strtoul(lname + sizeof("socket:[")-1, (char**)&lname, 0);
		if (*lname != ']')
			inode = -1;
	} else if (is_prefixed_with(lname, "[0000]:")) {
		/* "[0000]:12345", extract the "12345" as inode */
		inode = bb_strtoul(lname + sizeof("[0000]:")-1, NULL, 0);
		if (errno) /* not NUL terminated? */
			inode = -1;
	}

#if 0 /* bb_strtol returns all-ones bit pattern on ERANGE anyway */
	if (errno == ERANGE)
		inode = -1;
#endif
	return inode;
}

static int FAST_FUNC add_to_prg_cache_if_socket(const char *fileName,
		struct stat *statbuf UNUSED_PARAM,
		void *pid_slash_progname,
		int depth UNUSED_PARAM)
{
	char *linkname;
	long inode;

	linkname = xmalloc_readlink(fileName);
	if (linkname != NULL) {
		inode = extract_socket_inode(linkname);
		free(linkname);
		if (inode >= 0)
			prg_cache_add(inode, (char *)pid_slash_progname);
	}
	return TRUE;
}

static int FAST_FUNC dir_act(const char *fileName,
		struct stat *statbuf UNUSED_PARAM,
		void *userData UNUSED_PARAM,
		int depth)
{
	const char *pid;
	char *pid_slash_progname;
	char proc_pid_fname[sizeof("/proc/%u/cmdline") + sizeof(long)*3];
	char cmdline_buf[512];
	int n, len;

	if (depth == 0) /* "/proc" itself */
		return TRUE; /* continue looking one level below /proc */

	pid = fileName + sizeof("/proc/")-1; /* point after "/proc/" */
	if (!isdigit(pid[0])) /* skip /proc entries which aren't processes */
		return SKIP;

	len = snprintf(proc_pid_fname, sizeof(proc_pid_fname), "%s/cmdline", fileName);
	n = open_read_close(proc_pid_fname, cmdline_buf, sizeof(cmdline_buf) - 1);
	if (n < 0)
		return FALSE;
	cmdline_buf[n] = '\0';

	/* go through all files in /proc/PID/fd and check whether they are sockets */
	strcpy(proc_pid_fname + len - (sizeof("cmdline")-1), "fd");
	pid_slash_progname = concat_path_file(pid, bb_basename(cmdline_buf)); /* "PID/argv0" */
	n = recursive_action(proc_pid_fname,
			ACTION_RECURSE | ACTION_QUIET,
			add_to_prg_cache_if_socket,
			NULL,
			(void *)pid_slash_progname,
			0);
	free(pid_slash_progname);

	if (!n)
		return FALSE; /* signal permissions error to caller */

	return SKIP; /* caller should not recurse further into this dir */
}

static void prg_cache_load(void)
{
	int load_ok;

	prg_cache_loaded = 1;
	load_ok = recursive_action("/proc", ACTION_RECURSE | ACTION_QUIET,
				NULL, dir_act, NULL, 0);
	if (load_ok)
		return;

	if (prg_cache_loaded == 1)
		bb_error_msg("can't scan /proc - are you root?");
	else
		bb_error_msg("showing only processes with your user ID");
}

#else

#define prg_cache_clear()       ((void)0)

#endif //ENABLE_FEATURE_NETSTAT_PRG


#if ENABLE_FEATURE_IPV6
static void build_ipv6_addr(char* local_addr, struct sockaddr_in6* localaddr)
{
	char addr6[INET6_ADDRSTRLEN];
	struct in6_addr in6;

	sscanf(local_addr, "%08X%08X%08X%08X",
		   &in6.s6_addr32[0], &in6.s6_addr32[1],
		   &in6.s6_addr32[2], &in6.s6_addr32[3]);
	inet_ntop(AF_INET6, &in6, addr6, sizeof(addr6));
	inet_pton(AF_INET6, addr6, &localaddr->sin6_addr);

	localaddr->sin6_family = AF_INET6;
}
#endif

static void build_ipv4_addr(char* local_addr, struct sockaddr_in* localaddr)
{
	sscanf(local_addr, "%X", &localaddr->sin_addr.s_addr);
	localaddr->sin_family = AF_INET;
}

static const char *get_sname(int port, const char *proto, int numeric)
{
	if (!port)
		return "*";
	if (!numeric) {
		struct servent *se = getservbyport(port, proto);
		if (se)
			return se->s_name;
	}
	/* hummm, we may return static buffer here!! */
	return itoa(ntohs(port));
}

static char *ip_port_str(struct sockaddr *addr, int port, const char *proto, int numeric)
{
	char *host, *host_port;

	/* Code which used "*" for INADDR_ANY is removed: it's ambiguous
	 * in IPv6, while "0.0.0.0" is not. */

	host = NULL;
	if (!numeric)
		host = xmalloc_sockaddr2host_noport(addr);
	if (!host)
		host = xmalloc_sockaddr2dotted_noport(addr);

	host_port = xasprintf("%s:%s", host, get_sname(htons(port), proto, numeric));
	free(host);
	return host_port;
}

struct inet_params {
	int local_port, rem_port, state, uid;
	union {
		struct sockaddr     sa;
		struct sockaddr_in  sin;
#if ENABLE_FEATURE_IPV6
		struct sockaddr_in6 sin6;
#endif
	} localaddr, remaddr;
	unsigned long rxq, txq, inode;
};

static int scan_inet_proc_line(struct inet_params *param, char *line)
{
	int num;
	/* IPv6 /proc files use 32-char hex representation
	 * of IPv6 address, followed by :PORT_IN_HEX
	 */
	char local_addr[33], rem_addr[33]; /* 32 + 1 for NUL */

	num = sscanf(line,
			"%*d: %32[0-9A-Fa-f]:%X "
			"%32[0-9A-Fa-f]:%X %X "
			"%lX:%lX %*X:%*X "
			"%*X %d %*d %lu ",
			local_addr, &param->local_port,
			rem_addr, &param->rem_port, &param->state,
			&param->txq, &param->rxq,
			&param->uid, &param->inode);
	if (num < 9) {
		return 1; /* error */
	}

	if (strlen(local_addr) > 8) {
#if ENABLE_FEATURE_IPV6
		build_ipv6_addr(local_addr, &param->localaddr.sin6);
		build_ipv6_addr(rem_addr, &param->remaddr.sin6);
#endif
	} else {
		build_ipv4_addr(local_addr, &param->localaddr.sin);
		build_ipv4_addr(rem_addr, &param->remaddr.sin);
	}
	return 0;
}

static void print_inet_line(struct inet_params *param,
		const char *state_str, const char *proto, int is_connected)
{
	if ((is_connected && (flags & NETSTAT_CONNECTED))
	 || (!is_connected && (flags & NETSTAT_LISTENING))
	) {
		char *l = ip_port_str(
				&param->localaddr.sa, param->local_port,
				proto, flags & NETSTAT_NUMERIC);
		char *r = ip_port_str(
				&param->remaddr.sa, param->rem_port,
				proto, flags & NETSTAT_NUMERIC);
		printf(FMT_NET_CONN_DATA,
			proto, param->rxq, param->txq,
			IF_FEATURE_NETSTAT_WIDE(G.addr_width,) l,
			IF_FEATURE_NETSTAT_WIDE(G.addr_width,) r,
			state_str);
#if ENABLE_FEATURE_NETSTAT_PRG
		if (option_mask32 & OPT_prg)
			printf("%."PROGNAME_WIDTH_STR"s", prg_cache_get(param->inode));
#endif
		bb_putchar('\n');
		free(l);
		free(r);
	}
}

static int FAST_FUNC tcp_do_one(char *line)
{
	struct inet_params param;

	memset(&param, 0, sizeof(param));
	if (scan_inet_proc_line(&param, line))
		return 1;

	print_inet_line(&param, tcp_state[param.state], "tcp", param.rem_port);
	return 0;
}

#if ENABLE_FEATURE_IPV6
# define NOT_NULL_ADDR(A) ( \
	( (A.sa.sa_family == AF_INET6) \
	  && (A.sin6.sin6_addr.s6_addr32[0] | A.sin6.sin6_addr.s6_addr32[1] | \
	      A.sin6.sin6_addr.s6_addr32[2] | A.sin6.sin6_addr.s6_addr32[3])  \
	) || ( \
	  (A.sa.sa_family == AF_INET) \
	  && A.sin.sin_addr.s_addr != 0 \
	) \
)
#else
# define NOT_NULL_ADDR(A) (A.sin.sin_addr.s_addr)
#endif

static int FAST_FUNC udp_do_one(char *line)
{
	int have_remaddr;
	const char *state_str;
	struct inet_params param;

	memset(&param, 0, sizeof(param)); /* otherwise we display garbage IPv6 scope_ids */
	if (scan_inet_proc_line(&param, line))
		return 1;

	state_str = "UNKNOWN";
	switch (param.state) {
	case TCP_ESTABLISHED:
		state_str = "ESTABLISHED";
		break;
	case TCP_CLOSE:
		state_str = "";
		break;
	}

	have_remaddr = NOT_NULL_ADDR(param.remaddr);
	print_inet_line(&param, state_str, "udp", have_remaddr);
	return 0;
}

static int FAST_FUNC raw_do_one(char *line)
{
	int have_remaddr;
	struct inet_params param;

	if (scan_inet_proc_line(&param, line))
		return 1;

	have_remaddr = NOT_NULL_ADDR(param.remaddr);
	print_inet_line(&param, itoa(param.state), "raw", have_remaddr);
	return 0;
}

static int FAST_FUNC unix_do_one(char *line)
{
	unsigned long refcnt, proto, unix_flags;
	unsigned long inode;
	int type, state;
	int num, path_ofs;
	const char *ss_proto, *ss_state, *ss_type;
	char ss_flags[32];

	/* 2.6.15 may report lines like "... @/tmp/fam-user-^@^@^@^@^@^@^@..."
	 * Other users report long lines filled by NUL bytes.
	 * (those ^@ are NUL bytes too). We see them as empty lines. */
	if (!line[0])
		return 0;

	path_ofs = 0; /* paranoia */
	num = sscanf(line, "%*p: %lX %lX %lX %X %X %lu %n",
			&refcnt, &proto, &unix_flags, &type, &state, &inode, &path_ofs);
	if (num < 6) {
		return 1; /* error */
	}
	if ((flags & (NETSTAT_LISTENING|NETSTAT_CONNECTED)) != (NETSTAT_LISTENING|NETSTAT_CONNECTED)) {
		if ((state == SS_UNCONNECTED) && (unix_flags & SO_ACCEPTCON)) {
			if (!(flags & NETSTAT_LISTENING))
				return 0;
		} else {
			if (!(flags & NETSTAT_CONNECTED))
				return 0;
		}
	}

	switch (proto) {
		case 0:
			ss_proto = "unix";
			break;
		default:
			ss_proto = "??";
	}

	switch (type) {
		case SOCK_STREAM:
			ss_type = "STREAM";
			break;
		case SOCK_DGRAM:
			ss_type = "DGRAM";
			break;
		case SOCK_RAW:
			ss_type = "RAW";
			break;
		case SOCK_RDM:
			ss_type = "RDM";
			break;
		case SOCK_SEQPACKET:
			ss_type = "SEQPACKET";
			break;
		default:
			ss_type = "UNKNOWN";
	}

	switch (state) {
		case SS_FREE:
			ss_state = "FREE";
			break;
		case SS_UNCONNECTED:
			/*
			 * Unconnected sockets may be listening
			 * for something.
			 */
			if (unix_flags & SO_ACCEPTCON) {
				ss_state = "LISTENING";
			} else {
				ss_state = "";
			}
			break;
		case SS_CONNECTING:
			ss_state = "CONNECTING";
			break;
		case SS_CONNECTED:
			ss_state = "CONNECTED";
			break;
		case SS_DISCONNECTING:
			ss_state = "DISCONNECTING";
			break;
		default:
			ss_state = "UNKNOWN";
	}

	strcpy(ss_flags, "[ ");
	if (unix_flags & SO_ACCEPTCON)
		strcat(ss_flags, "ACC ");
	if (unix_flags & SO_WAITDATA)
		strcat(ss_flags, "W ");
	if (unix_flags & SO_NOSPACE)
		strcat(ss_flags, "N ");
	strcat(ss_flags, "]");

	printf("%-5s %-6lu %-11s %-10s %-13s %6lu ",
		ss_proto, refcnt, ss_flags, ss_type, ss_state, inode
		);

#if ENABLE_FEATURE_NETSTAT_PRG
	if (option_mask32 & OPT_prg)
		printf("%-"PROGNAME_WIDTH_STR"s", prg_cache_get(inode));
#endif

	/* TODO: currently we stop at first NUL byte. Is it a problem? */
	line += path_ofs;
	chomp(line);
	while (*line)
		fputc_printable(*line++, stdout);
	bb_putchar('\n');
	return 0;
}

static void do_info(const char *file, int FAST_FUNC (*proc)(char *))
{
	int lnr;
	FILE *procinfo;
	char *buffer;

	/* _stdin is just to save "r" param */
	procinfo = fopen_or_warn_stdin(file);
	if (procinfo == NULL) {
		return;
	}
	lnr = 0;
	/* Why xmalloc_fgets_str? because it doesn't stop on NULs */
	while ((buffer = xmalloc_fgets_str(procinfo, "\n")) != NULL) {
		/* line 0 is skipped */
		if (lnr && proc(buffer))
			bb_error_msg("%s: bogus data on line %d", file, lnr + 1);
		lnr++;
		free(buffer);
	}
	fclose(procinfo);
}

int netstat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int netstat_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;

	INIT_G();

	/* Option string must match NETSTAT_xxx constants */
	opt = getopt32(argv, NETSTAT_OPTS);
	if (opt & OPT_sock_listen) { // -l
		flags &= ~NETSTAT_CONNECTED;
		flags |= NETSTAT_LISTENING;
	}
	if (opt & OPT_sock_all) flags |= NETSTAT_LISTENING | NETSTAT_CONNECTED; // -a
	//if (opt & OPT_extended) // -e
	if (opt & OPT_noresolve) flags |= NETSTAT_NUMERIC; // -n
	//if (opt & OPT_sock_tcp) // -t: NETSTAT_TCP
	//if (opt & OPT_sock_udp) // -u: NETSTAT_UDP
	//if (opt & OPT_sock_raw) // -w: NETSTAT_RAW
	//if (opt & OPT_sock_unix) // -x: NETSTAT_UNIX
#if ENABLE_ROUTE
	if (opt & OPT_route) { // -r
		bb_displayroutes(flags & NETSTAT_NUMERIC, !(opt & OPT_extended));
		return 0;
	}
#endif
#if ENABLE_FEATURE_NETSTAT_WIDE
	G.addr_width = ADDR_NORMAL_WIDTH;
	if (opt & OPT_wide) { // -W
		G.addr_width = ADDR_WIDE;
	}
#endif
#if ENABLE_FEATURE_NETSTAT_PRG
	progname_banner = "";
	if (opt & OPT_prg) { // -p
		progname_banner = PROGNAME_BANNER;
		prg_cache_load();
	}
#endif

	opt &= NETSTAT_ALLPROTO;
	if (opt) {
		flags &= ~NETSTAT_ALLPROTO;
		flags |= opt;
	}
	if (flags & (NETSTAT_TCP|NETSTAT_UDP|NETSTAT_RAW)) {
		printf("Active Internet connections "); /* xxx */

		if ((flags & (NETSTAT_LISTENING|NETSTAT_CONNECTED)) == (NETSTAT_LISTENING|NETSTAT_CONNECTED))
			printf("(servers and established)");
		else if (flags & NETSTAT_LISTENING)
			printf("(only servers)");
		else
			printf("(w/o servers)");
		printf(FMT_NET_CONN_HEADER,
				IF_FEATURE_NETSTAT_WIDE(G.addr_width,) "Local Address",
				IF_FEATURE_NETSTAT_WIDE(G.addr_width,) "Foreign Address",
				progname_banner
		);
	}
	if (flags & NETSTAT_TCP) {
		do_info("/proc/net/tcp", tcp_do_one);
#if ENABLE_FEATURE_IPV6
		do_info("/proc/net/tcp6", tcp_do_one);
#endif
	}
	if (flags & NETSTAT_UDP) {
		do_info("/proc/net/udp", udp_do_one);
#if ENABLE_FEATURE_IPV6
		do_info("/proc/net/udp6", udp_do_one);
#endif
	}
	if (flags & NETSTAT_RAW) {
		do_info("/proc/net/raw", raw_do_one);
#if ENABLE_FEATURE_IPV6
		do_info("/proc/net/raw6", raw_do_one);
#endif
	}
	if (flags & NETSTAT_UNIX) {
		printf("Active UNIX domain sockets ");
		if ((flags & (NETSTAT_LISTENING|NETSTAT_CONNECTED)) == (NETSTAT_LISTENING|NETSTAT_CONNECTED))
			printf("(servers and established)");
		else if (flags & NETSTAT_LISTENING)
			printf("(only servers)");
		else
			printf("(w/o servers)");
		printf("\nProto RefCnt Flags       Type       State         I-Node %sPath\n", progname_banner);
		do_info("/proc/net/unix", unix_do_one);
	}
	prg_cache_clear();
	return 0;
}
