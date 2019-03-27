/*
 * Copyright (c) 1988, 1989, 1991, 1994, 1995, 1996, 1997, 1998, 1999, 2000
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static const char copyright[] =
    "@(#) Copyright (c) 1988, 1989, 1991, 1994, 1995, 1996, 1997, 1998, 1999, 2000\n\
The Regents of the University of California.  All rights reserved.\n";
#if 0
static const char rcsid[] =
    "@(#)$Id: traceroute.c,v 1.68 2000/12/14 08:04:33 leres Exp $ (LBL)";
#endif
static const char rcsid[] =
    "$FreeBSD$";
#endif

/*
 * traceroute host  - trace the route ip packets follow going to "host".
 *
 * Attempt to trace the route an ip packet would follow to some
 * internet host.  We find out intermediate hops by launching probe
 * packets with a small ttl (time to live) then listening for an
 * icmp "time exceeded" reply from a gateway.  We start our probes
 * with a ttl of one and increase by one until we get an icmp "port
 * unreachable" (which means we got to "host") or hit a max (which
 * defaults to net.inet.ip.ttl hops & can be changed with the -m flag).
 * Three probes (change with -q flag) are sent at each ttl setting and
 * a line is printed showing the ttl, address of the gateway and
 * round trip time of each probe.  If the probe answers come from
 * different gateways, the address of each responding system will
 * be printed.  If there is no response within a 5 sec. timeout
 * interval (changed with the -w flag), a "*" is printed for that
 * probe.
 *
 * Probe packets are UDP format.  We don't want the destination
 * host to process them so the destination port is set to an
 * unlikely value (if some clod on the destination is using that
 * value, it can be changed with the -p flag).
 *
 * A sample use might be:
 *
 *     [yak 71]% traceroute nis.nsf.net.
 *     traceroute to nis.nsf.net (35.1.1.48), 64 hops max, 56 byte packet
 *      1  helios.ee.lbl.gov (128.3.112.1)  19 ms  19 ms  0 ms
 *      2  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  39 ms  19 ms
 *      3  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  39 ms  19 ms
 *      4  ccngw-ner-cc.Berkeley.EDU (128.32.136.23)  39 ms  40 ms  39 ms
 *      5  ccn-nerif22.Berkeley.EDU (128.32.168.22)  39 ms  39 ms  39 ms
 *      6  128.32.197.4 (128.32.197.4)  40 ms  59 ms  59 ms
 *      7  131.119.2.5 (131.119.2.5)  59 ms  59 ms  59 ms
 *      8  129.140.70.13 (129.140.70.13)  99 ms  99 ms  80 ms
 *      9  129.140.71.6 (129.140.71.6)  139 ms  239 ms  319 ms
 *     10  129.140.81.7 (129.140.81.7)  220 ms  199 ms  199 ms
 *     11  nic.merit.edu (35.1.1.48)  239 ms  239 ms  239 ms
 *
 * Note that lines 2 & 3 are the same.  This is due to a buggy
 * kernel on the 2nd hop system -- lbl-csam.arpa -- that forwards
 * packets with a zero ttl.
 *
 * A more interesting example is:
 *
 *     [yak 72]% traceroute allspice.lcs.mit.edu.
 *     traceroute to allspice.lcs.mit.edu (18.26.0.115), 64 hops max
 *      1  helios.ee.lbl.gov (128.3.112.1)  0 ms  0 ms  0 ms
 *      2  lilac-dmc.Berkeley.EDU (128.32.216.1)  19 ms  19 ms  19 ms
 *      3  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  19 ms  19 ms
 *      4  ccngw-ner-cc.Berkeley.EDU (128.32.136.23)  19 ms  39 ms  39 ms
 *      5  ccn-nerif22.Berkeley.EDU (128.32.168.22)  20 ms  39 ms  39 ms
 *      6  128.32.197.4 (128.32.197.4)  59 ms  119 ms  39 ms
 *      7  131.119.2.5 (131.119.2.5)  59 ms  59 ms  39 ms
 *      8  129.140.70.13 (129.140.70.13)  80 ms  79 ms  99 ms
 *      9  129.140.71.6 (129.140.71.6)  139 ms  139 ms  159 ms
 *     10  129.140.81.7 (129.140.81.7)  199 ms  180 ms  300 ms
 *     11  129.140.72.17 (129.140.72.17)  300 ms  239 ms  239 ms
 *     12  * * *
 *     13  128.121.54.72 (128.121.54.72)  259 ms  499 ms  279 ms
 *     14  * * *
 *     15  * * *
 *     16  * * *
 *     17  * * *
 *     18  ALLSPICE.LCS.MIT.EDU (18.26.0.115)  339 ms  279 ms  279 ms
 *
 * (I start to see why I'm having so much trouble with mail to
 * MIT.)  Note that the gateways 12, 14, 15, 16 & 17 hops away
 * either don't send ICMP "time exceeded" messages or send them
 * with a ttl too small to reach us.  14 - 17 are running the
 * MIT C Gateway code that doesn't send "time exceeded"s.  God
 * only knows what's going on with 12.
 *
 * The silent gateway 12 in the above may be the result of a bug in
 * the 4.[23]BSD network code (and its derivatives):  4.x (x <= 3)
 * sends an unreachable message using whatever ttl remains in the
 * original datagram.  Since, for gateways, the remaining ttl is
 * zero, the icmp "time exceeded" is guaranteed to not make it back
 * to us.  The behavior of this bug is slightly more interesting
 * when it appears on the destination system:
 *
 *      1  helios.ee.lbl.gov (128.3.112.1)  0 ms  0 ms  0 ms
 *      2  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  19 ms  39 ms
 *      3  lilac-dmc.Berkeley.EDU (128.32.216.1)  19 ms  39 ms  19 ms
 *      4  ccngw-ner-cc.Berkeley.EDU (128.32.136.23)  39 ms  40 ms  19 ms
 *      5  ccn-nerif35.Berkeley.EDU (128.32.168.35)  39 ms  39 ms  39 ms
 *      6  csgw.Berkeley.EDU (128.32.133.254)  39 ms  59 ms  39 ms
 *      7  * * *
 *      8  * * *
 *      9  * * *
 *     10  * * *
 *     11  * * *
 *     12  * * *
 *     13  rip.Berkeley.EDU (128.32.131.22)  59 ms !  39 ms !  39 ms !
 *
 * Notice that there are 12 "gateways" (13 is the final
 * destination) and exactly the last half of them are "missing".
 * What's really happening is that rip (a Sun-3 running Sun OS3.5)
 * is using the ttl from our arriving datagram as the ttl in its
 * icmp reply.  So, the reply will time out on the return path
 * (with no notice sent to anyone since icmp's aren't sent for
 * icmp's) until we probe with a ttl that's at least twice the path
 * length.  I.e., rip is really only 7 hops away.  A reply that
 * returns with a ttl of 1 is a clue this problem exists.
 * Traceroute prints a "!" after the time if the ttl is <= 1.
 * Since vendors ship a lot of obsolete (DEC's Ultrix, Sun 3.x) or
 * non-standard (HPUX) software, expect to see this problem
 * frequently and/or take care picking the target host of your
 * probes.
 *
 * Other possible annotations after the time are !H, !N, !P (got a host,
 * network or protocol unreachable, respectively), !S or !F (source
 * route failed or fragmentation needed -- neither of these should
 * ever occur and the associated gateway is busted if you see one).  If
 * almost all the probes result in some kind of unreachable, traceroute
 * will give up and exit.
 *
 * Notes
 * -----
 * This program must be run by root or be setuid.  (I suggest that
 * you *don't* make it setuid -- casual use could result in a lot
 * of unnecessary traffic on our poor, congested nets.)
 *
 * This program requires a kernel mod that does not appear in any
 * system available from Berkeley:  A raw ip socket using proto
 * IPPROTO_RAW must interpret the data sent as an ip datagram (as
 * opposed to data to be wrapped in a ip datagram).  See the README
 * file that came with the source to this program for a description
 * of the mods I made to /sys/netinet/raw_ip.c.  Your mileage may
 * vary.  But, again, ANY 4.x (x < 4) BSD KERNEL WILL HAVE TO BE
 * MODIFIED TO RUN THIS PROGRAM.
 *
 * The udp port usage may appear bizarre (well, ok, it is bizarre).
 * The problem is that an icmp message only contains 8 bytes of
 * data from the original datagram.  8 bytes is the size of a udp
 * header so, if we want to associate replies with the original
 * datagram, the necessary information must be encoded into the
 * udp header (the ip id could be used but there's no way to
 * interlock with the kernel's assignment of ip id's and, anyway,
 * it would have taken a lot more kernel hacking to allow this
 * code to set the ip id).  So, to allow two or more users to
 * use traceroute simultaneously, we use this task's pid as the
 * source port (the high bit is set to move the port number out
 * of the "likely" range).  To keep track of which probe is being
 * replied to (so times and/or hop counts don't get confused by a
 * reply that was delayed in transit), we increment the destination
 * port number before each probe.
 *
 * Don't use this as a coding example.  I was trying to find a
 * routing problem and this code sort-of popped out after 48 hours
 * without sleep.  I was amazed it ever compiled, much less ran.
 *
 * I stole the idea for this program from Steve Deering.  Since
 * the first release, I've learned that had I attended the right
 * IETF working group meetings, I also could have stolen it from Guy
 * Almes or Matt Mathis.  I don't know (or care) who came up with
 * the idea first.  I envy the originators' perspicacity and I'm
 * glad they didn't keep the idea a secret.
 *
 * Tim Seaver, Ken Adelman and C. Philip Wood provided bug fixes and/or
 * enhancements to the original distribution.
 *
 * I've hacked up a round-trip-route version of this that works by
 * sending a loose-source-routed udp datagram through the destination
 * back to yourself.  Unfortunately, SO many gateways botch source
 * routing, the thing is almost worthless.  Maybe one day...
 *
 *  -- Van Jacobson (van@ee.lbl.gov)
 *     Tue Dec 20 03:50:13 PST 1988
 */

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#include <sys/time.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/sctp.h>
#include <netinet/sctp_header.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>

#include <arpa/inet.h>

#ifdef WITH_CASPER
#include <libcasper.h>
#include <casper/cap_dns.h>
#endif

#ifdef	IPSEC
#include <net/route.h>
#include <netipsec/ipsec.h>	/* XXX */
#endif	/* IPSEC */

#include <ctype.h>
#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <memory.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* rfc1716 */
#ifndef ICMP_UNREACH_FILTER_PROHIB
#define ICMP_UNREACH_FILTER_PROHIB	13	/* admin prohibited filter */
#endif
#ifndef ICMP_UNREACH_HOST_PRECEDENCE
#define ICMP_UNREACH_HOST_PRECEDENCE	14	/* host precedence violation */
#endif
#ifndef ICMP_UNREACH_PRECEDENCE_CUTOFF
#define ICMP_UNREACH_PRECEDENCE_CUTOFF	15	/* precedence cutoff */
#endif

#include "findsaddr.h"
#include "ifaddrlist.h"
#include "as.h"
#include "traceroute.h"

/* Maximum number of gateways (include room for one noop) */
#define NGATEWAYS ((int)((MAX_IPOPTLEN - IPOPT_MINOFF - 1) / sizeof(u_int32_t)))

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

#define Fprintf (void)fprintf
#define Printf (void)printf

/* What a GRE packet header looks like */
struct grehdr {
	u_int16_t   flags;
	u_int16_t   proto;
	u_int16_t   length;	/* PPTP version of these fields */
	u_int16_t   callId;
};
#ifndef IPPROTO_GRE
#define IPPROTO_GRE	47
#endif

/* For GRE, we prepare what looks like a PPTP packet */
#define GRE_PPTP_PROTO	0x880b

/* Host name and address list */
struct hostinfo {
	char *name;
	int n;
	u_int32_t *addrs;
};

/* Data section of the probe packet */
struct outdata {
	u_char seq;		/* sequence number of this packet */
	u_char ttl;		/* ttl packet left with */
	struct timeval tv;	/* time packet left */
};

#ifndef HAVE_ICMP_NEXTMTU
/* Path MTU Discovery (RFC1191) */
struct my_pmtu {
	u_short ipm_void;
	u_short ipm_nextmtu;
};
#endif

u_char	packet[512];		/* last inbound (icmp) packet */

struct ip *outip;		/* last output ip packet */
u_char *outp;		/* last output inner protocol packet */

struct ip *hip = NULL;		/* Quoted IP header */
int hiplen = 0;

/* loose source route gateway list (including room for final destination) */
u_int32_t gwlist[NGATEWAYS + 1];

int s;				/* receive (icmp) socket file descriptor */
int sndsock;			/* send (udp) socket file descriptor */

struct sockaddr whereto;	/* Who to try to reach */
struct sockaddr wherefrom;	/* Who we are */
int packlen;			/* total length of packet */
int protlen;			/* length of protocol part of packet */
int minpacket;			/* min ip packet size */
int maxpacket = 32 * 1024;	/* max ip packet size */
int pmtu;			/* Path MTU Discovery (RFC1191) */
u_int pausemsecs;

char *prog;
char *source;
char *hostname;
char *device;
static const char devnull[] = "/dev/null";

int nprobes = -1;
int max_ttl;
int first_ttl = 1;
u_short ident;
u_short port;			/* protocol specific base "port" */

int options;			/* socket options */
int verbose;
int waittime = 5;		/* time to wait for response (in seconds) */
int nflag;			/* print addresses numerically */
int as_path;			/* print as numbers for each hop */
char *as_server = NULL;
void *asn;
#ifdef CANT_HACK_IPCKSUM
int doipcksum = 0;		/* don't calculate ip checksums by default */
#else
int doipcksum = 1;		/* calculate ip checksums by default */
#endif
int optlen;			/* length of ip options */
int fixedPort = 0;		/* Use fixed destination port for TCP and UDP */
int printdiff = 0;		/* Print the difference between sent and quoted */

extern int optind;
extern int opterr;
extern char *optarg;

#ifdef WITH_CASPER
static cap_channel_t *capdns;
#endif

/* Forwards */
double	deltaT(struct timeval *, struct timeval *);
void	freehostinfo(struct hostinfo *);
void	getaddr(u_int32_t *, char *);
struct	hostinfo *gethostinfo(char *);
u_short	in_cksum(u_short *, int);
u_int32_t sctp_crc32c(const void *, u_int32_t);
char	*inetname(struct in_addr);
int	main(int, char **);
u_short p_cksum(struct ip *, u_short *, int, int);
int	packet_ok(u_char *, int, struct sockaddr_in *, int);
char	*pr_type(u_char);
void	print(u_char *, int, struct sockaddr_in *);
#ifdef	IPSEC
int	setpolicy __P((int so, char *policy));
#endif
void	send_probe(int, int);
struct outproto *setproto(char *);
int	str2val(const char *, const char *, int, int);
void	tvsub(struct timeval *, struct timeval *);
void usage(void);
int	wait_for_reply(int, struct sockaddr_in *, const struct timeval *);
void pkt_compare(const u_char *, int, const u_char *, int);
#ifndef HAVE_USLEEP
int	usleep(u_int);
#endif

void	udp_prep(struct outdata *);
int	udp_check(const u_char *, int);
void	udplite_prep(struct outdata *);
int	udplite_check(const u_char *, int);
void	tcp_prep(struct outdata *);
int	tcp_check(const u_char *, int);
void	sctp_prep(struct outdata *);
int	sctp_check(const u_char *, int);
void	gre_prep(struct outdata *);
int	gre_check(const u_char *, int);
void	gen_prep(struct outdata *);
int	gen_check(const u_char *, int);
void	icmp_prep(struct outdata *);
int	icmp_check(const u_char *, int);

/* Descriptor structure for each outgoing protocol we support */
struct outproto {
	char	*name;		/* name of protocol */
	const char *key;	/* An ascii key for the bytes of the header */
	u_char	num;		/* IP protocol number */
	u_short	hdrlen;		/* max size of protocol header */
	u_short	port;		/* default base protocol-specific "port" */
	void	(*prepare)(struct outdata *);
				/* finish preparing an outgoing packet */
	int	(*check)(const u_char *, int);
				/* check an incoming packet */
};

/* List of supported protocols. The first one is the default. The last
   one is the handler for generic protocols not explicitly listed. */
struct	outproto protos[] = {
	{
		"udp",
		"spt dpt len sum",
		IPPROTO_UDP,
		sizeof(struct udphdr),
		32768 + 666,
		udp_prep,
		udp_check
	},
	{
		"udplite",
		"spt dpt cov sum",
		IPPROTO_UDPLITE,
		sizeof(struct udphdr),
		32768 + 666,
		udplite_prep,
		udplite_check
	},
	{
		"tcp",
		"spt dpt seq     ack     xxflwin sum urp",
		IPPROTO_TCP,
		sizeof(struct tcphdr),
		32768 + 666,
		tcp_prep,
		tcp_check
	},
	{
		"sctp",
		"spt dpt vtag    crc     tyfllen tyfllen ",
		IPPROTO_SCTP,
		sizeof(struct sctphdr),
		32768 + 666,
		sctp_prep,
		sctp_check
	},
	{
		"gre",
		"flg pro len clid",
		IPPROTO_GRE,
		sizeof(struct grehdr),
		GRE_PPTP_PROTO,
		gre_prep,
		gre_check
	},
	{
		"icmp",
		"typ cod sum ",
		IPPROTO_ICMP,
		sizeof(struct icmp),
		0,
		icmp_prep,
		icmp_check
	},
	{
		NULL,
		"",
		0,
		2 * sizeof(u_short),
		0,
		gen_prep,
		gen_check
	},
};
struct	outproto *proto = &protos[0];

const char *ip_hdr_key = "vhtslen id  off tlprsum srcip   dstip   opts";

int
main(int argc, char **argv)
{
	register int op, code, n;
	register char *cp;
	register const char *err;
	register u_int32_t *ap;
	register struct sockaddr_in *from = (struct sockaddr_in *)&wherefrom;
	register struct sockaddr_in *to = (struct sockaddr_in *)&whereto;
	register struct hostinfo *hi;
	int on = 1;
	register struct protoent *pe;
	register int ttl, probe, i;
	register int seq = 0;
	int tos = 0, settos = 0;
	register int lsrr = 0;
	register u_short off = 0;
	struct ifaddrlist *al;
	char errbuf[132];
	int requestPort = -1;
	int sump = 0;
	int sockerrno;
#ifdef WITH_CASPER
	const char *types[] = { "NAME2ADDR", "ADDR2NAME" };
	int families[1];
	cap_channel_t *casper;
#endif
	cap_rights_t rights;
	bool cansandbox;

	/* Insure the socket fds won't be 0, 1 or 2 */
	if (open(devnull, O_RDONLY) < 0 ||
	    open(devnull, O_RDONLY) < 0 ||
	    open(devnull, O_RDONLY) < 0) {
		Fprintf(stderr, "%s: open \"%s\": %s\n",
		    prog, devnull, strerror(errno));
		exit(1);
	}
	/*
	 * Do the setuid-required stuff first, then lose priveleges ASAP.
	 * Do error checking for these two calls where they appeared in
	 * the original code.
	 */
	cp = "icmp";
	pe = getprotobyname(cp);
	if (pe) {
		if ((s = socket(AF_INET, SOCK_RAW, pe->p_proto)) < 0)
			sockerrno = errno;
		else if ((sndsock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
			sockerrno = errno;
	}

	if (setuid(getuid()) != 0) {
		perror("setuid()");
		exit(1);
	}

#ifdef WITH_CASPER
	casper = cap_init();
	if (casper == NULL)
		errx(1, "unable to create casper process");
	capdns = cap_service_open(casper, "system.dns");
	if (capdns == NULL)
		errx(1, "unable to open system.dns service");
	if (cap_dns_type_limit(capdns, types, 2) < 0)
		errx(1, "unable to limit access to system.dns service");
	families[0] = AF_INET;
	if (cap_dns_family_limit(capdns, families, 1) < 0)
		errx(1, "unable to limit access to system.dns service");
#endif /* WITH_CASPER */

#ifdef IPCTL_DEFTTL
	{
		int mib[4] = { CTL_NET, PF_INET, IPPROTO_IP, IPCTL_DEFTTL };
		size_t sz = sizeof(max_ttl);

		if (sysctl(mib, 4, &max_ttl, &sz, NULL, 0) == -1) {
			perror("sysctl(net.inet.ip.ttl)");
			exit(1);
		}
	}
#else /* !IPCTL_DEFTTL */
	max_ttl = 30;
#endif

#ifdef WITH_CASPER
	cap_close(casper);
#endif

	if (argv[0] == NULL)
		prog = "traceroute";
	else if ((cp = strrchr(argv[0], '/')) != NULL)
		prog = cp + 1;
	else
		prog = argv[0];

	opterr = 0;
	while ((op = getopt(argc, argv, "aA:edDFInrSvxf:g:i:M:m:P:p:q:s:t:w:z:")) != EOF)
		switch (op) {
		case 'a':
			as_path = 1;
			break;

		case 'A':
			as_path = 1;
			as_server = optarg;
			break;

		case 'd':
			options |= SO_DEBUG;
			break;

		case 'D':
			printdiff = 1;
			break;

		case 'e':
			fixedPort = 1;
			break;

		case 'f':
		case 'M':	/* FreeBSD compat. */
			first_ttl = str2val(optarg, "first ttl", 1, 255);
			break;

		case 'F':
			off = IP_DF;
			break;

		case 'g':
			if (lsrr >= NGATEWAYS) {
				Fprintf(stderr,
				    "%s: No more than %d gateways\n",
				    prog, NGATEWAYS);
				exit(1);
			}
			getaddr(gwlist + lsrr, optarg);
			++lsrr;
			break;

		case 'i':
			device = optarg;
			break;

		case 'I':
			proto = setproto("icmp");
			break;

		case 'm':
			max_ttl = str2val(optarg, "max ttl", 1, 255);
			break;

		case 'n':
			++nflag;
			break;

		case 'P':
			proto = setproto(optarg);
			break;

		case 'p':
			requestPort = (u_short)str2val(optarg, "port",
			    1, (1 << 16) - 1);
			break;

		case 'q':
			nprobes = str2val(optarg, "nprobes", 1, -1);
			break;

		case 'r':
			options |= SO_DONTROUTE;
			break;

		case 's':
			/*
			 * set the ip source address of the outbound
			 * probe (e.g., on a multi-homed host).
			 */
			source = optarg;
			break;

		case 'S':
			sump = 1;
			break;

		case 't':
			tos = str2val(optarg, "tos", 0, 255);
			++settos;
			break;

		case 'v':
			++verbose;
			break;

		case 'x':
			doipcksum = (doipcksum == 0);
			break;

		case 'w':
			waittime = str2val(optarg, "wait time",
			    1, 24 * 60 * 60);
			break;

		case 'z':
			pausemsecs = str2val(optarg, "pause msecs",
			    0, 60 * 60 * 1000);
			break;

		default:
			usage();
		}

	/* Set requested port, if any, else default for this protocol */
	port = (requestPort != -1) ? requestPort : proto->port;

	if (nprobes == -1)
		nprobes = printdiff ? 1 : 3;

	if (first_ttl > max_ttl) {
		Fprintf(stderr,
		    "%s: first ttl (%d) may not be greater than max ttl (%d)\n",
		    prog, first_ttl, max_ttl);
		exit(1);
	}

	if (!doipcksum)
		Fprintf(stderr, "%s: Warning: ip checksums disabled\n", prog);

	if (lsrr > 0)
		optlen = (lsrr + 1) * sizeof(gwlist[0]);
	minpacket = sizeof(*outip) + proto->hdrlen + optlen;
	if (minpacket > 40)
		packlen = minpacket;
	else
		packlen = 40;

	/* Process destination and optional packet size */
	switch (argc - optind) {

	case 2:
		packlen = str2val(argv[optind + 1],
		    "packet length", minpacket, maxpacket);
		/* Fall through */

	case 1:
		hostname = argv[optind];
		hi = gethostinfo(hostname);
		setsin(to, hi->addrs[0]);
		if (hi->n > 1)
			Fprintf(stderr,
		    "%s: Warning: %s has multiple addresses; using %s\n",
				prog, hostname, inet_ntoa(to->sin_addr));
		hostname = hi->name;
		hi->name = NULL;
		freehostinfo(hi);
		break;

	default:
		usage();
	}

#ifdef HAVE_SETLINEBUF
	setlinebuf (stdout);
#else
	setvbuf(stdout, NULL, _IOLBF, 0);
#endif

	protlen = packlen - sizeof(*outip) - optlen;
	if ((proto->num == IPPROTO_SCTP) && (packlen & 3)) {
		Fprintf(stderr, "%s: packet length must be a multiple of 4\n",
		    prog);
		exit(1);
	}

	outip = (struct ip *)malloc((unsigned)packlen);
	if (outip == NULL) {
		Fprintf(stderr, "%s: malloc: %s\n", prog, strerror(errno));
		exit(1);
	}
	memset((char *)outip, 0, packlen);

	outip->ip_v = IPVERSION;
	if (settos)
		outip->ip_tos = tos;
#ifdef BYTESWAP_IP_HDR
	outip->ip_len = htons(packlen);
	outip->ip_off = htons(off);
#else
	outip->ip_len = packlen;
	outip->ip_off = off;
#endif
	outip->ip_p = proto->num;
	outp = (u_char *)(outip + 1);
#ifdef HAVE_RAW_OPTIONS
	if (lsrr > 0) {
		register u_char *optlist;

		optlist = outp;
		outp += optlen;

		/* final hop */
		gwlist[lsrr] = to->sin_addr.s_addr;

		outip->ip_dst.s_addr = gwlist[0];

		/* force 4 byte alignment */
		optlist[0] = IPOPT_NOP;
		/* loose source route option */
		optlist[1] = IPOPT_LSRR;
		i = lsrr * sizeof(gwlist[0]);
		optlist[2] = i + 3;
		/* Pointer to LSRR addresses */
		optlist[3] = IPOPT_MINOFF;
		memcpy(optlist + 4, gwlist + 1, i);
	} else
#endif
		outip->ip_dst = to->sin_addr;

	outip->ip_hl = (outp - (u_char *)outip) >> 2;
	ident = (getpid() & 0xffff) | 0x8000;

	if (pe == NULL) {
		Fprintf(stderr, "%s: unknown protocol %s\n", prog, cp);
		exit(1);
	}
	if (s < 0) {
		errno = sockerrno;
		Fprintf(stderr, "%s: icmp socket: %s\n", prog, strerror(errno));
		exit(1);
	}
	if (options & SO_DEBUG)
		(void)setsockopt(s, SOL_SOCKET, SO_DEBUG, (char *)&on,
		    sizeof(on));
	if (options & SO_DONTROUTE)
		(void)setsockopt(s, SOL_SOCKET, SO_DONTROUTE, (char *)&on,
		    sizeof(on));

#if	defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
	if (setpolicy(s, "in bypass") < 0)
		errx(1, "%s", ipsec_strerror());

	if (setpolicy(s, "out bypass") < 0)
		errx(1, "%s", ipsec_strerror());
#endif	/* defined(IPSEC) && defined(IPSEC_POLICY_IPSEC) */

	if (sndsock < 0) {
		errno = sockerrno;
		Fprintf(stderr, "%s: raw socket: %s\n", prog, strerror(errno));
		exit(1);
	}

#if defined(IP_OPTIONS) && !defined(HAVE_RAW_OPTIONS)
	if (lsrr > 0) {
		u_char optlist[MAX_IPOPTLEN];

		cp = "ip";
		if ((pe = getprotobyname(cp)) == NULL) {
			Fprintf(stderr, "%s: unknown protocol %s\n", prog, cp);
			exit(1);
		}

		/* final hop */
		gwlist[lsrr] = to->sin_addr.s_addr;
		++lsrr;

		/* force 4 byte alignment */
		optlist[0] = IPOPT_NOP;
		/* loose source route option */
		optlist[1] = IPOPT_LSRR;
		i = lsrr * sizeof(gwlist[0]);
		optlist[2] = i + 3;
		/* Pointer to LSRR addresses */
		optlist[3] = IPOPT_MINOFF;
		memcpy(optlist + 4, gwlist, i);

		if ((setsockopt(sndsock, pe->p_proto, IP_OPTIONS,
		    (char *)optlist, i + sizeof(gwlist[0]))) < 0) {
			Fprintf(stderr, "%s: IP_OPTIONS: %s\n",
			    prog, strerror(errno));
			exit(1);
		    }
	}
#endif

#ifdef SO_SNDBUF
	if (setsockopt(sndsock, SOL_SOCKET, SO_SNDBUF, (char *)&packlen,
	    sizeof(packlen)) < 0) {
		Fprintf(stderr, "%s: SO_SNDBUF: %s\n", prog, strerror(errno));
		exit(1);
	}
#endif
#ifdef IP_HDRINCL
	if (setsockopt(sndsock, IPPROTO_IP, IP_HDRINCL, (char *)&on,
	    sizeof(on)) < 0) {
		Fprintf(stderr, "%s: IP_HDRINCL: %s\n", prog, strerror(errno));
		exit(1);
	}
#else
#ifdef IP_TOS
	if (settos && setsockopt(sndsock, IPPROTO_IP, IP_TOS,
	    (char *)&tos, sizeof(tos)) < 0) {
		Fprintf(stderr, "%s: setsockopt tos %d: %s\n",
		    prog, tos, strerror(errno));
		exit(1);
	}
#endif
#endif
	if (options & SO_DEBUG)
		(void)setsockopt(sndsock, SOL_SOCKET, SO_DEBUG, (char *)&on,
		    sizeof(on));
	if (options & SO_DONTROUTE)
		(void)setsockopt(sndsock, SOL_SOCKET, SO_DONTROUTE, (char *)&on,
		    sizeof(on));

	/* Get the interface address list */
	n = ifaddrlist(&al, errbuf);
	if (n < 0) {
		Fprintf(stderr, "%s: ifaddrlist: %s\n", prog, errbuf);
		exit(1);
	}
	if (n == 0) {
		Fprintf(stderr,
		    "%s: Can't find any network interfaces\n", prog);
		exit(1);
	}

	/* Look for a specific device */
	if (device != NULL) {
		for (i = n; i > 0; --i, ++al)
			if (strcmp(device, al->device) == 0)
				break;
		if (i <= 0) {
			Fprintf(stderr, "%s: Can't find interface %.32s\n",
			    prog, device);
			exit(1);
		}
	}

	/* Determine our source address */
	if (source == NULL) {
		/*
		 * If a device was specified, use the interface address.
		 * Otherwise, try to determine our source address.
		 */
		if (device != NULL)
			setsin(from, al->addr);
		else if ((err = findsaddr(to, from)) != NULL) {
			Fprintf(stderr, "%s: findsaddr: %s\n",
			    prog, err);
			exit(1);
		}
	} else {
		hi = gethostinfo(source);
		source = hi->name;
		hi->name = NULL;
		/*
		 * If the device was specified make sure it
		 * corresponds to the source address specified.
		 * Otherwise, use the first address (and warn if
		 * there are more than one).
		 */
		if (device != NULL) {
			for (i = hi->n, ap = hi->addrs; i > 0; --i, ++ap)
				if (*ap == al->addr)
					break;
			if (i <= 0) {
				Fprintf(stderr,
				    "%s: %s is not on interface %.32s\n",
				    prog, source, device);
				exit(1);
			}
			setsin(from, *ap);
		} else {
			setsin(from, hi->addrs[0]);
			if (hi->n > 1)
				Fprintf(stderr,
			"%s: Warning: %s has multiple addresses; using %s\n",
				    prog, source, inet_ntoa(from->sin_addr));
		}
		freehostinfo(hi);
	}

	outip->ip_src = from->sin_addr;

	/* Check the source address (-s), if any, is valid */
	if (bind(sndsock, (struct sockaddr *)from, sizeof(*from)) < 0) {
		Fprintf(stderr, "%s: bind: %s\n",
		    prog, strerror(errno));
		exit (1);
	}

	if (as_path) {
		asn = as_setup(as_server);
		if (asn == NULL) {
			Fprintf(stderr, "%s: as_setup failed, AS# lookups"
			    " disabled\n", prog);
			(void)fflush(stderr);
			as_path = 0;
		}
	}

	if (connect(sndsock, (struct sockaddr *)&whereto,
	    sizeof(whereto)) != 0) {
		Fprintf(stderr, "%s: connect: %s\n", prog, strerror(errno));
		exit(1);
	}

#ifdef WITH_CASPER
	cansandbox = true;
#else
	if (nflag)
		cansandbox = true;
	else
		cansandbox = false;
#endif

	caph_cache_catpages();

	/*
	 * Here we enter capability mode. Further down access to global
	 * namespaces (e.g filesystem) is restricted (see capsicum(4)).
	 * We must connect(2) our socket before this point.
	 */
	if (cansandbox && cap_enter() < 0) {
		if (errno != ENOSYS) {
			Fprintf(stderr, "%s: cap_enter: %s\n", prog,
			    strerror(errno));
			exit(1);
		} else {
			cansandbox = false;
		}
	}

	cap_rights_init(&rights, CAP_SEND, CAP_SETSOCKOPT);
	if (cansandbox && cap_rights_limit(sndsock, &rights) < 0) {
		Fprintf(stderr, "%s: cap_rights_limit sndsock: %s\n", prog,
		    strerror(errno));
		exit(1);
	}

	cap_rights_init(&rights, CAP_RECV, CAP_EVENT);
	if (cansandbox && cap_rights_limit(s, &rights) < 0) {
		Fprintf(stderr, "%s: cap_rights_limit s: %s\n", prog,
		    strerror(errno));
		exit(1);
	}

#if	defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
	if (setpolicy(sndsock, "in bypass") < 0)
		errx(1, "%s", ipsec_strerror());

	if (setpolicy(sndsock, "out bypass") < 0)
		errx(1, "%s", ipsec_strerror());
#endif	/* defined(IPSEC) && defined(IPSEC_POLICY_IPSEC) */

	Fprintf(stderr, "%s to %s (%s)",
	    prog, hostname, inet_ntoa(to->sin_addr));
	if (source)
		Fprintf(stderr, " from %s", source);
	Fprintf(stderr, ", %d hops max, %d byte packets\n", max_ttl, packlen);
	(void)fflush(stderr);

	for (ttl = first_ttl; ttl <= max_ttl; ++ttl) {
		u_int32_t lastaddr = 0;
		int gotlastaddr = 0;
		int got_there = 0;
		int unreachable = 0;
		int sentfirst = 0;
		int loss;

		Printf("%2d ", ttl);
		for (probe = 0, loss = 0; probe < nprobes; ++probe) {
			register int cc;
			struct timeval t1, t2;
			register struct ip *ip;
			struct outdata outdata;

			if (sentfirst && pausemsecs > 0)
				usleep(pausemsecs * 1000);
			/* Prepare outgoing data */
			outdata.seq = ++seq;
			outdata.ttl = ttl;

			/* Avoid alignment problems by copying bytewise: */
			(void)gettimeofday(&t1, NULL);
			memcpy(&outdata.tv, &t1, sizeof(outdata.tv));

			/* Finalize and send packet */
			(*proto->prepare)(&outdata);
			send_probe(seq, ttl);
			++sentfirst;

			/* Wait for a reply */
			while ((cc = wait_for_reply(s, from, &t1)) != 0) {
				double T;
				int precis;

				(void)gettimeofday(&t2, NULL);
				i = packet_ok(packet, cc, from, seq);
				/* Skip short packet */
				if (i == 0)
					continue;
				if (!gotlastaddr ||
				    from->sin_addr.s_addr != lastaddr) {
					if (gotlastaddr) printf("\n   ");
					print(packet, cc, from);
					lastaddr = from->sin_addr.s_addr;
					++gotlastaddr;
				}
				T = deltaT(&t1, &t2);
#ifdef SANE_PRECISION
				if (T >= 1000.0)
					precis = 0;
				else if (T >= 100.0)
					precis = 1;
				else if (T >= 10.0)
					precis = 2;
				else
#endif
					precis = 3;
				Printf("  %.*f ms", precis, T);
				if (printdiff) {
					Printf("\n");
					Printf("%*.*s%s\n",
					    -(outip->ip_hl << 3),
					    outip->ip_hl << 3,
					    ip_hdr_key,
					    proto->key);
					pkt_compare((void *)outip, packlen,
					    (void *)hip, hiplen);
				}
				if (i == -2) {
#ifndef ARCHAIC
					ip = (struct ip *)packet;
					if (ip->ip_ttl <= 1)
						Printf(" !");
#endif
					++got_there;
					break;
				}
				/* time exceeded in transit */
				if (i == -1)
					break;
				code = i - 1;
				switch (code) {

				case ICMP_UNREACH_PORT:
#ifndef ARCHAIC
					ip = (struct ip *)packet;
					if (ip->ip_ttl <= 1)
						Printf(" !");
#endif
					++got_there;
					break;

				case ICMP_UNREACH_NET:
					++unreachable;
					Printf(" !N");
					break;

				case ICMP_UNREACH_HOST:
					++unreachable;
					Printf(" !H");
					break;

				case ICMP_UNREACH_PROTOCOL:
					++got_there;
					Printf(" !P");
					break;

				case ICMP_UNREACH_NEEDFRAG:
					++unreachable;
					Printf(" !F-%d", pmtu);
					break;

				case ICMP_UNREACH_SRCFAIL:
					++unreachable;
					Printf(" !S");
					break;

				case ICMP_UNREACH_NET_UNKNOWN:
					++unreachable;
					Printf(" !U");
					break;

				case ICMP_UNREACH_HOST_UNKNOWN:
					++unreachable;
					Printf(" !W");
					break;

				case ICMP_UNREACH_ISOLATED:
					++unreachable;
					Printf(" !I");
					break;

				case ICMP_UNREACH_NET_PROHIB:
					++unreachable;
					Printf(" !A");
					break;

				case ICMP_UNREACH_HOST_PROHIB:
					++unreachable;
					Printf(" !Z");
					break;

				case ICMP_UNREACH_TOSNET:
					++unreachable;
					Printf(" !Q");
					break;

				case ICMP_UNREACH_TOSHOST:
					++unreachable;
					Printf(" !T");
					break;

				case ICMP_UNREACH_FILTER_PROHIB:
					++unreachable;
					Printf(" !X");
					break;

				case ICMP_UNREACH_HOST_PRECEDENCE:
					++unreachable;
					Printf(" !V");
					break;

				case ICMP_UNREACH_PRECEDENCE_CUTOFF:
					++unreachable;
					Printf(" !C");
					break;

				default:
					++unreachable;
					Printf(" !<%d>", code);
					break;
				}
				break;
			}
			if (cc == 0) {
				loss++;
				Printf(" *");
			}
			(void)fflush(stdout);
		}
		if (sump) {
			Printf(" (%d%% loss)", (loss * 100) / nprobes);
		}
		putchar('\n');
		if (got_there ||
		    (unreachable > 0 && unreachable >= nprobes - 1))
			break;
	}
	if (as_path)
		as_shutdown(asn);
	exit(0);
}

int
wait_for_reply(register int sock, register struct sockaddr_in *fromp,
    register const struct timeval *tp)
{
	fd_set *fdsp;
	size_t nfds;
	struct timeval now, wait;
	register int cc = 0;
	register int error;
	int fromlen = sizeof(*fromp);

	nfds = howmany(sock + 1, NFDBITS);
	if ((fdsp = malloc(nfds * sizeof(fd_mask))) == NULL)
		err(1, "malloc");
	memset(fdsp, 0, nfds * sizeof(fd_mask));
	FD_SET(sock, fdsp);

	wait.tv_sec = tp->tv_sec + waittime;
	wait.tv_usec = tp->tv_usec;
	(void)gettimeofday(&now, NULL);
	tvsub(&wait, &now);
	if (wait.tv_sec < 0) {
		wait.tv_sec = 0;
		wait.tv_usec = 1;
	}

	error = select(sock + 1, fdsp, NULL, NULL, &wait);
	if (error == -1 && errno == EINVAL) {
		Fprintf(stderr, "%s: botched select() args\n", prog);
		exit(1);
	}
	if (error > 0)
		cc = recvfrom(sock, (char *)packet, sizeof(packet), 0,
			    (struct sockaddr *)fromp, &fromlen);

	free(fdsp);
	return(cc);
}

void
send_probe(int seq, int ttl)
{
	register int cc;

	outip->ip_ttl = ttl;
	outip->ip_id = htons(ident + seq);

	/* XXX undocumented debugging hack */
	if (verbose > 1) {
		register const u_short *sp;
		register int nshorts, i;

		sp = (u_short *)outip;
		nshorts = (u_int)packlen / sizeof(u_short);
		i = 0;
		Printf("[ %d bytes", packlen);
		while (--nshorts >= 0) {
			if ((i++ % 8) == 0)
				Printf("\n\t");
			Printf(" %04x", ntohs(*sp++));
		}
		if (packlen & 1) {
			if ((i % 8) == 0)
				Printf("\n\t");
			Printf(" %02x", *(u_char *)sp);
		}
		Printf("]\n");
	}

#if !defined(IP_HDRINCL) && defined(IP_TTL)
	if (setsockopt(sndsock, IPPROTO_IP, IP_TTL,
	    (char *)&ttl, sizeof(ttl)) < 0) {
		Fprintf(stderr, "%s: setsockopt ttl %d: %s\n",
		    prog, ttl, strerror(errno));
		exit(1);
	}
#endif

	cc = send(sndsock, (char *)outip, packlen, 0);
	if (cc < 0 || cc != packlen)  {
		if (cc < 0)
			Fprintf(stderr, "%s: sendto: %s\n",
			    prog, strerror(errno));
		Printf("%s: wrote %s %d chars, ret=%d\n",
		    prog, hostname, packlen, cc);
		(void)fflush(stdout);
	}
}

#if	defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
int
setpolicy(so, policy)
	int so;
	char *policy;
{
	char *buf;

	buf = ipsec_set_policy(policy, strlen(policy));
	if (buf == NULL) {
		warnx("%s", ipsec_strerror());
		return -1;
	}
	(void)setsockopt(so, IPPROTO_IP, IP_IPSEC_POLICY,
		buf, ipsec_get_policylen(buf));

	free(buf);

	return 0;
}
#endif

double
deltaT(struct timeval *t1p, struct timeval *t2p)
{
	register double dt;

	dt = (double)(t2p->tv_sec - t1p->tv_sec) * 1000.0 +
	     (double)(t2p->tv_usec - t1p->tv_usec) / 1000.0;
	return (dt);
}

/*
 * Convert an ICMP "type" field to a printable string.
 */
char *
pr_type(register u_char t)
{
	static char *ttab[] = {
	"Echo Reply",	"ICMP 1",	"ICMP 2",	"Dest Unreachable",
	"Source Quench", "Redirect",	"ICMP 6",	"ICMP 7",
	"Echo",		"ICMP 9",	"ICMP 10",	"Time Exceeded",
	"Param Problem", "Timestamp",	"Timestamp Reply", "Info Request",
	"Info Reply"
	};

	if (t > 16)
		return("OUT-OF-RANGE");

	return(ttab[t]);
}

int
packet_ok(register u_char *buf, int cc, register struct sockaddr_in *from,
    register int seq)
{
	register struct icmp *icp;
	register u_char type, code;
	register int hlen;
#ifndef ARCHAIC
	register struct ip *ip;

	ip = (struct ip *) buf;
	hlen = ip->ip_hl << 2;
	if (cc < hlen + ICMP_MINLEN) {
		if (verbose)
			Printf("packet too short (%d bytes) from %s\n", cc,
				inet_ntoa(from->sin_addr));
		return (0);
	}
	cc -= hlen;
	icp = (struct icmp *)(buf + hlen);
#else
	icp = (struct icmp *)buf;
#endif
	type = icp->icmp_type;
	code = icp->icmp_code;
	/* Path MTU Discovery (RFC1191) */
	if (code != ICMP_UNREACH_NEEDFRAG)
		pmtu = 0;
	else {
#ifdef HAVE_ICMP_NEXTMTU
		pmtu = ntohs(icp->icmp_nextmtu);
#else
		pmtu = ntohs(((struct my_pmtu *)&icp->icmp_void)->ipm_nextmtu);
#endif
	}
	if (type == ICMP_ECHOREPLY
	    && proto->num == IPPROTO_ICMP
	    && (*proto->check)((u_char *)icp, (u_char)seq))
		return -2;
	if ((type == ICMP_TIMXCEED && code == ICMP_TIMXCEED_INTRANS) ||
	    type == ICMP_UNREACH) {
		u_char *inner;

		hip = &icp->icmp_ip;
		hiplen = ((u_char *)icp + cc) - (u_char *)hip;
		hlen = hip->ip_hl << 2;
		inner = (u_char *)((u_char *)hip + hlen);
		if (hlen + 16 <= cc
		    && hip->ip_p == proto->num
		    && (*proto->check)(inner, (u_char)seq))
			return (type == ICMP_TIMXCEED ? -1 : code + 1);
	}
#ifndef ARCHAIC
	if (verbose) {
		register int i;
		u_int32_t *lp = (u_int32_t *)&icp->icmp_ip;

		Printf("\n%d bytes from %s to ", cc, inet_ntoa(from->sin_addr));
		Printf("%s: icmp type %d (%s) code %d\n",
		    inet_ntoa(ip->ip_dst), type, pr_type(type), icp->icmp_code);
		for (i = 4; i <= cc - ICMP_MINLEN; i += sizeof(*lp))
			Printf("%2d: %8.8x\n", i, ntohl(*lp++));
	}
#endif
	return(0);
}

void
icmp_prep(struct outdata *outdata)
{
	struct icmp *const icmpheader = (struct icmp *) outp;

	icmpheader->icmp_type = ICMP_ECHO;
	icmpheader->icmp_id = htons(ident);
	icmpheader->icmp_seq = htons(outdata->seq);
	icmpheader->icmp_cksum = 0;
	icmpheader->icmp_cksum = in_cksum((u_short *)icmpheader, protlen);
	if (icmpheader->icmp_cksum == 0)
		icmpheader->icmp_cksum = 0xffff;
}

int
icmp_check(const u_char *data, int seq)
{
	struct icmp *const icmpheader = (struct icmp *) data;

	return (icmpheader->icmp_id == htons(ident)
	    && icmpheader->icmp_seq == htons(seq));
}

void
udp_prep(struct outdata *outdata)
{
	struct udphdr *const outudp = (struct udphdr *) outp;

	outudp->uh_sport = htons(ident + (fixedPort ? outdata->seq : 0));
	outudp->uh_dport = htons(port + (fixedPort ? 0 : outdata->seq));
	outudp->uh_ulen = htons((u_short)protlen);
	outudp->uh_sum = 0;
	if (doipcksum) {
	    u_short sum = p_cksum(outip, (u_short*)outudp, protlen, protlen);
	    outudp->uh_sum = (sum) ? sum : 0xffff;
	}

	return;
}

int
udp_check(const u_char *data, int seq)
{
	struct udphdr *const udp = (struct udphdr *) data;

	return (ntohs(udp->uh_sport) == ident + (fixedPort ? seq : 0) &&
	    ntohs(udp->uh_dport) == port + (fixedPort ? 0 : seq));
}

void
udplite_prep(struct outdata *outdata)
{
	struct udphdr *const outudp = (struct udphdr *) outp;

	outudp->uh_sport = htons(ident + (fixedPort ? outdata->seq : 0));
	outudp->uh_dport = htons(port + (fixedPort ? 0 : outdata->seq));
	outudp->uh_ulen = htons(8);
	outudp->uh_sum = 0;
	if (doipcksum) {
	    u_short sum = p_cksum(outip, (u_short*)outudp, protlen, 8);
	    outudp->uh_sum = (sum) ? sum : 0xffff;
	}

	return;
}

int
udplite_check(const u_char *data, int seq)
{
	struct udphdr *const udp = (struct udphdr *) data;

	return (ntohs(udp->uh_sport) == ident + (fixedPort ? seq : 0) &&
	    ntohs(udp->uh_dport) == port + (fixedPort ? 0 : seq));
}

void
tcp_prep(struct outdata *outdata)
{
	struct tcphdr *const tcp = (struct tcphdr *) outp;

	tcp->th_sport = htons(ident);
	tcp->th_dport = htons(port + (fixedPort ? 0 : outdata->seq));
	tcp->th_seq = (tcp->th_sport << 16) | tcp->th_dport;
	tcp->th_ack = 0;
	tcp->th_off = 5;
	tcp->th_flags = TH_SYN;
	tcp->th_sum = 0;

	if (doipcksum)
	    tcp->th_sum = p_cksum(outip, (u_short*)tcp, protlen, protlen);
}

int
tcp_check(const u_char *data, int seq)
{
	struct tcphdr *const tcp = (struct tcphdr *) data;

	return (ntohs(tcp->th_sport) == ident
	    && ntohs(tcp->th_dport) == port + (fixedPort ? 0 : seq)
	    && tcp->th_seq == (tcp_seq)((tcp->th_sport << 16) | tcp->th_dport));
}

void
sctp_prep(struct outdata *outdata)
{
	struct sctphdr *const sctp = (struct sctphdr *) outp;
	struct sctp_chunkhdr *chk;
	struct sctp_init_chunk *init;
	struct sctp_paramhdr *param;

	sctp->src_port = htons(ident);
	sctp->dest_port = htons(port + (fixedPort ? 0 : outdata->seq));
	if (protlen >= (int)(sizeof(struct sctphdr) +
	    sizeof(struct sctp_init_chunk))) {
		sctp->v_tag = 0;
	} else {
		sctp->v_tag = (sctp->src_port << 16) | sctp->dest_port;
	}
	sctp->checksum = htonl(0);
	if (protlen >= (int)(sizeof(struct sctphdr) +
	    sizeof(struct sctp_init_chunk))) {
		/*
		 * Send a packet containing an INIT chunk. This works
		 * better in case of firewalls on the path, but
		 * results in a probe packet containing at least
		 * 32 bytes of payload. For shorter payloads, use
		 * SHUTDOWN-ACK chunks.
		 */
		init = (struct sctp_init_chunk *)(sctp + 1);
		init->ch.chunk_type = SCTP_INITIATION;
		init->ch.chunk_flags = 0;
		init->ch.chunk_length = htons((u_int16_t)(protlen -
		    sizeof(struct sctphdr)));
		init->init.initiate_tag = (sctp->src_port << 16) |
		    sctp->dest_port;
		init->init.a_rwnd = htonl(1500);
		init->init.num_outbound_streams = htons(1);
		init->init.num_inbound_streams = htons(1);
		init->init.initial_tsn = htonl(0);
		if (protlen >= (int)(sizeof(struct sctphdr) +
		    sizeof(struct sctp_init_chunk) +
		    sizeof(struct sctp_paramhdr))) {
			param = (struct sctp_paramhdr *)(init + 1);
			param->param_type = htons(SCTP_PAD);
			param->param_length =
			    htons((u_int16_t)(protlen -
			    sizeof(struct sctphdr) -
			    sizeof(struct sctp_init_chunk)));
		}
	} else {
		/*
		 * Send a packet containing a SHUTDOWN-ACK chunk,
		 * possibly followed by a PAD chunk.
		 */
		if (protlen >=
		    (int)(sizeof(struct sctphdr) +
		    sizeof(struct sctp_chunkhdr))) {
			chk = (struct sctp_chunkhdr *)(sctp + 1);
			chk->chunk_type = SCTP_SHUTDOWN_ACK;
			chk->chunk_flags = 0;
			chk->chunk_length = htons(4);
		}
		if (protlen >=
		    (int)(sizeof(struct sctphdr) +
		    2 * sizeof(struct sctp_chunkhdr))) {
			chk = chk + 1;
			chk->chunk_type = SCTP_PAD_CHUNK;
			chk->chunk_flags = 0;
			chk->chunk_length = htons(protlen -
			    (sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr)));
		}
	}
	if (doipcksum) {
		sctp->checksum = sctp_crc32c(sctp, protlen);
	}
}

int
sctp_check(const u_char *data, int seq)
{
	struct sctphdr *const sctp = (struct sctphdr *) data;

	if (ntohs(sctp->src_port) != ident ||
	    ntohs(sctp->dest_port) != port + (fixedPort ? 0 : seq))
		return (0);
	if (protlen < (int)(sizeof(struct sctphdr) +
	    sizeof(struct sctp_init_chunk))) {
		return (sctp->v_tag ==
		    (u_int32_t)((sctp->src_port << 16) | sctp->dest_port));
	} else {
		/*
		 * Don't verify the initiate_tag, since it is not available,
		 * most of the time.
		 */
		return (sctp->v_tag == 0);
	}
}

void
gre_prep(struct outdata *outdata)
{
	struct grehdr *const gre = (struct grehdr *) outp;

	gre->flags = htons(0x2001);
	gre->proto = htons(port);
	gre->length = 0;
	gre->callId = htons(ident + outdata->seq);
}

int
gre_check(const u_char *data, int seq)
{
	struct grehdr *const gre = (struct grehdr *) data;

	return(ntohs(gre->proto) == port
	    && ntohs(gre->callId) == ident + seq);
}

void
gen_prep(struct outdata *outdata)
{
	u_int16_t *const ptr = (u_int16_t *) outp;

	ptr[0] = htons(ident);
	ptr[1] = htons(port + outdata->seq);
}

int
gen_check(const u_char *data, int seq)
{
	u_int16_t *const ptr = (u_int16_t *) data;

	return(ntohs(ptr[0]) == ident
	    && ntohs(ptr[1]) == port + seq);
}

void
print(register u_char *buf, register int cc, register struct sockaddr_in *from)
{
	register struct ip *ip;
	register int hlen;
	char addr[INET_ADDRSTRLEN];

	ip = (struct ip *) buf;
	hlen = ip->ip_hl << 2;
	cc -= hlen;

	strlcpy(addr, inet_ntoa(from->sin_addr), sizeof(addr));

	if (as_path)
		Printf(" [AS%u]", as_lookup(asn, addr, AF_INET));

	if (nflag)
		Printf(" %s", addr);
	else
		Printf(" %s (%s)", inetname(from->sin_addr), addr);

	if (verbose)
		Printf(" %d bytes to %s", cc, inet_ntoa (ip->ip_dst));
}

/*
 * Checksum routine for UDP and TCP headers.
 */
u_short
p_cksum(struct ip *ip, u_short *data, int len, int cov)
{
	static struct ipovly ipo;
	u_short sum[2];

	ipo.ih_pr = ip->ip_p;
	ipo.ih_len = htons(len);
	ipo.ih_src = ip->ip_src;
	ipo.ih_dst = ip->ip_dst;

	sum[1] = in_cksum((u_short*)&ipo, sizeof(ipo)); /* pseudo ip hdr cksum */
	sum[0] = in_cksum(data, cov);                   /* payload data cksum */

	return ~in_cksum(sum, sizeof(sum));
}

/*
 * Checksum routine for Internet Protocol family headers (C Version)
 */
u_short
in_cksum(register u_short *addr, register int len)
{
	register int nleft = len;
	register u_short *w = addr;
	register u_short answer;
	register int sum = 0;

	/*
	 *  Our algorithm is simple, using a 32 bit accumulator (sum),
	 *  we add sequential 16 bit words to it, and at the end, fold
	 *  back all the carry bits from the top 16 bits into the lower
	 *  16 bits.
	 */
	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}

	/* mop up an odd byte, if necessary */
	if (nleft == 1)
		sum += *(u_char *)w;

	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */
	return (answer);
}

/*
 * CRC32C routine for the Stream Control Transmission Protocol
 */

#define CRC32C(c, d) (c = (c>>8) ^ crc_c[(c^(d))&0xFF])

static u_int32_t crc_c[256] = {
	0x00000000, 0xF26B8303, 0xE13B70F7, 0x1350F3F4,
	0xC79A971F, 0x35F1141C, 0x26A1E7E8, 0xD4CA64EB,
	0x8AD958CF, 0x78B2DBCC, 0x6BE22838, 0x9989AB3B,
	0x4D43CFD0, 0xBF284CD3, 0xAC78BF27, 0x5E133C24,
	0x105EC76F, 0xE235446C, 0xF165B798, 0x030E349B,
	0xD7C45070, 0x25AFD373, 0x36FF2087, 0xC494A384,
	0x9A879FA0, 0x68EC1CA3, 0x7BBCEF57, 0x89D76C54,
	0x5D1D08BF, 0xAF768BBC, 0xBC267848, 0x4E4DFB4B,
	0x20BD8EDE, 0xD2D60DDD, 0xC186FE29, 0x33ED7D2A,
	0xE72719C1, 0x154C9AC2, 0x061C6936, 0xF477EA35,
	0xAA64D611, 0x580F5512, 0x4B5FA6E6, 0xB93425E5,
	0x6DFE410E, 0x9F95C20D, 0x8CC531F9, 0x7EAEB2FA,
	0x30E349B1, 0xC288CAB2, 0xD1D83946, 0x23B3BA45,
	0xF779DEAE, 0x05125DAD, 0x1642AE59, 0xE4292D5A,
	0xBA3A117E, 0x4851927D, 0x5B016189, 0xA96AE28A,
	0x7DA08661, 0x8FCB0562, 0x9C9BF696, 0x6EF07595,
	0x417B1DBC, 0xB3109EBF, 0xA0406D4B, 0x522BEE48,
	0x86E18AA3, 0x748A09A0, 0x67DAFA54, 0x95B17957,
	0xCBA24573, 0x39C9C670, 0x2A993584, 0xD8F2B687,
	0x0C38D26C, 0xFE53516F, 0xED03A29B, 0x1F682198,
	0x5125DAD3, 0xA34E59D0, 0xB01EAA24, 0x42752927,
	0x96BF4DCC, 0x64D4CECF, 0x77843D3B, 0x85EFBE38,
	0xDBFC821C, 0x2997011F, 0x3AC7F2EB, 0xC8AC71E8,
	0x1C661503, 0xEE0D9600, 0xFD5D65F4, 0x0F36E6F7,
	0x61C69362, 0x93AD1061, 0x80FDE395, 0x72966096,
	0xA65C047D, 0x5437877E, 0x4767748A, 0xB50CF789,
	0xEB1FCBAD, 0x197448AE, 0x0A24BB5A, 0xF84F3859,
	0x2C855CB2, 0xDEEEDFB1, 0xCDBE2C45, 0x3FD5AF46,
	0x7198540D, 0x83F3D70E, 0x90A324FA, 0x62C8A7F9,
	0xB602C312, 0x44694011, 0x5739B3E5, 0xA55230E6,
	0xFB410CC2, 0x092A8FC1, 0x1A7A7C35, 0xE811FF36,
	0x3CDB9BDD, 0xCEB018DE, 0xDDE0EB2A, 0x2F8B6829,
	0x82F63B78, 0x709DB87B, 0x63CD4B8F, 0x91A6C88C,
	0x456CAC67, 0xB7072F64, 0xA457DC90, 0x563C5F93,
	0x082F63B7, 0xFA44E0B4, 0xE9141340, 0x1B7F9043,
	0xCFB5F4A8, 0x3DDE77AB, 0x2E8E845F, 0xDCE5075C,
	0x92A8FC17, 0x60C37F14, 0x73938CE0, 0x81F80FE3,
	0x55326B08, 0xA759E80B, 0xB4091BFF, 0x466298FC,
	0x1871A4D8, 0xEA1A27DB, 0xF94AD42F, 0x0B21572C,
	0xDFEB33C7, 0x2D80B0C4, 0x3ED04330, 0xCCBBC033,
	0xA24BB5A6, 0x502036A5, 0x4370C551, 0xB11B4652,
	0x65D122B9, 0x97BAA1BA, 0x84EA524E, 0x7681D14D,
	0x2892ED69, 0xDAF96E6A, 0xC9A99D9E, 0x3BC21E9D,
	0xEF087A76, 0x1D63F975, 0x0E330A81, 0xFC588982,
	0xB21572C9, 0x407EF1CA, 0x532E023E, 0xA145813D,
	0x758FE5D6, 0x87E466D5, 0x94B49521, 0x66DF1622,
	0x38CC2A06, 0xCAA7A905, 0xD9F75AF1, 0x2B9CD9F2,
	0xFF56BD19, 0x0D3D3E1A, 0x1E6DCDEE, 0xEC064EED,
	0xC38D26C4, 0x31E6A5C7, 0x22B65633, 0xD0DDD530,
	0x0417B1DB, 0xF67C32D8, 0xE52CC12C, 0x1747422F,
	0x49547E0B, 0xBB3FFD08, 0xA86F0EFC, 0x5A048DFF,
	0x8ECEE914, 0x7CA56A17, 0x6FF599E3, 0x9D9E1AE0,
	0xD3D3E1AB, 0x21B862A8, 0x32E8915C, 0xC083125F,
	0x144976B4, 0xE622F5B7, 0xF5720643, 0x07198540,
	0x590AB964, 0xAB613A67, 0xB831C993, 0x4A5A4A90,
	0x9E902E7B, 0x6CFBAD78, 0x7FAB5E8C, 0x8DC0DD8F,
	0xE330A81A, 0x115B2B19, 0x020BD8ED, 0xF0605BEE,
	0x24AA3F05, 0xD6C1BC06, 0xC5914FF2, 0x37FACCF1,
	0x69E9F0D5, 0x9B8273D6, 0x88D28022, 0x7AB90321,
	0xAE7367CA, 0x5C18E4C9, 0x4F48173D, 0xBD23943E,
	0xF36E6F75, 0x0105EC76, 0x12551F82, 0xE03E9C81,
	0x34F4F86A, 0xC69F7B69, 0xD5CF889D, 0x27A40B9E,
	0x79B737BA, 0x8BDCB4B9, 0x988C474D, 0x6AE7C44E,
	0xBE2DA0A5, 0x4C4623A6, 0x5F16D052, 0xAD7D5351
};

u_int32_t
sctp_crc32c(const void *packet, u_int32_t len)
{
	u_int32_t i, crc32c;
	u_int8_t byte0, byte1, byte2, byte3;
	const u_int8_t *buf = (const u_int8_t *)packet;

	crc32c = ~0;
	for (i = 0; i < len; i++)
		CRC32C(crc32c, buf[i]);
	crc32c = ~crc32c;
	byte0  = crc32c & 0xff;
	byte1  = (crc32c>>8) & 0xff;
	byte2  = (crc32c>>16) & 0xff;
	byte3  = (crc32c>>24) & 0xff;
	crc32c = ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3);
	return htonl(crc32c);
}

/*
 * Subtract 2 timeval structs:  out = out - in.
 * Out is assumed to be within about LONG_MAX seconds of in.
 */
void
tvsub(register struct timeval *out, register struct timeval *in)
{

	if ((out->tv_usec -= in->tv_usec) < 0)   {
		--out->tv_sec;
		out->tv_usec += 1000000;
	}
	out->tv_sec -= in->tv_sec;
}

/*
 * Construct an Internet address representation.
 * If the nflag has been supplied, give
 * numeric value, otherwise try for symbolic name.
 */
char *
inetname(struct in_addr in)
{
	register char *cp;
	register struct hostent *hp;
	static int first = 1;
	static char domain[MAXHOSTNAMELEN + 1], line[MAXHOSTNAMELEN + 1];

	if (first && !nflag) {
		first = 0;
		if (gethostname(domain, sizeof(domain) - 1) < 0)
			domain[0] = '\0';
		else {
			cp = strchr(domain, '.');
			if (cp == NULL) {
#ifdef WITH_CASPER
				if (capdns != NULL)
					hp = cap_gethostbyname(capdns, domain);
				else
#endif
					hp = gethostbyname(domain);
				if (hp != NULL)
					cp = strchr(hp->h_name, '.');
			}
			if (cp == NULL)
				domain[0] = '\0';
			else {
				++cp;
				(void)strncpy(domain, cp, sizeof(domain) - 1);
				domain[sizeof(domain) - 1] = '\0';
			}
		}
	}
	if (!nflag && in.s_addr != INADDR_ANY) {
#ifdef WITH_CASPER
		if (capdns != NULL)
			hp = cap_gethostbyaddr(capdns, (char *)&in, sizeof(in),
			    AF_INET);
		else
#endif
			hp = gethostbyaddr((char *)&in, sizeof(in), AF_INET);
		if (hp != NULL) {
			if ((cp = strchr(hp->h_name, '.')) != NULL &&
			    strcmp(cp + 1, domain) == 0)
				*cp = '\0';
			(void)strncpy(line, hp->h_name, sizeof(line) - 1);
			line[sizeof(line) - 1] = '\0';
			return (line);
		}
	}
	return (inet_ntoa(in));
}

struct hostinfo *
gethostinfo(register char *hostname)
{
	register int n;
	register struct hostent *hp;
	register struct hostinfo *hi;
	register char **p;
	register u_int32_t addr, *ap;

	if (strlen(hostname) >= MAXHOSTNAMELEN) {
		Fprintf(stderr, "%s: hostname \"%.32s...\" is too long\n",
		    prog, hostname);
		exit(1);
	}
	hi = calloc(1, sizeof(*hi));
	if (hi == NULL) {
		Fprintf(stderr, "%s: calloc %s\n", prog, strerror(errno));
		exit(1);
	}
	addr = inet_addr(hostname);
	if ((int32_t)addr != -1) {
		hi->name = strdup(hostname);
		hi->n = 1;
		hi->addrs = calloc(1, sizeof(hi->addrs[0]));
		if (hi->addrs == NULL) {
			Fprintf(stderr, "%s: calloc %s\n",
			    prog, strerror(errno));
			exit(1);
		}
		hi->addrs[0] = addr;
		return (hi);
	}

#ifdef WITH_CASPER
	if (capdns != NULL)
		hp = cap_gethostbyname(capdns, hostname);
	else
#endif
		hp = gethostbyname(hostname);
	if (hp == NULL) {
		Fprintf(stderr, "%s: unknown host %s\n", prog, hostname);
		exit(1);
	}
	if (hp->h_addrtype != AF_INET || hp->h_length != 4) {
		Fprintf(stderr, "%s: bad host %s\n", prog, hostname);
		exit(1);
	}
	hi->name = strdup(hp->h_name);
	for (n = 0, p = hp->h_addr_list; *p != NULL; ++n, ++p)
		continue;
	hi->n = n;
	hi->addrs = calloc(n, sizeof(hi->addrs[0]));
	if (hi->addrs == NULL) {
		Fprintf(stderr, "%s: calloc %s\n", prog, strerror(errno));
		exit(1);
	}
	for (ap = hi->addrs, p = hp->h_addr_list; *p != NULL; ++ap, ++p)
		memcpy(ap, *p, sizeof(*ap));
	return (hi);
}

void
freehostinfo(register struct hostinfo *hi)
{
	if (hi->name != NULL) {
		free(hi->name);
		hi->name = NULL;
	}
	free((char *)hi->addrs);
	free((char *)hi);
}

void
getaddr(register u_int32_t *ap, register char *hostname)
{
	register struct hostinfo *hi;

	hi = gethostinfo(hostname);
	*ap = hi->addrs[0];
	freehostinfo(hi);
}

void
setsin(register struct sockaddr_in *sin, register u_int32_t addr)
{

	memset(sin, 0, sizeof(*sin));
#ifdef HAVE_SOCKADDR_SA_LEN
	sin->sin_len = sizeof(*sin);
#endif
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
}

/* String to value with optional min and max. Handles decimal and hex. */
int
str2val(register const char *str, register const char *what,
    register int mi, register int ma)
{
	register const char *cp;
	register int val;
	char *ep;

	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
		cp = str + 2;
		val = (int)strtol(cp, &ep, 16);
	} else
		val = (int)strtol(str, &ep, 10);
	if (*ep != '\0') {
		Fprintf(stderr, "%s: \"%s\" bad value for %s \n",
		    prog, str, what);
		exit(1);
	}
	if (val < mi && mi >= 0) {
		if (mi == 0)
			Fprintf(stderr, "%s: %s must be >= %d\n",
			    prog, what, mi);
		else
			Fprintf(stderr, "%s: %s must be > %d\n",
			    prog, what, mi - 1);
		exit(1);
	}
	if (val > ma && ma >= 0) {
		Fprintf(stderr, "%s: %s must be <= %d\n", prog, what, ma);
		exit(1);
	}
	return (val);
}

struct outproto *
setproto(char *pname)
{
	struct outproto *proto;
	int i;

	for (i = 0; protos[i].name != NULL; i++) {
		if (strcasecmp(protos[i].name, pname) == 0) {
			break;
		}
	}
	proto = &protos[i];
	if (proto->name == NULL) {	/* generic handler */
		struct protoent *pe;
		u_long pnum;

		/* Determine the IP protocol number */
		if ((pe = getprotobyname(pname)) != NULL)
			pnum = pe->p_proto;
		else
			pnum = str2val(optarg, "proto number", 1, 255);
		proto->num = pnum;
	}
	return proto;
}

void
pkt_compare(const u_char *a, int la, const u_char *b, int lb) {
	int l;
	int i;

	for (i = 0; i < la; i++)
		Printf("%02x", (unsigned int)a[i]);
	Printf("\n");
	l = (la <= lb) ? la : lb;
	for (i = 0; i < l; i++)
		if (a[i] == b[i])
			Printf("__");
		else
			Printf("%02x", (unsigned int)b[i]);
	for (; i < lb; i++)
		Printf("%02x", (unsigned int)b[i]);
	Printf("\n");
}


void
usage(void)
{
	extern char version[];

	Fprintf(stderr, "Version %s\n", version);
	Fprintf(stderr,
	    "Usage: %s [-adDeFInrSvx] [-f first_ttl] [-g gateway] [-i iface]\n"
	    "\t[-m max_ttl] [-p port] [-P proto] [-q nqueries] [-s src_addr]\n"
	    "\t[-t tos] [-w waittime] [-A as_server] [-z pausemsecs] host [packetlen]\n", prog);
	exit(1);
}
