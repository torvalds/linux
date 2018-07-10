/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Ported to Busybox by:  Curt Brune <curt@cumulusnetworks.com>
 */
#include "ip_common.h"  /* #include "libbb.h" is inside */
#include "common_bufsiz.h"
#include "rt_names.h"
#include "utils.h"
#include <linux/neighbour.h>
#include <net/if_arp.h>

//static int xshow_stats = 3;
enum { xshow_stats = 3 };

static inline uint32_t rta_getattr_u32(const struct rtattr *rta)
{
	return *(uint32_t *)RTA_DATA(rta);
}

#ifndef RTAX_RTTVAR
#define RTAX_RTTVAR RTAX_HOPS
#endif


struct filter_t {
	int family;
	int index;
	int state;
	int unused_only;
	inet_prefix pfx;
	int flushed;
	char *flushb;
	int flushp;
	int flushe;
	struct rtnl_handle *rth;
} FIX_ALIASING;
typedef struct filter_t filter_t;

#define G_filter (*(filter_t*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static int flush_update(void)
{
	if (rtnl_send(G_filter.rth, G_filter.flushb, G_filter.flushp) < 0) {
		bb_perror_msg("can't send flush request");
		return -1;
	}
	G_filter.flushp = 0;
	return 0;
}

static unsigned nud_state_a2n(char *arg)
{
	static const char keywords[] ALIGN1 =
		/* "ip neigh show/flush" parameters: */
		"permanent\0" "reachable\0"   "noarp\0"  "none\0"
		"stale\0"     "incomplete\0"  "delay\0"  "probe\0"
		"failed\0"
		;
	static uint8_t nuds[] ALIGN1 = {
		NUD_PERMANENT,NUD_REACHABLE, NUD_NOARP,NUD_NONE,
		NUD_STALE,    NUD_INCOMPLETE,NUD_DELAY,NUD_PROBE,
		NUD_FAILED
	};
	int id;

	BUILD_BUG_ON(
		(NUD_PERMANENT|NUD_REACHABLE| NUD_NOARP|NUD_NONE|
		NUD_STALE|    NUD_INCOMPLETE|NUD_DELAY|NUD_PROBE|
		NUD_FAILED) > 0xff
	);

	id = index_in_substrings(keywords, arg);
	if (id < 0)
		bb_error_msg_and_die(bb_msg_invalid_arg_to, arg, "nud state");
	return nuds[id];
}

#ifndef NDA_RTA
#define NDA_RTA(r) \
	((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))
#endif


static int FAST_FUNC print_neigh(const struct sockaddr_nl *who UNUSED_PARAM,
				 struct nlmsghdr *n, void *arg UNUSED_PARAM)
{
	struct ndmsg *r = NLMSG_DATA(n);
	int len = n->nlmsg_len;
	struct rtattr *tb[NDA_MAX+1];

	if (n->nlmsg_type != RTM_NEWNEIGH && n->nlmsg_type != RTM_DELNEIGH) {
		bb_error_msg_and_die("not RTM_NEWNEIGH: %08x %08x %08x",
				     n->nlmsg_len, n->nlmsg_type,
				     n->nlmsg_flags);
	}
	len -= NLMSG_LENGTH(sizeof(*r));
	if (len < 0) {
		bb_error_msg_and_die("BUG: wrong nlmsg len %d", len);
	}

	if (G_filter.flushb && n->nlmsg_type != RTM_NEWNEIGH)
		return 0;

	if (G_filter.family && G_filter.family != r->ndm_family)
		return 0;
	if (G_filter.index && G_filter.index != r->ndm_ifindex)
		return 0;
	if (!(G_filter.state&r->ndm_state)
	 && !(r->ndm_flags & NTF_PROXY)
	 && (r->ndm_state || !(G_filter.state & 0x100))
	 && (r->ndm_family != AF_DECnet)
	) {
		return 0;
	}

	parse_rtattr(tb, NDA_MAX, NDA_RTA(r), n->nlmsg_len - NLMSG_LENGTH(sizeof(*r)));

	if (tb[NDA_DST]) {
		if (G_filter.pfx.family) {
			inet_prefix dst;
			memset(&dst, 0, sizeof(dst));
			dst.family = r->ndm_family;
			memcpy(&dst.data, RTA_DATA(tb[NDA_DST]), RTA_PAYLOAD(tb[NDA_DST]));
			if (inet_addr_match(&dst, &G_filter.pfx, G_filter.pfx.bitlen))
				return 0;
		}
	}
	if (G_filter.unused_only && tb[NDA_CACHEINFO]) {
		struct nda_cacheinfo *ci = RTA_DATA(tb[NDA_CACHEINFO]);
		if (ci->ndm_refcnt)
			return 0;
	}

	if (G_filter.flushb) {
		struct nlmsghdr *fn;
		if (NLMSG_ALIGN(G_filter.flushp) + n->nlmsg_len > G_filter.flushe) {
			if (flush_update())
				return -1;
		}
		fn = (struct nlmsghdr*)(G_filter.flushb + NLMSG_ALIGN(G_filter.flushp));
		memcpy(fn, n, n->nlmsg_len);
		fn->nlmsg_type = RTM_DELNEIGH;
		fn->nlmsg_flags = NLM_F_REQUEST;
		fn->nlmsg_seq = ++(G_filter.rth->seq);
		G_filter.flushp = (((char*)fn) + n->nlmsg_len) - G_filter.flushb;
		G_filter.flushed++;
		if (xshow_stats < 2)
			return 0;
	}

	if (tb[NDA_DST]) {
		printf("%s ",
		       format_host(r->ndm_family,
				   RTA_PAYLOAD(tb[NDA_DST]),
				   RTA_DATA(tb[NDA_DST]))
		);
	}
	if (!G_filter.index && r->ndm_ifindex)
		printf("dev %s ", ll_index_to_name(r->ndm_ifindex));
	if (tb[NDA_LLADDR]) {
		SPRINT_BUF(b1);
		printf("lladdr %s", ll_addr_n2a(RTA_DATA(tb[NDA_LLADDR]),
						RTA_PAYLOAD(tb[NDA_LLADDR]),
						ARPHRD_ETHER,
						b1, sizeof(b1)));
	}
	if (r->ndm_flags & NTF_ROUTER) {
		printf(" router");
	}
	if (r->ndm_flags & NTF_PROXY) {
		printf(" proxy");
	}
	if (tb[NDA_CACHEINFO] && xshow_stats) {
		struct nda_cacheinfo *ci = RTA_DATA(tb[NDA_CACHEINFO]);
		int hz = get_hz();

		if (ci->ndm_refcnt)
			printf(" ref %d", ci->ndm_refcnt);
		printf(" used %d/%d/%d", ci->ndm_used/hz,
		       ci->ndm_confirmed/hz, ci->ndm_updated/hz);
	}

	if (tb[NDA_PROBES] && xshow_stats) {
		uint32_t p = rta_getattr_u32(tb[NDA_PROBES]);
		printf(" probes %u", p);
	}

	/*if (r->ndm_state)*/ {
		int nud = r->ndm_state;
		char c = ' ';
#define PRINT_FLAG(f) \
		if (nud & NUD_##f) { \
			printf("%c"#f, c); \
			c = ','; \
		}
		PRINT_FLAG(INCOMPLETE);
		PRINT_FLAG(REACHABLE);
		PRINT_FLAG(STALE);
		PRINT_FLAG(DELAY);
		PRINT_FLAG(PROBE);
		PRINT_FLAG(FAILED);
		PRINT_FLAG(NOARP);
		PRINT_FLAG(PERMANENT);
#undef PRINT_FLAG
	}
	bb_putchar('\n');

	return 0;
}

static void ipneigh_reset_filter(void)
{
	memset(&G_filter, 0, sizeof(G_filter));
	G_filter.state = ~0;
}

#define MAX_ROUNDS	10
/* Return value becomes exitcode. It's okay to not return at all */
static int FAST_FUNC ipneigh_list_or_flush(char **argv, int flush)
{
	static const char keywords[] ALIGN1 =
		/* "ip neigh show/flush" parameters: */
		"to\0" "dev\0"   "nud\0";
	enum {
		KW_to, KW_dev, KW_nud,
	};
	struct rtnl_handle rth;
	struct ndmsg ndm = { 0 };
	char *filter_dev = NULL;
	int state_given = 0;
	int arg;

	ipneigh_reset_filter();

	if (flush && !*argv)
		bb_error_msg_and_die(bb_msg_requires_arg, "\"ip neigh flush\"");

	if (!G_filter.family)
		G_filter.family = preferred_family;

	G_filter.state = (flush) ?
		~(NUD_PERMANENT|NUD_NOARP) : 0xFF & ~NUD_NOARP;

	while (*argv) {
		arg = index_in_substrings(keywords, *argv);
		if (arg == KW_dev) {
			NEXT_ARG();
			filter_dev = *argv;
		} else if (arg == KW_nud) {
			unsigned state;
			NEXT_ARG();
			if (!state_given) {
				state_given = 1;
				G_filter.state = 0;
			}
			if (strcmp(*argv, "all") == 0) {
				state = ~0;
				if (flush)
					state &= ~NUD_NOARP;
			} else {
				state = nud_state_a2n(*argv);
			}
			if (state == 0)
				state = 0x100;
			G_filter.state |= state;
		} else {
			if (arg == KW_to) {
				NEXT_ARG();
			}
			get_prefix(&G_filter.pfx, *argv, G_filter.family);
			if (G_filter.family == AF_UNSPEC)
				G_filter.family = G_filter.pfx.family;
		}
		argv++;
	}

	xrtnl_open(&rth);
	ll_init_map(&rth);

	if (filter_dev)  {
		G_filter.index = xll_name_to_index(filter_dev);
		if (G_filter.index == 0) {
			bb_error_msg_and_die("can't find device '%s'", filter_dev);
		}
	}

	if (flush) {
		int round = 0;
		char flushb[4096-512];
		G_filter.flushb = flushb;
		G_filter.flushp = 0;
		G_filter.flushe = sizeof(flushb);
		G_filter.state &= ~NUD_FAILED;
		G_filter.rth = &rth;

		while (round < MAX_ROUNDS) {
			if (xrtnl_wilddump_request(&rth, G_filter.family, RTM_GETNEIGH) < 0) {
				bb_perror_msg_and_die("can't send dump request");
			}
			G_filter.flushed = 0;
			if (xrtnl_dump_filter(&rth, print_neigh, NULL) < 0) {
				bb_perror_msg_and_die("flush terminated");
			}
			if (G_filter.flushed == 0) {
				if (round == 0)
					puts("Nothing to flush");
				else
					printf("*** Flush is complete after %d round(s) ***\n", round);
				return 0;
			}
			round++;
			if (flush_update() < 0)
				xfunc_die();
			printf("\n*** Round %d, deleting %d entries ***\n", round, G_filter.flushed);
		}
		bb_error_msg_and_die("*** Flush not complete bailing out after %d rounds", MAX_ROUNDS);
	}

	ndm.ndm_family = G_filter.family;

	if (rtnl_dump_request(&rth, RTM_GETNEIGH, &ndm, sizeof(struct ndmsg)) < 0) {
		bb_perror_msg_and_die("can't send dump request");
	}

	if (xrtnl_dump_filter(&rth, print_neigh, NULL) < 0) {
		bb_error_msg_and_die("dump terminated");
	}

	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
int FAST_FUNC do_ipneigh(char **argv)
{
	static const char ip_neigh_commands[] ALIGN1 =
		/*0-1*/	"show\0"  "flush\0";
	int command_num;

	INIT_G();

	if (!*argv)
		return ipneigh_list_or_flush(argv, 0);

	command_num = index_in_substrings(ip_neigh_commands, *argv);
	switch (command_num) {
		case 0: /* show */
			return ipneigh_list_or_flush(argv + 1, 0);
		case 1: /* flush */
			return ipneigh_list_or_flush(argv + 1, 1);
	}
	invarg_1_to_2(*argv, applet_name);
	return 1;
}
