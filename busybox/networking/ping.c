/* vi: set sw=4 ts=4: */
/*
 * Mini ping implementation for busybox
 *
 * Copyright (C) 1999 by Randolph Chung <tausq@debian.org>
 *
 * Adapted from the ping in netkit-base 0.10:
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Muuss.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* from ping6.c:
 * Copyright (C) 1999 by Randolph Chung <tausq@debian.org>
 *
 * This version of ping is adapted from the ping in netkit-base 0.10,
 * which is:
 *
 * Original copyright notice is retained at the end of this file.
 *
 * This version is an adaptation of ping.c from busybox.
 * The code was modified by Bart Visscher <magick@linux-fan.com>
 */
//config:config PING
//config:	bool "ping (9.5 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	ping uses the ICMP protocol's mandatory ECHO_REQUEST datagram to
//config:	elicit an ICMP ECHO_RESPONSE from a host or gateway.
//config:
//config:config PING6
//config:	bool "ping6 (10 kb)"
//config:	default y
//config:	depends on FEATURE_IPV6
//config:	help
//config:	Alias to "ping -6".
//config:
//config:config FEATURE_FANCY_PING
//config:	bool "Enable fancy ping output"
//config:	default y
//config:	depends on PING || PING6
//config:	help
//config:	With this option off, ping will say "HOST is alive!"
//config:	or terminate with SIGALRM in 5 seconds otherwise.
//config:	No command-line options will be recognized.

/* Needs socket(AF_INET, SOCK_RAW, IPPROTO_ICMP), therefore BB_SUID_MAYBE: */
//applet:IF_PING(APPLET(ping, BB_DIR_BIN, BB_SUID_MAYBE))
//applet:IF_PING6(APPLET(ping6, BB_DIR_BIN, BB_SUID_MAYBE))

//kbuild:lib-$(CONFIG_PING)  += ping.o
//kbuild:lib-$(CONFIG_PING6) += ping.o

//usage:#if !ENABLE_FEATURE_FANCY_PING
//usage:# define ping_trivial_usage
//usage:       "HOST"
//usage:# define ping_full_usage "\n\n"
//usage:       "Send ICMP ECHO_REQUEST packets to network hosts"
//usage:# define ping6_trivial_usage
//usage:       "HOST"
//usage:# define ping6_full_usage "\n\n"
//usage:       "Send ICMP ECHO_REQUEST packets to network hosts"
//usage:#else
//usage:# define ping_trivial_usage
//usage:       "[OPTIONS] HOST"
//usage:# define ping_full_usage "\n\n"
//usage:       "Send ICMP ECHO_REQUEST packets to network hosts\n"
//usage:	IF_PING6(
//usage:     "\n	-4,-6		Force IP or IPv6 name resolution"
//usage:	)
//usage:     "\n	-c CNT		Send only CNT pings"
//usage:     "\n	-s SIZE		Send SIZE data bytes in packets (default 56)"
//usage:     "\n	-A		Ping as soon as reply is recevied"
//usage:     "\n	-t TTL		Set TTL"
//usage:     "\n	-I IFACE/IP	Source interface or IP address"
//usage:     "\n	-W SEC		Seconds to wait for the first response (default 10)"
//usage:     "\n			(after all -c CNT packets are sent)"
//usage:     "\n	-w SEC		Seconds until ping exits (default:infinite)"
//usage:     "\n			(can exit earlier with -c CNT)"
//usage:     "\n	-q		Quiet, only display output at start"
//usage:     "\n			and when finished"
//usage:     "\n	-p HEXBYTE	Pattern to use for payload"
//usage:
//usage:# define ping6_trivial_usage
//usage:       "[OPTIONS] HOST"
//usage:# define ping6_full_usage "\n\n"
//usage:       "Send ICMP ECHO_REQUEST packets to network hosts\n"
//usage:     "\n	-c CNT		Send only CNT pings"
//usage:     "\n	-s SIZE		Send SIZE data bytes in packets (default 56)"
//usage:     "\n	-A		Ping as soon as reply is recevied"
//usage:     "\n	-I IFACE/IP	Source interface or IP address"
//usage:     "\n	-q		Quiet, only display output at start"
//usage:     "\n			and when finished"
//usage:     "\n	-p HEXBYTE	Pattern to use for payload"
//usage:
//usage:#endif
//usage:
//usage:#define ping_example_usage
//usage:       "$ ping localhost\n"
//usage:       "PING slag (127.0.0.1): 56 data bytes\n"
//usage:       "64 bytes from 127.0.0.1: icmp_seq=0 ttl=255 time=20.1 ms\n"
//usage:       "\n"
//usage:       "--- debian ping statistics ---\n"
//usage:       "1 packets transmitted, 1 packets received, 0% packet loss\n"
//usage:       "round-trip min/avg/max = 20.1/20.1/20.1 ms\n"
//usage:#define ping6_example_usage
//usage:       "$ ping6 ip6-localhost\n"
//usage:       "PING ip6-localhost (::1): 56 data bytes\n"
//usage:       "64 bytes from ::1: icmp6_seq=0 ttl=64 time=20.1 ms\n"
//usage:       "\n"
//usage:       "--- ip6-localhost ping statistics ---\n"
//usage:       "1 packets transmitted, 1 packets received, 0% packet loss\n"
//usage:       "round-trip min/avg/max = 20.1/20.1/20.1 ms\n"

#include <net/if.h>
#include <netinet/ip_icmp.h>
#include "libbb.h"
#include "common_bufsiz.h"

#ifdef __BIONIC__
/* should be in netinet/ip_icmp.h */
# define ICMP_DEST_UNREACH    3  /* Destination Unreachable  */
# define ICMP_SOURCE_QUENCH   4  /* Source Quench    */
# define ICMP_REDIRECT        5  /* Redirect (change route)  */
# define ICMP_ECHO            8  /* Echo Request      */
# define ICMP_TIME_EXCEEDED  11  /* Time Exceeded    */
# define ICMP_PARAMETERPROB  12  /* Parameter Problem    */
# define ICMP_TIMESTAMP      13  /* Timestamp Request    */
# define ICMP_TIMESTAMPREPLY 14  /* Timestamp Reply    */
# define ICMP_INFO_REQUEST   15  /* Information Request    */
# define ICMP_INFO_REPLY     16  /* Information Reply    */
# define ICMP_ADDRESS        17  /* Address Mask Request    */
# define ICMP_ADDRESSREPLY   18  /* Address Mask Reply    */
#endif

/* Some operating systems, like GNU/Hurd, don't define SOL_RAW, but do have
 * IPPROTO_RAW. Since the IPPROTO definitions are also valid to use for
 * setsockopt (and take the same value as their corresponding SOL definitions,
 * if they exist), we can just fall back on IPPROTO_RAW. */
#ifndef SOL_RAW
# define SOL_RAW IPPROTO_RAW
#endif

#if ENABLE_PING6
# include <netinet/icmp6.h>
/* I see RENUMBERED constants in bits/in.h - !!?
 * What a fuck is going on with libc? Is it a glibc joke? */
# ifdef IPV6_2292HOPLIMIT
#  undef IPV6_HOPLIMIT
#  define IPV6_HOPLIMIT IPV6_2292HOPLIMIT
# endif
#endif

enum {
	DEFDATALEN = 56,
	MAXIPLEN = 60,
	MAXICMPLEN = 76,
	MAX_DUP_CHK = (8 * 128),
	MAXWAIT = 10,
	PINGINTERVAL = 1, /* 1 second */
	pingsock = 0,
};

static void
#if ENABLE_PING6
create_icmp_socket(len_and_sockaddr *lsa)
#else
create_icmp_socket(void)
#define create_icmp_socket(lsa) create_icmp_socket()
#endif
{
	int sock;
#if ENABLE_PING6
	if (lsa->u.sa.sa_family == AF_INET6)
		sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	else
#endif
		sock = socket(AF_INET, SOCK_RAW, 1); /* 1 == ICMP */
	if (sock < 0) {
		if (errno == EPERM)
			bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);
		bb_perror_msg_and_die(bb_msg_can_not_create_raw_socket);
	}

	xmove_fd(sock, pingsock);
}

#if !ENABLE_FEATURE_FANCY_PING

/* Simple version */

struct globals {
	char *hostname;
	char packet[DEFDATALEN + MAXIPLEN + MAXICMPLEN];
	uint16_t myid;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void noresp(int ign UNUSED_PARAM)
{
	printf("No response from %s\n", G.hostname);
	exit(EXIT_FAILURE);
}

static void ping4(len_and_sockaddr *lsa)
{
	struct icmp *pkt;
	int c;

	pkt = (struct icmp *) G.packet;
	/*memset(pkt, 0, sizeof(G.packet)); already is */
	pkt->icmp_type = ICMP_ECHO;
	pkt->icmp_id = G.myid;
	pkt->icmp_cksum = inet_cksum((uint16_t *) pkt, sizeof(G.packet));

	xsendto(pingsock, G.packet, DEFDATALEN + ICMP_MINLEN, &lsa->u.sa, lsa->len);

	/* listen for replies */
	while (1) {
#if 0
		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);

		c = recvfrom(pingsock, G.packet, sizeof(G.packet), 0,
				(struct sockaddr *) &from, &fromlen);
#else
		c = recv(pingsock, G.packet, sizeof(G.packet), 0);
#endif
		if (c < 0) {
			if (errno != EINTR)
				bb_perror_msg("recvfrom");
			continue;
		}
		if (c >= 76) {			/* ip + icmp */
			struct iphdr *iphdr = (struct iphdr *) G.packet;

			pkt = (struct icmp *) (G.packet + (iphdr->ihl << 2));	/* skip ip hdr */
			if (pkt->icmp_id != G.myid)
				continue; /* not our ping */
			if (pkt->icmp_type == ICMP_ECHOREPLY)
				break;
		}
	}
}

#if ENABLE_PING6
static void ping6(len_and_sockaddr *lsa)
{
	struct icmp6_hdr *pkt;
	int c;
	int sockopt;

	pkt = (struct icmp6_hdr *) G.packet;
	/*memset(pkt, 0, sizeof(G.packet)); already is */
	pkt->icmp6_type = ICMP6_ECHO_REQUEST;
	pkt->icmp6_id = G.myid;

	sockopt = offsetof(struct icmp6_hdr, icmp6_cksum);
	setsockopt_int(pingsock, SOL_RAW, IPV6_CHECKSUM, sockopt);

	xsendto(pingsock, G.packet, DEFDATALEN + sizeof(struct icmp6_hdr), &lsa->u.sa, lsa->len);

	/* listen for replies */
	while (1) {
#if 0
		struct sockaddr_in6 from;
		socklen_t fromlen = sizeof(from);

		c = recvfrom(pingsock, G.packet, sizeof(G.packet), 0,
				(struct sockaddr *) &from, &fromlen);
#else
		c = recv(pingsock, G.packet, sizeof(G.packet), 0);
#endif
		if (c < 0) {
			if (errno != EINTR)
				bb_perror_msg("recvfrom");
			continue;
		}
		if (c >= ICMP_MINLEN) {	/* icmp6_hdr */
			if (pkt->icmp6_id != G.myid)
				continue; /* not our ping */
			if (pkt->icmp6_type == ICMP6_ECHO_REPLY)
				break;
		}
	}
}
#endif

#if !ENABLE_PING6
# define common_ping_main(af, argv) common_ping_main(argv)
#endif
static int common_ping_main(sa_family_t af, char **argv)
{
	len_and_sockaddr *lsa;

	INIT_G();

#if ENABLE_PING6
	while ((++argv)[0] && argv[0][0] == '-') {
		if (argv[0][1] == '4') {
			af = AF_INET;
			continue;
		}
		if (argv[0][1] == '6') {
			af = AF_INET6;
			continue;
		}
		bb_show_usage();
	}
#else
	argv++;
#endif

	G.hostname = *argv;
	if (!G.hostname)
		bb_show_usage();

#if ENABLE_PING6
	lsa = xhost_and_af2sockaddr(G.hostname, 0, af);
#else
	lsa = xhost_and_af2sockaddr(G.hostname, 0, AF_INET);
#endif
	/* Set timer _after_ DNS resolution */
	signal(SIGALRM, noresp);
	alarm(5); /* give the host 5000ms to respond */

	create_icmp_socket(lsa);
	G.myid = (uint16_t) getpid();
#if ENABLE_PING6
	if (lsa->u.sa.sa_family == AF_INET6)
		ping6(lsa);
	else
#endif
		ping4(lsa);
	if (ENABLE_FEATURE_CLEAN_UP)
		close(pingsock);
	printf("%s is alive!\n", G.hostname);
	return EXIT_SUCCESS;
}


#else /* FEATURE_FANCY_PING */


/* Full(er) version */

/* -c NUM, -t NUM, -w NUM, -W NUM */
#define OPT_STRING "qvAc:+s:t:+w:+W:+I:np:4"IF_PING6("6")
enum {
	OPT_QUIET = 1 << 0,
	OPT_VERBOSE = 1 << 1,
	OPT_A = 1 << 2,
	OPT_c = 1 << 3,
	OPT_s = 1 << 4,
	OPT_t = 1 << 5,
	OPT_w = 1 << 6,
	OPT_W = 1 << 7,
	OPT_I = 1 << 8,
	/*OPT_n = 1 << 9, - ignored */
	OPT_p = 1 << 10,
	OPT_IPV4 = 1 << 11,
	OPT_IPV6 = (1 << 12) * ENABLE_PING6,
};


struct globals {
	int if_index;
	char *str_I;
	len_and_sockaddr *source_lsa;
	unsigned datalen;
	unsigned pingcount; /* must be int-sized */
	unsigned opt_ttl;
	unsigned long ntransmitted, nreceived, nrepeats;
	uint16_t myid;
	uint8_t pattern;
	unsigned tmin, tmax; /* in us */
	unsigned long long tsum; /* in us, sum of all times */
	unsigned cur_us; /* low word only, we don't need more */
	unsigned deadline_us;
	unsigned timeout;
	unsigned sizeof_rcv_packet;
	char *rcv_packet; /* [datalen + MAXIPLEN + MAXICMPLEN] */
	void *snd_packet; /* [datalen + ipv4/ipv6_const] */
	const char *hostname;
	const char *dotted;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
#if ENABLE_PING6
		struct sockaddr_in6 sin6;
#endif
	} pingaddr;
	unsigned char rcvd_tbl[MAX_DUP_CHK / 8];
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define if_index     (G.if_index    )
#define source_lsa   (G.source_lsa  )
#define str_I        (G.str_I       )
#define datalen      (G.datalen     )
#define pingcount    (G.pingcount   )
#define opt_ttl      (G.opt_ttl     )
#define myid         (G.myid        )
#define tmin         (G.tmin        )
#define tmax         (G.tmax        )
#define tsum         (G.tsum        )
#define timeout      (G.timeout     )
#define hostname     (G.hostname    )
#define dotted       (G.dotted      )
#define pingaddr     (G.pingaddr    )
#define rcvd_tbl     (G.rcvd_tbl    )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
	datalen = DEFDATALEN; \
	timeout = MAXWAIT; \
	tmin = UINT_MAX; \
} while (0)


#define BYTE(bit)	rcvd_tbl[(bit)>>3]
#define MASK(bit)	(1 << ((bit) & 7))
#define SET(bit)	(BYTE(bit) |= MASK(bit))
#define CLR(bit)	(BYTE(bit) &= (~MASK(bit)))
#define TST(bit)	(BYTE(bit) & MASK(bit))

static void print_stats_and_exit(int junk) NORETURN;
static void print_stats_and_exit(int junk UNUSED_PARAM)
{
	unsigned long ul;
	unsigned long nrecv;

	signal(SIGINT, SIG_IGN);

	nrecv = G.nreceived;
	printf("\n--- %s ping statistics ---\n"
		"%lu packets transmitted, "
		"%lu packets received, ",
		hostname, G.ntransmitted, nrecv
	);
	if (G.nrepeats)
		printf("%lu duplicates, ", G.nrepeats);
	ul = G.ntransmitted;
	if (ul != 0)
		ul = (ul - nrecv) * 100 / ul;
	printf("%lu%% packet loss\n", ul);
	if (tmin != UINT_MAX) {
		unsigned tavg = tsum / (nrecv + G.nrepeats);
		printf("round-trip min/avg/max = %u.%03u/%u.%03u/%u.%03u ms\n",
			tmin / 1000, tmin % 1000,
			tavg / 1000, tavg % 1000,
			tmax / 1000, tmax % 1000);
	}
	/* if condition is true, exit with 1 -- 'failure' */
	exit(nrecv == 0 || (G.deadline_us && nrecv < pingcount));
}

static void sendping_tail(void (*sp)(int), int size_pkt)
{
	int sz;

	CLR((uint16_t)G.ntransmitted % MAX_DUP_CHK);
	G.ntransmitted++;

	size_pkt += datalen;

	if (G.deadline_us) {
		unsigned n = G.cur_us - G.deadline_us;
		if ((int)n >= 0)
			print_stats_and_exit(0);
	}

	/* sizeof(pingaddr) can be larger than real sa size, but I think
	 * it doesn't matter */
	sz = xsendto(pingsock, G.snd_packet, size_pkt, &pingaddr.sa, sizeof(pingaddr));
	if (sz != size_pkt)
		bb_error_msg_and_die(bb_msg_write_error);

	if (pingcount == 0 || G.ntransmitted < pingcount) {
		/* Didn't send all pings yet - schedule next in 1s */
		signal(SIGALRM, sp);
		alarm(PINGINTERVAL);
	} else { /* -c NN, and all NN are sent */
		/* Wait for the last ping to come back.
		 * -W timeout: wait for a response in seconds.
		 * Affects only timeout in absence of any responses,
		 * otherwise ping waits for two RTTs. */
		unsigned expire = timeout;

		if (G.nreceived) {
			/* approx. 2*tmax, in seconds (2 RTT) */
			expire = tmax / (512*1024);
			if (expire == 0)
				expire = 1;
		}
		signal(SIGALRM, print_stats_and_exit);
		alarm(expire);
	}
}

static void sendping4(int junk UNUSED_PARAM)
{
	struct icmp *pkt = G.snd_packet;

	memset(pkt, G.pattern, datalen + ICMP_MINLEN + 4);
	pkt->icmp_type = ICMP_ECHO;
	/*pkt->icmp_code = 0;*/
	pkt->icmp_cksum = 0; /* cksum is calculated with this field set to 0 */
	pkt->icmp_seq = htons(G.ntransmitted); /* don't ++ here, it can be a macro */
	pkt->icmp_id = myid;

	/* If datalen < 4, we store timestamp _past_ the packet,
	 * but it's ok - we allocated 4 extra bytes in xzalloc() just in case.
	 */
	/*if (datalen >= 4)*/
		/* No hton: we'll read it back on the same machine */
		*(uint32_t*)&pkt->icmp_dun = G.cur_us = monotonic_us();

	pkt->icmp_cksum = inet_cksum((uint16_t *) pkt, datalen + ICMP_MINLEN);

	sendping_tail(sendping4, ICMP_MINLEN);
}
#if ENABLE_PING6
static void sendping6(int junk UNUSED_PARAM)
{
	struct icmp6_hdr *pkt = G.snd_packet;

	memset(pkt, G.pattern, datalen + sizeof(struct icmp6_hdr) + 4);
	pkt->icmp6_type = ICMP6_ECHO_REQUEST;
	/*pkt->icmp6_code = 0;*/
	/*pkt->icmp6_cksum = 0;*/
	pkt->icmp6_seq = htons(G.ntransmitted); /* don't ++ here, it can be a macro */
	pkt->icmp6_id = myid;

	/*if (datalen >= 4)*/
		*(bb__aliased_uint32_t*)(&pkt->icmp6_data8[4]) = G.cur_us = monotonic_us();

	//TODO? pkt->icmp_cksum = inet_cksum(...);

	sendping_tail(sendping6, sizeof(struct icmp6_hdr));
}
#endif

static const char *icmp_type_name(int id)
{
	switch (id) {
	case ICMP_ECHOREPLY:      return "Echo Reply";
	case ICMP_DEST_UNREACH:   return "Destination Unreachable";
	case ICMP_SOURCE_QUENCH:  return "Source Quench";
	case ICMP_REDIRECT:       return "Redirect (change route)";
	case ICMP_ECHO:           return "Echo Request";
	case ICMP_TIME_EXCEEDED:  return "Time Exceeded";
	case ICMP_PARAMETERPROB:  return "Parameter Problem";
	case ICMP_TIMESTAMP:      return "Timestamp Request";
	case ICMP_TIMESTAMPREPLY: return "Timestamp Reply";
	case ICMP_INFO_REQUEST:   return "Information Request";
	case ICMP_INFO_REPLY:     return "Information Reply";
	case ICMP_ADDRESS:        return "Address Mask Request";
	case ICMP_ADDRESSREPLY:   return "Address Mask Reply";
	default:                  return "unknown ICMP type";
	}
}
#if ENABLE_PING6
/* RFC3542 changed some definitions from RFC2292 for no good reason, whee!
 * the newer 3542 uses a MLD_ prefix where as 2292 uses ICMP6_ prefix */
#ifndef MLD_LISTENER_QUERY
# define MLD_LISTENER_QUERY ICMP6_MEMBERSHIP_QUERY
#endif
#ifndef MLD_LISTENER_REPORT
# define MLD_LISTENER_REPORT ICMP6_MEMBERSHIP_REPORT
#endif
#ifndef MLD_LISTENER_REDUCTION
# define MLD_LISTENER_REDUCTION ICMP6_MEMBERSHIP_REDUCTION
#endif
static const char *icmp6_type_name(int id)
{
	switch (id) {
	case ICMP6_DST_UNREACH:      return "Destination Unreachable";
	case ICMP6_PACKET_TOO_BIG:   return "Packet too big";
	case ICMP6_TIME_EXCEEDED:    return "Time Exceeded";
	case ICMP6_PARAM_PROB:       return "Parameter Problem";
	case ICMP6_ECHO_REPLY:       return "Echo Reply";
	case ICMP6_ECHO_REQUEST:     return "Echo Request";
	case MLD_LISTENER_QUERY:     return "Listener Query";
	case MLD_LISTENER_REPORT:    return "Listener Report";
	case MLD_LISTENER_REDUCTION: return "Listener Reduction";
	default:                     return "unknown ICMP type";
	}
}
#endif

static void unpack_tail(int sz, uint32_t *tp,
		const char *from_str,
		uint16_t recv_seq, int ttl)
{
	unsigned char *b, m;
	const char *dupmsg = " (DUP!)";
	unsigned triptime = triptime; /* for gcc */

	if (tp) {
		/* (int32_t) cast is for hypothetical 64-bit unsigned */
		/* (doesn't hurt 32-bit real-world anyway) */
		triptime = (int32_t) ((uint32_t)monotonic_us() - *tp);
		tsum += triptime;
		if (triptime < tmin)
			tmin = triptime;
		if (triptime > tmax)
			tmax = triptime;
	}

	b = &BYTE(recv_seq % MAX_DUP_CHK);
	m = MASK(recv_seq % MAX_DUP_CHK);
	/*if TST(recv_seq % MAX_DUP_CHK):*/
	if (*b & m) {
		++G.nrepeats;
	} else {
		/*SET(recv_seq % MAX_DUP_CHK):*/
		*b |= m;
		++G.nreceived;
		dupmsg += 7;
	}

	if (option_mask32 & OPT_QUIET)
		return;

	printf("%d bytes from %s: seq=%u ttl=%d", sz,
		from_str, recv_seq, ttl);
	if (tp)
		printf(" time=%u.%03u ms", triptime / 1000, triptime % 1000);
	puts(dupmsg);
	fflush_all();
}
static int unpack4(char *buf, int sz, struct sockaddr_in *from)
{
	struct icmp *icmppkt;
	struct iphdr *iphdr;
	int hlen;

	/* discard if too short */
	if (sz < (datalen + ICMP_MINLEN))
		return 0;

	/* check IP header */
	iphdr = (struct iphdr *) buf;
	hlen = iphdr->ihl << 2;
	sz -= hlen;
	icmppkt = (struct icmp *) (buf + hlen);
	if (icmppkt->icmp_id != myid)
		return 0;				/* not our ping */

	if (icmppkt->icmp_type == ICMP_ECHOREPLY) {
		uint16_t recv_seq = ntohs(icmppkt->icmp_seq);
		uint32_t *tp = NULL;

		if (sz >= ICMP_MINLEN + sizeof(uint32_t))
			tp = (uint32_t *) icmppkt->icmp_data;
		unpack_tail(sz, tp,
			inet_ntoa(*(struct in_addr *) &from->sin_addr.s_addr),
			recv_seq, iphdr->ttl);
		return 1;
	}
	if (icmppkt->icmp_type != ICMP_ECHO) {
		bb_error_msg("warning: got ICMP %d (%s)",
				icmppkt->icmp_type,
				icmp_type_name(icmppkt->icmp_type));
	}
	return 0;
}
#if ENABLE_PING6
static int unpack6(char *packet, int sz, struct sockaddr_in6 *from, int hoplimit)
{
	struct icmp6_hdr *icmppkt;
	char buf[INET6_ADDRSTRLEN];

	/* discard if too short */
	if (sz < (datalen + sizeof(struct icmp6_hdr)))
		return 0;

	icmppkt = (struct icmp6_hdr *) packet;
	if (icmppkt->icmp6_id != myid)
		return 0;				/* not our ping */

	if (icmppkt->icmp6_type == ICMP6_ECHO_REPLY) {
		uint16_t recv_seq = ntohs(icmppkt->icmp6_seq);
		uint32_t *tp = NULL;

		if (sz >= sizeof(struct icmp6_hdr) + sizeof(uint32_t))
			tp = (uint32_t *) &icmppkt->icmp6_data8[4];
		unpack_tail(sz, tp,
			inet_ntop(AF_INET6, &from->sin6_addr,
					buf, sizeof(buf)),
			recv_seq, hoplimit);
		return 1;
	}
	if (icmppkt->icmp6_type != ICMP6_ECHO_REQUEST) {
		bb_error_msg("warning: got ICMP %d (%s)",
				icmppkt->icmp6_type,
				icmp6_type_name(icmppkt->icmp6_type));
	}
	return 0;
}
#endif

static void ping4(len_and_sockaddr *lsa)
{
	int sockopt;

	pingaddr.sin = lsa->u.sin;
	if (source_lsa) {
		if (setsockopt(pingsock, IPPROTO_IP, IP_MULTICAST_IF,
				&source_lsa->u.sa, source_lsa->len))
			bb_error_msg_and_die("can't set multicast source interface");
		xbind(pingsock, &source_lsa->u.sa, source_lsa->len);
	}

	/* enable broadcast pings */
	setsockopt_broadcast(pingsock);

	/* set recv buf (needed if we can get lots of responses: flood ping,
	 * broadcast ping etc) */
	sockopt = (datalen * 2) + 7 * 1024; /* giving it a bit of extra room */
	setsockopt_SOL_SOCKET_int(pingsock, SO_RCVBUF, sockopt);

	if (opt_ttl != 0) {
		setsockopt_int(pingsock, IPPROTO_IP, IP_TTL, opt_ttl);
		/* above doesn't affect packets sent to bcast IP, so... */
		setsockopt_int(pingsock, IPPROTO_IP, IP_MULTICAST_TTL, opt_ttl);
	}

	signal(SIGINT, print_stats_and_exit);

	/* start the ping's going ... */
 send_ping:
	sendping4(0);

	/* listen for replies */
	while (1) {
		struct sockaddr_in from;
		socklen_t fromlen = (socklen_t) sizeof(from);
		int c;

		c = recvfrom(pingsock, G.rcv_packet, G.sizeof_rcv_packet, 0,
				(struct sockaddr *) &from, &fromlen);
		if (c < 0) {
			if (errno != EINTR)
				bb_perror_msg("recvfrom");
			continue;
		}
		c = unpack4(G.rcv_packet, c, &from);
		if (pingcount && G.nreceived >= pingcount)
			break;
		if (c && (option_mask32 & OPT_A)) {
			goto send_ping;
		}
	}
}
#if ENABLE_PING6
static void ping6(len_and_sockaddr *lsa)
{
	int sockopt;
	struct msghdr msg;
	struct sockaddr_in6 from;
	struct iovec iov;
	char control_buf[CMSG_SPACE(36)];

	pingaddr.sin6 = lsa->u.sin6;
	if (source_lsa)
		xbind(pingsock, &source_lsa->u.sa, source_lsa->len);

#ifdef ICMP6_FILTER
	{
		struct icmp6_filter filt;
		if (!(option_mask32 & OPT_VERBOSE)) {
			ICMP6_FILTER_SETBLOCKALL(&filt);
			ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &filt);
		} else {
			ICMP6_FILTER_SETPASSALL(&filt);
		}
		if (setsockopt(pingsock, IPPROTO_ICMPV6, ICMP6_FILTER, &filt,
					sizeof(filt)) < 0)
			bb_error_msg_and_die("setsockopt(%s)", "ICMP6_FILTER");
	}
#endif /*ICMP6_FILTER*/

	/* enable broadcast pings */
	setsockopt_broadcast(pingsock);

	/* set recv buf (needed if we can get lots of responses: flood ping,
	 * broadcast ping etc) */
	sockopt = (datalen * 2) + 7 * 1024; /* giving it a bit of extra room */
	setsockopt_SOL_SOCKET_int(pingsock, SO_RCVBUF, sockopt);

	sockopt = offsetof(struct icmp6_hdr, icmp6_cksum);
	BUILD_BUG_ON(offsetof(struct icmp6_hdr, icmp6_cksum) != 2);
	setsockopt_int(pingsock, SOL_RAW, IPV6_CHECKSUM, sockopt);

	/* request ttl info to be returned in ancillary data */
	setsockopt_1(pingsock, SOL_IPV6, IPV6_HOPLIMIT);

	if (if_index)
		pingaddr.sin6.sin6_scope_id = if_index;

	signal(SIGINT, print_stats_and_exit);

	msg.msg_name = &from;
	msg.msg_namelen = sizeof(from);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	iov.iov_base = G.rcv_packet;
	iov.iov_len = G.sizeof_rcv_packet;

	/* start the ping's going ... */
 send_ping:
	sendping6(0);

	/* listen for replies */
	while (1) {
		int c;
		struct cmsghdr *mp;
		int hoplimit = -1;

		msg.msg_controllen = sizeof(control_buf);
		c = recvmsg(pingsock, &msg, 0);
		if (c < 0) {
			if (errno != EINTR)
				bb_perror_msg("recvfrom");
			continue;
		}
		for (mp = CMSG_FIRSTHDR(&msg); mp; mp = CMSG_NXTHDR(&msg, mp)) {
			if (mp->cmsg_level == SOL_IPV6
			 && mp->cmsg_type == IPV6_HOPLIMIT
			 /* don't check len - we trust the kernel: */
			 /* && mp->cmsg_len >= CMSG_LEN(sizeof(int)) */
			) {
				/*hoplimit = *(int*)CMSG_DATA(mp); - unaligned access */
				move_from_unaligned_int(hoplimit, CMSG_DATA(mp));
			}
		}
		c = unpack6(G.rcv_packet, c, &from, hoplimit);
		if (pingcount && G.nreceived >= pingcount)
			break;
		if (c && (option_mask32 & OPT_A)) {
			goto send_ping;
		}
	}
}
#endif

static void ping(len_and_sockaddr *lsa)
{
	printf("PING %s (%s)", hostname, dotted);
	if (source_lsa) {
		printf(" from %s",
			xmalloc_sockaddr2dotted_noport(&source_lsa->u.sa));
	}
	printf(": %d data bytes\n", datalen);

	create_icmp_socket(lsa);
	/* untested whether "-I addr" really works for IPv6: */
	if (str_I)
		setsockopt_bindtodevice(pingsock, str_I);

	G.sizeof_rcv_packet = datalen + MAXIPLEN + MAXICMPLEN;
	G.rcv_packet = xzalloc(G.sizeof_rcv_packet);
#if ENABLE_PING6
	if (lsa->u.sa.sa_family == AF_INET6) {
		/* +4 reserves a place for timestamp, which may end up sitting
		 * _after_ packet. Saves one if() - see sendping4/6() */
		G.snd_packet = xzalloc(datalen + sizeof(struct icmp6_hdr) + 4);
		ping6(lsa);
	} else
#endif
	{
		G.snd_packet = xzalloc(datalen + ICMP_MINLEN + 4);
		ping4(lsa);
	}
}

static int common_ping_main(int opt, char **argv)
{
	len_and_sockaddr *lsa;
	char *str_s, *str_p;

	INIT_G();

	opt |= getopt32(argv, "^"
			OPT_STRING
			/* exactly one arg; -v and -q don't mix */
			"\0" "=1:q--v:v--q",
			&pingcount, &str_s, &opt_ttl, &G.deadline_us, &timeout, &str_I, &str_p
	);
	if (opt & OPT_s)
		datalen = xatou16(str_s); // -s
	if (opt & OPT_I) { // -I
		if_index = if_nametoindex(str_I);
		if (!if_index) {
			/* TODO: I'm not sure it takes IPv6 unless in [XX:XX..] format */
			source_lsa = xdotted2sockaddr(str_I, 0);
			str_I = NULL; /* don't try to bind to device later */
		}
	}
	if (opt & OPT_p)
		G.pattern = xstrtou_range(str_p, 16, 0, 255);
	if (G.deadline_us) {
		unsigned d = G.deadline_us < INT_MAX/1000000 ? G.deadline_us : INT_MAX/1000000;
		G.deadline_us = 1 | ((d * 1000000) + monotonic_us());
	}

	myid = (uint16_t) getpid();
	hostname = argv[optind];
#if ENABLE_PING6
	{
		sa_family_t af = AF_UNSPEC;
		if (opt & OPT_IPV4)
			af = AF_INET;
		if (opt & OPT_IPV6)
			af = AF_INET6;
		lsa = xhost_and_af2sockaddr(hostname, 0, af);
	}
#else
	lsa = xhost_and_af2sockaddr(hostname, 0, AF_INET);
#endif

	if (source_lsa && source_lsa->u.sa.sa_family != lsa->u.sa.sa_family)
		/* leaking it here... */
		source_lsa = NULL;

	dotted = xmalloc_sockaddr2dotted_noport(&lsa->u.sa);
	ping(lsa);
	print_stats_and_exit(0);
	/*return EXIT_SUCCESS;*/
}
#endif /* FEATURE_FANCY_PING */


#if ENABLE_PING
int ping_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ping_main(int argc UNUSED_PARAM, char **argv)
{
# if !ENABLE_FEATURE_FANCY_PING
	return common_ping_main(AF_UNSPEC, argv);
# else
	return common_ping_main(0, argv);
# endif
}
#endif

#if ENABLE_PING6
int ping6_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ping6_main(int argc UNUSED_PARAM, char **argv)
{
# if !ENABLE_FEATURE_FANCY_PING
	return common_ping_main(AF_INET6, argv);
# else
	return common_ping_main(OPT_IPV6, argv);
# endif
}
#endif

/* from ping6.c:
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Muuss.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. <BSD Advertising Clause omitted per the July 22, 1999 licensing change
 *		ftp://ftp.cs.berkeley.edu/pub/4bsd/README.Impt.License.Change>
 *
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
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
