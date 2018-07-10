/* vi: set sw=4 ts=4: */
/*
 * route
 *
 * Similar to the standard Unix route, but with only the necessary
 * parts for AF_INET and AF_INET6
 *
 * Bjorn Wesen, Axis Communications AB
 *
 * Author of the original route:
 *              Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *              (derived from FvK's 'route.c     1.70    01/04/94')
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 *
 * displayroute() code added by Vladimir N. Oleynik <dzo@simtreas.ru>
 * adjustments by Larry Doolittle  <LRDoolittle@lbl.gov>
 *
 * IPV6 support added by Bart Visscher <magick@linux-fan.com>
 */
/* 2004/03/09  Manuel Novoa III <mjn3@codepoet.org>
 *
 * Rewritten to fix several bugs, add additional error checking, and
 * remove ridiculous amounts of bloat.
 */
//config:config ROUTE
//config:	bool "route (8.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Route displays or manipulates the kernel's IP routing tables.

//applet:IF_ROUTE(APPLET(route, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_ROUTE) += route.o

//usage:#define route_trivial_usage
//usage:       "[{add|del|delete}]"
//usage:#define route_full_usage "\n\n"
//usage:       "Edit kernel routing tables\n"
//usage:     "\n	-n	Don't resolve names"
//usage:     "\n	-e	Display other/more information"
//usage:     "\n	-A inet" IF_FEATURE_IPV6("{6}") "	Select address family"

#include <net/route.h>
#include <net/if.h>

#include "libbb.h"
#include "inet_common.h"


#ifndef RTF_UP
/* Keep this in sync with /usr/src/linux/include/linux/route.h */
#define RTF_UP          0x0001	/* route usable                 */
#define RTF_GATEWAY     0x0002	/* destination is a gateway     */
#define RTF_HOST        0x0004	/* host entry (net otherwise)   */
#define RTF_REINSTATE   0x0008	/* reinstate route after tmout  */
#define RTF_DYNAMIC     0x0010	/* created dyn. (by redirect)   */
#define RTF_MODIFIED    0x0020	/* modified dyn. (by redirect)  */
#define RTF_MTU         0x0040	/* specific MTU for this route  */
#ifndef RTF_MSS
#define RTF_MSS         RTF_MTU	/* Compatibility :-(            */
#endif
#define RTF_WINDOW      0x0080	/* per route window clamping    */
#define RTF_IRTT        0x0100	/* Initial round trip time      */
#define RTF_REJECT      0x0200	/* Reject route                 */
#define RTF_NONEXTHOP   0x00200000 /* route with no nexthop	*/
#endif

#if defined(SIOCADDRTOLD) || defined(RTF_IRTT)	/* route */
#define HAVE_NEW_ADDRT 1
#endif

#if HAVE_NEW_ADDRT
#define mask_in_addr(x) (((struct sockaddr_in *)&((x).rt_genmask))->sin_addr.s_addr)
#define full_mask(x) (x)
#else
#define mask_in_addr(x) ((x).rt_genmask)
#define full_mask(x) (((struct sockaddr_in *)&(x))->sin_addr.s_addr)
#endif

/* The RTACTION entries must agree with tbl_verb[] below! */
#define RTACTION_ADD 1
#define RTACTION_DEL 2

/* For the various tbl_*[] arrays, the 1st byte is the offset to
 * the next entry and the 2nd byte is return value. */

#define NET_FLAG  1
#define HOST_FLAG 2

/* We remap '-' to '#' to avoid problems with getopt. */
static const char tbl_hash_net_host[] ALIGN1 =
	"\007\001#net\0"
/*	"\010\002#host\0" */
	"\007\002#host"				/* Since last, we can save a byte. */
;

#define KW_TAKES_ARG            020
#define KW_SETS_FLAG            040

#define KW_IPVx_METRIC          020
#define KW_IPVx_NETMASK         021
#define KW_IPVx_GATEWAY         022
#define KW_IPVx_MSS             023
#define KW_IPVx_WINDOW          024
#define KW_IPVx_IRTT            025
#define KW_IPVx_DEVICE          026

#define KW_IPVx_FLAG_ONLY       040
#define KW_IPVx_REJECT          040
#define KW_IPVx_MOD             041
#define KW_IPVx_DYN             042
#define KW_IPVx_REINSTATE       043

static const char tbl_ipvx[] ALIGN1 =
	/* 020 is the "takes an arg" bit */
#if HAVE_NEW_ADDRT
	"\011\020metric\0"
#endif
	"\012\021netmask\0"
	"\005\022gw\0"
	"\012\022gateway\0"
	"\006\023mss\0"
	"\011\024window\0"
#ifdef RTF_IRTT
	"\007\025irtt\0"
#endif
	"\006\026dev\0"
	"\011\026device\0"
	/* 040 is the "sets a flag" bit - MUST match flags_ipvx[] values below. */
#ifdef RTF_REJECT
	"\011\040reject\0"
#endif
	"\006\041mod\0"
	"\006\042dyn\0"
/*	"\014\043reinstate\0" */
	"\013\043reinstate"			/* Since last, we can save a byte. */
;

static const uint16_t flags_ipvx[] = { /* MUST match tbl_ipvx[] values above. */
#ifdef RTF_REJECT
	RTF_REJECT,
#endif
	RTF_MODIFIED,
	RTF_DYNAMIC,
	RTF_REINSTATE
};

static int kw_lookup(const char *kwtbl, char ***pargs)
{
	if (**pargs) {
		do {
			if (strcmp(kwtbl+2, **pargs) == 0) { /* Found a match. */
				*pargs += 1;
				if (kwtbl[1] & KW_TAKES_ARG) {
					if (!**pargs) {	/* No more args! */
						bb_show_usage();
					}
					*pargs += 1; /* Calling routine will use args[-1]. */
				}
				return kwtbl[1];
			}
			kwtbl += *kwtbl;
		} while (*kwtbl);
	}
	return 0;
}

/* Add or delete a route, depending on action. */

static NOINLINE void INET_setroute(int action, char **args)
{
	/* char buffer instead of bona-fide struct avoids aliasing warning */
	char rt_buf[sizeof(struct rtentry)];
	struct rtentry *const rt = (void *)rt_buf;

	const char *netmask = NULL;
	int skfd, isnet, xflag;

	/* Grab the -net or -host options.  Remember they were transformed. */
	xflag = kw_lookup(tbl_hash_net_host, &args);

	/* If we did grab -net or -host, make sure we still have an arg left. */
	if (*args == NULL) {
		bb_show_usage();
	}

	/* Clean out the RTREQ structure. */
	memset(rt, 0, sizeof(*rt));

	{
		const char *target = *args++;
		char *prefix;

		/* recognize x.x.x.x/mask format. */
		prefix = strchr(target, '/');
		if (prefix) {
			int prefix_len;

			prefix_len = xatoul_range(prefix+1, 0, 32);
			mask_in_addr(*rt) = htonl( ~(0xffffffffUL >> prefix_len));
			*prefix = '\0';
#if HAVE_NEW_ADDRT
			rt->rt_genmask.sa_family = AF_INET;
#endif
		} else {
			/* Default netmask. */
			netmask = "default";
		}
		/* Prefer hostname lookup is -host flag (xflag==1) was given. */
		isnet = INET_resolve(target, (struct sockaddr_in *) &rt->rt_dst,
							 (xflag & HOST_FLAG));
		if (isnet < 0) {
			bb_error_msg_and_die("resolving %s", target);
		}
		if (prefix) {
			/* do not destroy prefix for process args */
			*prefix = '/';
		}
	}

	if (xflag) {		/* Reinit isnet if -net or -host was specified. */
		isnet = (xflag & NET_FLAG);
	}

	/* Fill in the other fields. */
	rt->rt_flags = ((isnet) ? RTF_UP : (RTF_UP | RTF_HOST));

	while (*args) {
		int k = kw_lookup(tbl_ipvx, &args);
		const char *args_m1 = args[-1];

		if (k & KW_IPVx_FLAG_ONLY) {
			rt->rt_flags |= flags_ipvx[k & 3];
			continue;
		}

#if HAVE_NEW_ADDRT
		if (k == KW_IPVx_METRIC) {
			rt->rt_metric = xatoul(args_m1) + 1;
			continue;
		}
#endif

		if (k == KW_IPVx_NETMASK) {
			struct sockaddr mask;

			if (mask_in_addr(*rt)) {
				bb_show_usage();
			}

			netmask = args_m1;
			isnet = INET_resolve(netmask, (struct sockaddr_in *) &mask, 0);
			if (isnet < 0) {
				bb_error_msg_and_die("resolving %s", netmask);
			}
			rt->rt_genmask = full_mask(mask);
			continue;
		}

		if (k == KW_IPVx_GATEWAY) {
			if (rt->rt_flags & RTF_GATEWAY) {
				bb_show_usage();
			}

			isnet = INET_resolve(args_m1,
						(struct sockaddr_in *) &rt->rt_gateway, 1);
			rt->rt_flags |= RTF_GATEWAY;

			if (isnet) {
				if (isnet < 0) {
					bb_error_msg_and_die("resolving %s", args_m1);
				}
				bb_error_msg_and_die("gateway %s is a NETWORK", args_m1);
			}
			continue;
		}

		if (k == KW_IPVx_MSS) {	/* Check valid MSS bounds. */
			rt->rt_flags |= RTF_MSS;
			rt->rt_mss = xatoul_range(args_m1, 64, 32768);
			continue;
		}

		if (k == KW_IPVx_WINDOW) {	/* Check valid window bounds. */
			rt->rt_flags |= RTF_WINDOW;
			rt->rt_window = xatoul_range(args_m1, 128, INT_MAX);
			continue;
		}

#ifdef RTF_IRTT
		if (k == KW_IPVx_IRTT) {
			rt->rt_flags |= RTF_IRTT;
			rt->rt_irtt = xatoul(args_m1);
			rt->rt_irtt *= (bb_clk_tck() / 100);	/* FIXME */
#if 0					/* FIXME: do we need to check anything of this? */
			if (rt->rt_irtt < 1 || rt->rt_irtt > (120 * HZ)) {
				bb_error_msg_and_die("bad irtt");
			}
#endif
			continue;
		}
#endif

		/* Device is special in that it can be the last arg specified
		 * and doesn't require the dev/device keyword in that case. */
		if (!rt->rt_dev && ((k == KW_IPVx_DEVICE) || (!k && !*++args))) {
			/* Don't use args_m1 here since args may have changed! */
			rt->rt_dev = args[-1];
			continue;
		}

		/* Nothing matched. */
		bb_show_usage();
	}

#ifdef RTF_REJECT
	if ((rt->rt_flags & RTF_REJECT) && !rt->rt_dev) {
		rt->rt_dev = (char*)"lo";
	}
#endif

	/* sanity checks.. */
	if (mask_in_addr(*rt)) {
		uint32_t mask = mask_in_addr(*rt);

		mask = ~ntohl(mask);
		if ((rt->rt_flags & RTF_HOST) && mask != 0xffffffff) {
			bb_error_msg_and_die("netmask %.8x and host route conflict",
								 (unsigned int) mask);
		}
		if (mask & (mask + 1)) {
			bb_error_msg_and_die("bogus netmask %s", netmask);
		}
		mask = ((struct sockaddr_in *) &rt->rt_dst)->sin_addr.s_addr;
		if (mask & ~(uint32_t)mask_in_addr(*rt)) {
			bb_error_msg_and_die("netmask and route address conflict");
		}
	}

	/* Fill out netmask if still unset */
	if ((action == RTACTION_ADD) && (rt->rt_flags & RTF_HOST)) {
		mask_in_addr(*rt) = 0xffffffff;
	}

	/* Create a socket to the INET kernel. */
	skfd = xsocket(AF_INET, SOCK_DGRAM, 0);

	if (action == RTACTION_ADD)
		xioctl(skfd, SIOCADDRT, rt);
	else
		xioctl(skfd, SIOCDELRT, rt);

	if (ENABLE_FEATURE_CLEAN_UP) close(skfd);
}

#if ENABLE_FEATURE_IPV6

static NOINLINE void INET6_setroute(int action, char **args)
{
	struct sockaddr_in6 sa6;
	struct in6_rtmsg rt;
	int prefix_len, skfd;
	const char *devname;

		/* We know args isn't NULL from the check in route_main. */
		const char *target = *args++;

		if (strcmp(target, "default") == 0) {
			prefix_len = 0;
			memset(&sa6, 0, sizeof(sa6));
		} else {
			char *cp;
			cp = strchr(target, '/'); /* Yes... const to non is ok. */
			if (cp) {
				*cp = '\0';
				prefix_len = xatoul_range(cp + 1, 0, 128);
			} else {
				prefix_len = 128;
			}
			if (INET6_resolve(target, (struct sockaddr_in6 *) &sa6) < 0) {
				bb_error_msg_and_die("resolving %s", target);
			}
		}

	/* Clean out the RTREQ structure. */
	memset(&rt, 0, sizeof(rt));

	memcpy(&rt.rtmsg_dst, sa6.sin6_addr.s6_addr, sizeof(struct in6_addr));

	/* Fill in the other fields. */
	rt.rtmsg_dst_len = prefix_len;
	rt.rtmsg_flags = ((prefix_len == 128) ? (RTF_UP|RTF_HOST) : RTF_UP);
	rt.rtmsg_metric = 1;

	devname = NULL;

	while (*args) {
		int k = kw_lookup(tbl_ipvx, &args);
		const char *args_m1 = args[-1];

		if ((k == KW_IPVx_MOD) || (k == KW_IPVx_DYN)) {
			rt.rtmsg_flags |= flags_ipvx[k & 3];
			continue;
		}

		if (k == KW_IPVx_METRIC) {
			rt.rtmsg_metric = xatoul(args_m1);
			continue;
		}

		if (k == KW_IPVx_GATEWAY) {
			if (rt.rtmsg_flags & RTF_GATEWAY) {
				bb_show_usage();
			}

			if (INET6_resolve(args_m1, (struct sockaddr_in6 *) &sa6) < 0) {
				bb_error_msg_and_die("resolving %s", args_m1);
			}
			memcpy(&rt.rtmsg_gateway, sa6.sin6_addr.s6_addr,
					sizeof(struct in6_addr));
			rt.rtmsg_flags |= RTF_GATEWAY;
			continue;
		}

		/* Device is special in that it can be the last arg specified
		 * and doesn't require the dev/device keyword in that case. */
		if (!devname && ((k == KW_IPVx_DEVICE) || (!k && !*++args))) {
			/* Don't use args_m1 here since args may have changed! */
			devname = args[-1];
			continue;
		}

		/* Nothing matched. */
		bb_show_usage();
	}

	/* Create a socket to the INET6 kernel. */
	skfd = xsocket(AF_INET6, SOCK_DGRAM, 0);

	rt.rtmsg_ifindex = 0;

	if (devname) {
		struct ifreq ifr;
		/*memset(&ifr, 0, sizeof(ifr)); - SIOCGIFINDEX does not need to clear all */
		strncpy_IFNAMSIZ(ifr.ifr_name, devname);
		xioctl(skfd, SIOCGIFINDEX, &ifr);
		rt.rtmsg_ifindex = ifr.ifr_ifindex;
	}

	/* Tell the kernel to accept this route. */
	if (action == RTACTION_ADD)
		xioctl(skfd, SIOCADDRT, &rt);
	else
		xioctl(skfd, SIOCDELRT, &rt);

	if (ENABLE_FEATURE_CLEAN_UP) close(skfd);
}
#endif

static const
IF_NOT_FEATURE_IPV6(uint16_t)
IF_FEATURE_IPV6(unsigned)
flagvals[] = { /* Must agree with flagchars[]. */
	RTF_UP,
	RTF_GATEWAY,
	RTF_HOST,
	RTF_REINSTATE,
	RTF_DYNAMIC,
	RTF_MODIFIED,
#if ENABLE_FEATURE_IPV6
	RTF_DEFAULT,
	RTF_ADDRCONF,
	RTF_CACHE,
	RTF_REJECT,
	RTF_NONEXTHOP, /* this one doesn't fit into 16 bits */
#endif
};
/* Must agree with flagvals[]. */
static const char flagchars[] ALIGN1 =
	"UGHRDM"
#if ENABLE_FEATURE_IPV6
	"DAC!n"
#endif
;
#define IPV4_MASK (RTF_UP|RTF_GATEWAY|RTF_HOST|RTF_REINSTATE|RTF_DYNAMIC|RTF_MODIFIED)
#define IPV6_MASK (RTF_UP|RTF_GATEWAY|RTF_HOST|RTF_DEFAULT|RTF_ADDRCONF|RTF_CACHE|RTF_REJECT|RTF_NONEXTHOP)

static void set_flags(char *flagstr, int flags)
{
	int i;

	for (i = 0; (*flagstr = flagchars[i]) != 0; i++) {
		if (flags & flagvals[i]) {
			++flagstr;
		}
	}
}

/* also used in netstat */
void FAST_FUNC bb_displayroutes(int noresolve, int netstatfmt)
{
	char devname[64], flags[16], *sdest, *sgw;
	unsigned long d, g, m;
	int r;
	int flgs, ref, use, metric, mtu, win, ir;
	struct sockaddr_in s_addr;
	struct in_addr mask;

	FILE *fp = xfopen_for_read("/proc/net/route");

	printf("Kernel IP routing table\n"
		"Destination     Gateway         Genmask         Flags %s Iface\n",
			netstatfmt ? "  MSS Window  irtt" : "Metric Ref    Use");

	/* Skip the first line. */
	r = fscanf(fp, "%*[^\n]\n");
	if (r < 0) {
		/* Empty line, read error, or EOF. Yes, if routing table
		 * is completely empty, /proc/net/route has no header.
		 */
		goto ERROR;
	}
	while (1) {
		r = fscanf(fp, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n",
				devname, &d, &g, &flgs, &ref, &use, &metric, &m,
				&mtu, &win, &ir);
		if (r != 11) {
 ERROR:
			if ((r < 0) && feof(fp)) { /* EOF with no (nonspace) chars read. */
				break;
			}
			bb_perror_msg_and_die(bb_msg_read_error);
		}

		if (!(flgs & RTF_UP)) { /* Skip interfaces that are down. */
			continue;
		}

		set_flags(flags, (flgs & IPV4_MASK));
#ifdef RTF_REJECT
		if (flgs & RTF_REJECT) {
			flags[0] = '!';
		}
#endif

		memset(&s_addr, 0, sizeof(struct sockaddr_in));
		s_addr.sin_family = AF_INET;
		s_addr.sin_addr.s_addr = d;
		sdest = INET_rresolve(&s_addr, (noresolve | 0x8000), m); /* 'default' instead of '*' */
		s_addr.sin_addr.s_addr = g;
		sgw = INET_rresolve(&s_addr, (noresolve | 0x4000), m); /* Host instead of net */
		mask.s_addr = m;
		/* "%15.15s" truncates hostnames, do we really want that? */
		printf("%-15.15s %-15.15s %-16s%-6s", sdest, sgw, inet_ntoa(mask), flags);
		free(sdest);
		free(sgw);
		if (netstatfmt) {
			printf("%5d %-5d %6d %s\n", mtu, win, ir, devname);
		} else {
			printf("%-6d %-2d %7d %s\n", metric, ref, use, devname);
		}
	}
	fclose(fp);
}

#if ENABLE_FEATURE_IPV6

static void INET6_displayroutes(void)
{
	char addr6[128], *naddr6;
	/* In addr6x, we store both 40-byte ':'-delimited ipv6 addresses.
	 * We read the non-delimited strings into the tail of the buffer
	 * using fscanf and then modify the buffer by shifting forward
	 * while inserting ':'s and the nul terminator for the first string.
	 * Hence the strings are at addr6x and addr6x+40.  This generates
	 * _much_ less code than the previous (upstream) approach. */
	char addr6x[80];
	char iface[16], flags[16];
	int iflags, metric, refcnt, use, prefix_len, slen;
	struct sockaddr_in6 snaddr6;

	FILE *fp = xfopen_for_read("/proc/net/ipv6_route");

	printf("Kernel IPv6 routing table\n%-44s%-40s"
			"Flags Metric Ref    Use Iface\n",
			"Destination", "Next Hop");

	while (1) {
		int r;
		r = fscanf(fp, "%32s%x%*s%x%32s%x%x%x%x%s\n",
				addr6x+14, &prefix_len, &slen, addr6x+40+7,
				&metric, &refcnt, &use, &iflags, iface);
		if (r != 9) {
			if ((r < 0) && feof(fp)) { /* EOF with no (nonspace) chars read. */
				break;
			}
 ERROR:
			bb_perror_msg_and_die(bb_msg_read_error);
		}

		/* Do the addr6x shift-and-insert changes to ':'-delimit addresses.
		 * For now, always do this to validate the proc route format, even
		 * if the interface is down. */
		{
			int i = 0;
			char *p = addr6x+14;

			do {
				if (!*p) {
					if (i == 40) { /* nul terminator for 1st address? */
						addr6x[39] = 0;	/* Fixup... need 0 instead of ':'. */
						++p;	/* Skip and continue. */
						continue;
					}
					goto ERROR;
				}
				addr6x[i++] = *p++;
				if (!((i+1) % 5)) {
					addr6x[i++] = ':';
				}
			} while (i < 40+28+7);
		}

		set_flags(flags, (iflags & IPV6_MASK));

		r = 0;
		while (1) {
			inet_pton(AF_INET6, addr6x + r,
					  (struct sockaddr *) &snaddr6.sin6_addr);
			snaddr6.sin6_family = AF_INET6;
			naddr6 = INET6_rresolve((struct sockaddr_in6 *) &snaddr6,
						0x0fff /* Apparently, upstream never resolves. */
						);

			if (!r) {			/* 1st pass */
				snprintf(addr6, sizeof(addr6), "%s/%d", naddr6, prefix_len);
				r += 40;
				free(naddr6);
			} else {			/* 2nd pass */
				/* Print the info. */
				printf("%-43s %-39s %-5s %-6d %-2d %7d %-8s\n",
						addr6, naddr6, flags, metric, refcnt, use, iface);
				free(naddr6);
				break;
			}
		}
	}
	fclose(fp);
}

#endif

#define ROUTE_OPT_A     0x01
#define ROUTE_OPT_n     0x02
#define ROUTE_OPT_e     0x04
#define ROUTE_OPT_INET6 0x08 /* Not an actual option. See below. */

/* 1st byte is offset to next entry offset.  2nd byte is return value. */
/* 2nd byte matches RTACTION_* code */
static const char tbl_verb[] ALIGN1 =
	"\006\001add\0"
	"\006\002del\0"
/*	"\011\002delete\0" */
	"\010\002delete"  /* Since it's last, we can save a byte. */
;

int route_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int route_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;
	int what;
	char *family;
	char **p;

	/* First, remap '-net' and '-host' to avoid getopt problems. */
	p = argv;
	while (*++p) {
		if (strcmp(*p, "-net") == 0 || strcmp(*p, "-host") == 0) {
			p[0][0] = '#';
		}
	}

	opt = getopt32(argv, "A:ne", &family);

	if ((opt & ROUTE_OPT_A) && strcmp(family, "inet") != 0) {
#if ENABLE_FEATURE_IPV6
		if (strcmp(family, "inet6") == 0) {
			opt |= ROUTE_OPT_INET6;	/* Set flag for ipv6. */
		} else
#endif
		bb_show_usage();
	}

	argv += optind;

	/* No more args means display the routing table. */
	if (!*argv) {
		int noresolve = (opt & ROUTE_OPT_n) ? 0x0fff : 0;
#if ENABLE_FEATURE_IPV6
		if (opt & ROUTE_OPT_INET6)
			INET6_displayroutes();
		else
#endif
			bb_displayroutes(noresolve, opt & ROUTE_OPT_e);

		fflush_stdout_and_exit(EXIT_SUCCESS);
	}

	/* Check verb.  At the moment, must be add, del, or delete. */
	what = kw_lookup(tbl_verb, &argv);
	if (!what || !*argv) {		/* Unknown verb or no more args. */
		bb_show_usage();
	}

#if ENABLE_FEATURE_IPV6
	if (opt & ROUTE_OPT_INET6)
		INET6_setroute(what, argv);
	else
#endif
		INET_setroute(what, argv);

	return EXIT_SUCCESS;
}
