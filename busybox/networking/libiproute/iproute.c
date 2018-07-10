/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 *
 * Rani Assaf <rani@magic.metawire.com> 980929: resolve addresses
 * Kunihiro Ishiguro <kunihiro@zebra.org> 001102: rtnh_ifindex was not initialized
 */
#include "ip_common.h"  /* #include "libbb.h" is inside */
#include "common_bufsiz.h"
#include "rt_names.h"
#include "utils.h"

#include <linux/version.h>
/* RTA_TABLE is not a define, can't test with ifdef. */
/* As a proxy, test which kernels toolchain expects: */
#define HAVE_RTA_TABLE (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))

#ifndef RTAX_RTTVAR
#define RTAX_RTTVAR RTAX_HOPS
#endif


struct filter_t {
	int tb;
	smallint flushed;
	char *flushb;
	int flushp;
	int flushe;
	struct rtnl_handle *rth;
	//int protocol, protocolmask; - write-only fields?!
	int scope, scopemask;
	//int type; - read-only
	//int typemask; - unused
	//int tos, tosmask; - unused
	int iif;
	int oif;
	//int realm, realmmask; - unused
	//inet_prefix rprefsrc; - read-only
	inet_prefix rvia;
	inet_prefix rdst;
	inet_prefix mdst;
	inet_prefix rsrc;
	inet_prefix msrc;
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

static int FAST_FUNC print_route(const struct sockaddr_nl *who UNUSED_PARAM,
		struct nlmsghdr *n, void *arg UNUSED_PARAM)
{
	struct rtmsg *r = NLMSG_DATA(n);
	int len = n->nlmsg_len;
	struct rtattr *tb[RTA_MAX+1];
	inet_prefix dst;
	inet_prefix src;
	int host_len = -1;
	uint32_t tid;

	if (n->nlmsg_type != RTM_NEWROUTE && n->nlmsg_type != RTM_DELROUTE) {
		fprintf(stderr, "Not a route: %08x %08x %08x\n",
			n->nlmsg_len, n->nlmsg_type, n->nlmsg_flags);
		return 0;
	}
	if (G_filter.flushb && n->nlmsg_type != RTM_NEWROUTE)
		return 0;
	len -= NLMSG_LENGTH(sizeof(*r));
	if (len < 0)
		bb_error_msg_and_die("wrong nlmsg len %d", len);

	//memset(tb, 0, sizeof(tb)); - parse_rtattr does this
	parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

#if HAVE_RTA_TABLE
	if (tb[RTA_TABLE])
		tid = *(uint32_t *)RTA_DATA(tb[RTA_TABLE]);
	else
#endif
		tid = r->rtm_table;

	if (r->rtm_family == AF_INET6)
		host_len = 128;
	else if (r->rtm_family == AF_INET)
		host_len = 32;

	if (r->rtm_family == AF_INET6) {
		if (G_filter.tb) {
			if (G_filter.tb < 0) {
				if (!(r->rtm_flags & RTM_F_CLONED)) {
					return 0;
				}
			} else {
				if (r->rtm_flags & RTM_F_CLONED) {
					return 0;
				}
				if (G_filter.tb == RT_TABLE_LOCAL) {
					if (r->rtm_type != RTN_LOCAL) {
						return 0;
					}
				} else if (G_filter.tb == RT_TABLE_MAIN) {
					if (r->rtm_type == RTN_LOCAL) {
						return 0;
					}
				} else {
					return 0;
				}
			}
		}
	} else {
		if (G_filter.tb > 0 && G_filter.tb != tid) {
			return 0;
		}
	}
	if ((G_filter.scope ^ r->rtm_scope) & G_filter.scopemask)
		return 0;
	if (G_filter.rdst.family
	 && (r->rtm_family != G_filter.rdst.family || G_filter.rdst.bitlen > r->rtm_dst_len)
	) {
		return 0;
	}
	if (G_filter.mdst.family
	 && (r->rtm_family != G_filter.mdst.family
	    || (G_filter.mdst.bitlen >= 0 && G_filter.mdst.bitlen < r->rtm_dst_len)
	    )
	) {
		return 0;
	}
	if (G_filter.rsrc.family
	 && (r->rtm_family != G_filter.rsrc.family || G_filter.rsrc.bitlen > r->rtm_src_len)
	) {
		return 0;
	}
	if (G_filter.msrc.family
	 && (r->rtm_family != G_filter.msrc.family
	    || (G_filter.msrc.bitlen >= 0 && G_filter.msrc.bitlen < r->rtm_src_len)
	    )
	) {
		return 0;
	}

	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));

	if (tb[RTA_SRC]) {
		src.bitlen = r->rtm_src_len;
		src.bytelen = (r->rtm_family == AF_INET6 ? 16 : 4);
		memcpy(src.data, RTA_DATA(tb[RTA_SRC]), src.bytelen);
	}
	if (tb[RTA_DST]) {
		dst.bitlen = r->rtm_dst_len;
		dst.bytelen = (r->rtm_family == AF_INET6 ? 16 : 4);
		memcpy(dst.data, RTA_DATA(tb[RTA_DST]), dst.bytelen);
	}

	if (G_filter.rdst.family
	 && inet_addr_match(&dst, &G_filter.rdst, G_filter.rdst.bitlen)
	) {
		return 0;
	}
	if (G_filter.mdst.family
	 && G_filter.mdst.bitlen >= 0
	 && inet_addr_match(&dst, &G_filter.mdst, r->rtm_dst_len)
	) {
		return 0;
	}
	if (G_filter.rsrc.family
	 && inet_addr_match(&src, &G_filter.rsrc, G_filter.rsrc.bitlen)
	) {
		return 0;
	}
	if (G_filter.msrc.family && G_filter.msrc.bitlen >= 0
	 && inet_addr_match(&src, &G_filter.msrc, r->rtm_src_len)
	) {
		return 0;
	}
	if (G_filter.oif != 0) {
		if (!tb[RTA_OIF])
			return 0;
		if (G_filter.oif != *(int*)RTA_DATA(tb[RTA_OIF]))
			return 0;
	}

	if (G_filter.flushb) {
		struct nlmsghdr *fn;

		/* We are creating route flush commands */

		if (r->rtm_family == AF_INET6
		 && r->rtm_dst_len == 0
		 && r->rtm_type == RTN_UNREACHABLE
		 && tb[RTA_PRIORITY]
		 && *(int*)RTA_DATA(tb[RTA_PRIORITY]) == -1
		) {
			return 0;
		}

		if (NLMSG_ALIGN(G_filter.flushp) + n->nlmsg_len > G_filter.flushe) {
			if (flush_update())
				xfunc_die();
		}
		fn = (void*)(G_filter.flushb + NLMSG_ALIGN(G_filter.flushp));
		memcpy(fn, n, n->nlmsg_len);
		fn->nlmsg_type = RTM_DELROUTE;
		fn->nlmsg_flags = NLM_F_REQUEST;
		fn->nlmsg_seq = ++G_filter.rth->seq;
		G_filter.flushp = (((char*)fn) + n->nlmsg_len) - G_filter.flushb;
		G_filter.flushed = 1;
		return 0;
	}

	/* We are printing routes */

	if (n->nlmsg_type == RTM_DELROUTE) {
		printf("Deleted ");
	}
	if (r->rtm_type != RTN_UNICAST /* && !G_filter.type - always 0 */) {
		printf("%s ", rtnl_rtntype_n2a(r->rtm_type));
	}

	if (tb[RTA_DST]) {
		if (r->rtm_dst_len != host_len) {
			printf("%s/%u ",
				rt_addr_n2a(r->rtm_family, RTA_DATA(tb[RTA_DST])),
				r->rtm_dst_len
			);
		} else {
			printf("%s ", format_host(r->rtm_family,
						RTA_PAYLOAD(tb[RTA_DST]),
						RTA_DATA(tb[RTA_DST]))
			);
		}
	} else if (r->rtm_dst_len) {
		printf("0/%d ", r->rtm_dst_len);
	} else {
		printf("default ");
	}
	if (tb[RTA_SRC]) {
		if (r->rtm_src_len != host_len) {
			printf("from %s/%u ",
				rt_addr_n2a(r->rtm_family, RTA_DATA(tb[RTA_SRC])),
				r->rtm_src_len
			);
		} else {
			printf("from %s ", format_host(r->rtm_family,
						RTA_PAYLOAD(tb[RTA_SRC]),
						RTA_DATA(tb[RTA_SRC]))
			);
		}
	} else if (r->rtm_src_len) {
		printf("from 0/%u ", r->rtm_src_len);
	}
	if (tb[RTA_GATEWAY] && G_filter.rvia.bitlen != host_len) {
		printf("via %s ", format_host(r->rtm_family,
					RTA_PAYLOAD(tb[RTA_GATEWAY]),
					RTA_DATA(tb[RTA_GATEWAY]))
		);
	}
	if (tb[RTA_OIF]) {
		printf("dev %s ", ll_index_to_name(*(int*)RTA_DATA(tb[RTA_OIF])));
	}
#if ENABLE_FEATURE_IP_RULE
	if (tid && tid != RT_TABLE_MAIN && !G_filter.tb)
		printf("table %s ", rtnl_rttable_n2a(tid));
#endif

	/* Todo: parse & show "proto kernel" here */
	if (!(r->rtm_flags & RTM_F_CLONED)) {
		if ((r->rtm_scope != RT_SCOPE_UNIVERSE) && G_filter.scopemask != -1)
			printf("scope %s ", rtnl_rtscope_n2a(r->rtm_scope));
	}

	if (tb[RTA_PREFSRC] && /*G_filter.rprefsrc.bitlen - always 0*/ 0 != host_len) {
		/* Do not use format_host(). It is our local addr
		   and symbolic name will not be useful.
		 */
		printf(" src %s ", rt_addr_n2a(r->rtm_family,
					RTA_DATA(tb[RTA_PREFSRC])));
	}
	if (tb[RTA_PRIORITY]) {
		printf(" metric %d ", *(uint32_t*)RTA_DATA(tb[RTA_PRIORITY]));
	}
	if (r->rtm_flags & RTNH_F_DEAD) {
		printf("dead ");
	}
	if (r->rtm_flags & RTNH_F_ONLINK) {
		printf("onlink ");
	}
	if (r->rtm_flags & RTNH_F_PERVASIVE) {
		printf("pervasive ");
	}
	if (r->rtm_flags & RTM_F_NOTIFY) {
		printf("notify ");
	}

	if (r->rtm_family == AF_INET6) {
		struct rta_cacheinfo *ci = NULL;
		if (tb[RTA_CACHEINFO]) {
			ci = RTA_DATA(tb[RTA_CACHEINFO]);
		}
		if ((r->rtm_flags & RTM_F_CLONED) || (ci && ci->rta_expires)) {
			if (r->rtm_flags & RTM_F_CLONED) {
				printf("%c    cache ", _SL_);
			}
			if (ci->rta_expires) {
				printf(" expires %dsec", ci->rta_expires / get_hz());
			}
			if (ci->rta_error != 0) {
				printf(" error %d", ci->rta_error);
			}
		} else if (ci) {
			if (ci->rta_error != 0)
				printf(" error %d", ci->rta_error);
		}
	}
	if (tb[RTA_IIF] && G_filter.iif == 0) {
		printf(" iif %s", ll_index_to_name(*(int*)RTA_DATA(tb[RTA_IIF])));
	}
	bb_putchar('\n');
	return 0;
}

static int str_is_lock(const char *str)
{
	return strcmp(str, "lock") == 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
static int iproute_modify(int cmd, unsigned flags, char **argv)
{
	/* If you add stuff here, update iproute_full_usage */
	static const char keywords[] ALIGN1 =
		"src\0""via\0"
		"mtu\0""advmss\0"
		"scope\0""protocol\0"IF_FEATURE_IP_RULE("table\0")
		"dev\0""oif\0""to\0""metric\0""onlink\0";
#define keyword_via    (keywords       + sizeof("src"))
#define keyword_mtu    (keyword_via    + sizeof("via"))
#define keyword_advmss (keyword_mtu    + sizeof("mtu"))
#define keyword_scope  (keyword_advmss + sizeof("advmss"))
#define keyword_proto  (keyword_scope  + sizeof("scope"))
#define keyword_table  (keyword_proto  + sizeof("protocol"))
	enum {
		ARG_src,
		ARG_via,
		ARG_mtu,
		ARG_advmss,
		ARG_scope,
		ARG_protocol,
IF_FEATURE_IP_RULE(ARG_table,)
		ARG_dev,
		ARG_oif,
		ARG_to,
		ARG_metric,
		ARG_onlink,
	};
	enum {
		gw_ok = 1 << 0,
		dst_ok = 1 << 1,
		proto_ok = 1 << 2,
		type_ok = 1 << 3
	};
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr n;
		struct rtmsg    r;
		char            buf[1024];
	} req;
	char mxbuf[256];
	struct rtattr * mxrta = (void*)mxbuf;
	unsigned mxlock = 0;
	char *d = NULL;
	smalluint ok = 0;
	smalluint scope_ok = 0;
	int arg;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | flags;
	req.n.nlmsg_type = cmd;
	req.r.rtm_family = preferred_family;
	if (RT_TABLE_MAIN != 0) /* if it is zero, memset already did it */
		req.r.rtm_table = RT_TABLE_MAIN;
	if (RT_SCOPE_NOWHERE != 0)
		req.r.rtm_scope = RT_SCOPE_NOWHERE;

	if (cmd != RTM_DELROUTE) {
		req.r.rtm_scope = RT_SCOPE_UNIVERSE;
		if (RTPROT_BOOT != 0)
			req.r.rtm_protocol = RTPROT_BOOT;
		if (RTN_UNICAST != 0)
			req.r.rtm_type = RTN_UNICAST;
	}

	mxrta->rta_type = RTA_METRICS;
	mxrta->rta_len = RTA_LENGTH(0);

	while (*argv) {
		arg = index_in_substrings(keywords, *argv);
		if (arg == ARG_src) {
			inet_prefix addr;
			NEXT_ARG();
			get_addr(&addr, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			addattr_l(&req.n, sizeof(req), RTA_PREFSRC, &addr.data, addr.bytelen);
		} else if (arg == ARG_via) {
			inet_prefix addr;
			ok |= gw_ok;
			NEXT_ARG();
			get_addr(&addr, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC) {
				req.r.rtm_family = addr.family;
			}
			addattr_l(&req.n, sizeof(req), RTA_GATEWAY, &addr.data, addr.bytelen);
		} else if (arg == ARG_mtu) {
			unsigned mtu;
			NEXT_ARG();
			if (str_is_lock(*argv)) {
				mxlock |= (1 << RTAX_MTU);
				NEXT_ARG();
			}
			mtu = get_unsigned(*argv, keyword_mtu);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_MTU, mtu);
		} else if (arg == ARG_advmss) {
			unsigned mss;
			NEXT_ARG();
			if (str_is_lock(*argv)) {
				mxlock |= (1 << RTAX_ADVMSS);
				NEXT_ARG();
			}
			mss = get_unsigned(*argv, keyword_advmss);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_ADVMSS, mss);
		} else if (arg == ARG_scope) {
			uint32_t scope;
			NEXT_ARG();
			if (rtnl_rtscope_a2n(&scope, *argv))
				invarg_1_to_2(*argv, keyword_scope);
			req.r.rtm_scope = scope;
			scope_ok = 1;
		} else if (arg == ARG_protocol) {
			uint32_t prot;
			NEXT_ARG();
			if (rtnl_rtprot_a2n(&prot, *argv))
				invarg_1_to_2(*argv, keyword_proto);
			req.r.rtm_protocol = prot;
			ok |= proto_ok;
#if ENABLE_FEATURE_IP_RULE
		} else if (arg == ARG_table) {
			uint32_t tid;
			NEXT_ARG();
			if (rtnl_rttable_a2n(&tid, *argv))
				invarg_1_to_2(*argv, keyword_table);
#if HAVE_RTA_TABLE
			if (tid > 255) {
				req.r.rtm_table = RT_TABLE_UNSPEC;
				addattr32(&req.n, sizeof(req), RTA_TABLE, tid);
			} else
#endif
				req.r.rtm_table = tid;
#endif
		} else if (arg == ARG_dev || arg == ARG_oif) {
			NEXT_ARG();
			d = *argv;
		} else if (arg == ARG_metric) {
//TODO: "metric", "priority" and "preference" are synonyms
			uint32_t metric;
			NEXT_ARG();
			metric = get_u32(*argv, "metric");
			addattr32(&req.n, sizeof(req), RTA_PRIORITY, metric);
		} else if (arg == ARG_onlink) {
			req.r.rtm_flags |= RTNH_F_ONLINK;
		} else {
			int type;
			inet_prefix dst;

			if (arg == ARG_to) {
				NEXT_ARG();
			}
			if ((**argv < '0' || **argv > '9')
			 && rtnl_rtntype_a2n(&type, *argv) == 0
			) {
				NEXT_ARG();
				req.r.rtm_type = type;
				ok |= type_ok;
			}

			if (ok & dst_ok) {
				duparg2("to", *argv);
			}
			get_prefix(&dst, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC) {
				req.r.rtm_family = dst.family;
			}
			req.r.rtm_dst_len = dst.bitlen;
			ok |= dst_ok;
			if (dst.bytelen) {
				addattr_l(&req.n, sizeof(req), RTA_DST, &dst.data, dst.bytelen);
			}
		}
/* Other keywords recognized by iproute2-3.19.0: */
#if 0
		} else if (strcmp(*argv, "from") == 0) {
			inet_prefix addr;
			NEXT_ARG();
			get_prefix(&addr, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			if (addr.bytelen)
				addattr_l(&req.n, sizeof(req), RTA_SRC, &addr.data, addr.bytelen);
			req.r.rtm_src_len = addr.bitlen;
		} else if (strcmp(*argv, "tos") == 0 ||
			   matches(*argv, "dsfield") == 0) {
			__u32 tos;
			NEXT_ARG();
			if (rtnl_dsfield_a2n(&tos, *argv))
				invarg("\"tos\" value is invalid\n", *argv);
			req.r.rtm_tos = tos;
		} else if (strcmp(*argv, "hoplimit") == 0) {
			unsigned hoplimit;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_HOPLIMIT);
				NEXT_ARG();
			}
			if (get_unsigned(&hoplimit, *argv, 0))
				invarg("\"hoplimit\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_HOPLIMIT, hoplimit);
		} else if (matches(*argv, "reordering") == 0) {
			unsigned reord;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_REORDERING);
				NEXT_ARG();
			}
			if (get_unsigned(&reord, *argv, 0))
				invarg("\"reordering\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_REORDERING, reord);
		} else if (strcmp(*argv, "rtt") == 0) {
			unsigned rtt;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_RTT);
				NEXT_ARG();
			}
			if (get_time_rtt(&rtt, *argv, &raw))
				invarg("\"rtt\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_RTT,
				(raw) ? rtt : rtt * 8);
		} else if (strcmp(*argv, "rto_min") == 0) {
			unsigned rto_min;
			NEXT_ARG();
			mxlock |= (1<<RTAX_RTO_MIN);
			if (get_time_rtt(&rto_min, *argv, &raw))
				invarg("\"rto_min\" value is invalid\n",
				       *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_RTO_MIN,
				      rto_min);
		} else if (matches(*argv, "window") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_WINDOW);
				NEXT_ARG();
			}
			if (get_unsigned(&win, *argv, 0))
				invarg("\"window\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_WINDOW, win);
		} else if (matches(*argv, "cwnd") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_CWND);
				NEXT_ARG();
			}
			if (get_unsigned(&win, *argv, 0))
				invarg("\"cwnd\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_CWND, win);
		} else if (matches(*argv, "initcwnd") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_INITCWND);
				NEXT_ARG();
			}
			if (get_unsigned(&win, *argv, 0))
				invarg("\"initcwnd\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_INITCWND, win);
		} else if (matches(*argv, "initrwnd") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_INITRWND);
				NEXT_ARG();
			}
			if (get_unsigned(&win, *argv, 0))
				invarg("\"initrwnd\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_INITRWND, win);
		} else if (matches(*argv, "features") == 0) {
			unsigned int features = 0;

			while (argc > 0) {
				NEXT_ARG();

				if (strcmp(*argv, "ecn") == 0)
					features |= RTAX_FEATURE_ECN;
				else
					invarg("\"features\" value not valid\n", *argv);
				break;
			}

			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_FEATURES, features);
		} else if (matches(*argv, "quickack") == 0) {
			unsigned quickack;
			NEXT_ARG();
			if (get_unsigned(&quickack, *argv, 0))
				invarg("\"quickack\" value is invalid\n", *argv);
			if (quickack != 1 && quickack != 0)
				invarg("\"quickack\" value should be 0 or 1\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_QUICKACK, quickack);
		} else if (matches(*argv, "rttvar") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_RTTVAR);
				NEXT_ARG();
			}
			if (get_time_rtt(&win, *argv, &raw))
				invarg("\"rttvar\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_RTTVAR,
				(raw) ? win : win * 4);
		} else if (matches(*argv, "ssthresh") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(*argv, "lock") == 0) {
				mxlock |= (1<<RTAX_SSTHRESH);
				NEXT_ARG();
			}
			if (get_unsigned(&win, *argv, 0))
				invarg("\"ssthresh\" value is invalid\n", *argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_SSTHRESH, win);
		} else if (matches(*argv, "realms") == 0) {
			__u32 realm;
			NEXT_ARG();
			if (get_rt_realms(&realm, *argv))
				invarg("\"realm\" value is invalid\n", *argv);
			addattr32(&req.n, sizeof(req), RTA_FLOW, realm);
		} else if (strcmp(*argv, "nexthop") == 0) {
			nhs_ok = 1;
			break;
		}
#endif
		argv++;
	}

	xrtnl_open(&rth);

	if (d)  {
		int idx;

		ll_init_map(&rth);

		if (d) {
			idx = xll_name_to_index(d);
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (mxrta->rta_len > RTA_LENGTH(0)) {
		if (mxlock) {
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_LOCK, mxlock);
		}
		addattr_l(&req.n, sizeof(req), RTA_METRICS, RTA_DATA(mxrta), RTA_PAYLOAD(mxrta));
	}

	if (!scope_ok) {
		if (req.r.rtm_type == RTN_LOCAL || req.r.rtm_type == RTN_NAT)
			req.r.rtm_scope = RT_SCOPE_HOST;
		else
		if (req.r.rtm_type == RTN_BROADCAST
		 || req.r.rtm_type == RTN_MULTICAST
		 || req.r.rtm_type == RTN_ANYCAST
		) {
			req.r.rtm_scope = RT_SCOPE_LINK;
		}
		else if (req.r.rtm_type == RTN_UNICAST || req.r.rtm_type == RTN_UNSPEC) {
			if (cmd == RTM_DELROUTE)
				req.r.rtm_scope = RT_SCOPE_NOWHERE;
			else if (!(ok & gw_ok))
				req.r.rtm_scope = RT_SCOPE_LINK;
		}
	}

	if (req.r.rtm_family == AF_UNSPEC) {
		req.r.rtm_family = AF_INET;
	}

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL, NULL, NULL) < 0) {
		return 2;
	}

	return 0;
}

static int rtnl_rtcache_request(struct rtnl_handle *rth, int family)
{
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
	} req;
	struct sockaddr_nl nladdr;

	memset(&nladdr, 0, sizeof(nladdr));
	memset(&req, 0, sizeof(req));
	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = sizeof(req);
	if (RTM_GETROUTE)
		req.nlh.nlmsg_type = RTM_GETROUTE;
	if (NLM_F_ROOT | NLM_F_REQUEST)
		req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_REQUEST;
	/*req.nlh.nlmsg_pid = 0; - memset did it already */
	req.nlh.nlmsg_seq = rth->dump = ++rth->seq;
	req.rtm.rtm_family = family;
	if (RTM_F_CLONED)
		req.rtm.rtm_flags = RTM_F_CLONED;

	return xsendto(rth->fd, (void*)&req, sizeof(req), (struct sockaddr*)&nladdr, sizeof(nladdr));
}

static void iproute_flush_cache(void)
{
	static const char fn[] ALIGN1 = "/proc/sys/net/ipv4/route/flush";
	int flush_fd = open_or_warn(fn, O_WRONLY);

	if (flush_fd < 0) {
		return;
	}

	if (write(flush_fd, "-1", 2) < 2) {
		bb_perror_msg("can't flush routing cache");
		return;
	}
	close(flush_fd);
}

static void iproute_reset_filter(void)
{
	memset(&G_filter, 0, sizeof(G_filter));
	G_filter.mdst.bitlen = -1;
	G_filter.msrc.bitlen = -1;
}

/* Return value becomes exitcode. It's okay to not return at all */
static int iproute_list_or_flush(char **argv, int flush)
{
	int do_ipv6 = preferred_family;
	struct rtnl_handle rth;
	char *id = NULL;
	char *od = NULL;
	static const char keywords[] ALIGN1 =
		/* If you add stuff here, update iproute_full_usage */
		/* "ip route list/flush" parameters: */
		"protocol\0" "dev\0"   "oif\0"   "iif\0"
		"via\0"      "table\0" "cache\0"
		"from\0"     "to\0"    "scope\0"
		/* and possible further keywords */
		"all\0"
		"root\0"
		"match\0"
		"exact\0"
		"main\0"
		;
	enum {
		KW_proto, KW_dev,   KW_oif,  KW_iif,
		KW_via,   KW_table, KW_cache,
		KW_from,  KW_to,    KW_scope,
		/* */
		KW_all,
		KW_root,
		KW_match,
		KW_exact,
		KW_main,
	};
	int arg, parm;

	iproute_reset_filter();
	G_filter.tb = RT_TABLE_MAIN;

	if (flush && !*argv)
		bb_error_msg_and_die(bb_msg_requires_arg, "\"ip route flush\"");

	while (*argv) {
		arg = index_in_substrings(keywords, *argv);
		if (arg == KW_proto) {
			uint32_t prot = 0;
			NEXT_ARG();
			//G_filter.protocolmask = -1;
			if (rtnl_rtprot_a2n(&prot, *argv)) {
				if (index_in_strings(keywords, *argv) != KW_all)
					invarg_1_to_2(*argv, "protocol");
				prot = 0;
				//G_filter.protocolmask = 0;
			}
			//G_filter.protocol = prot;
		} else if (arg == KW_dev || arg == KW_oif) {
			NEXT_ARG();
			od = *argv;
		} else if (arg == KW_iif) {
			NEXT_ARG();
			id = *argv;
		} else if (arg == KW_via) {
			NEXT_ARG();
			get_prefix(&G_filter.rvia, *argv, do_ipv6);
		} else if (arg == KW_table) { /* table all/cache/main */
			NEXT_ARG();
			parm = index_in_substrings(keywords, *argv);
			if (parm == KW_cache)
				G_filter.tb = -1;
			else if (parm == KW_all)
				G_filter.tb = 0;
			else if (parm != KW_main) {
#if ENABLE_FEATURE_IP_RULE
				uint32_t tid;
				if (rtnl_rttable_a2n(&tid, *argv))
					invarg_1_to_2(*argv, "table");
				G_filter.tb = tid;
#else
				invarg_1_to_2(*argv, "table");
#endif
			}
		} else if (arg == KW_cache) {
			/* The command 'ip route flush cache' is used by OpenSWAN.
			 * Assuming it's a synonym for 'ip route flush table cache' */
			G_filter.tb = -1;
		} else if (arg == KW_scope) {
			uint32_t scope;
			NEXT_ARG();
			G_filter.scopemask = -1;
			if (rtnl_rtscope_a2n(&scope, *argv)) {
				if (strcmp(*argv, "all") != 0)
					invarg_1_to_2(*argv, "scope");
				scope = RT_SCOPE_NOWHERE;
				G_filter.scopemask = 0;
			}
			G_filter.scope = scope;
		} else if (arg == KW_from) {
			NEXT_ARG();
			parm = index_in_substrings(keywords, *argv);
			if (parm == KW_root) {
				NEXT_ARG();
				get_prefix(&G_filter.rsrc, *argv, do_ipv6);
			} else if (parm == KW_match) {
				NEXT_ARG();
				get_prefix(&G_filter.msrc, *argv, do_ipv6);
			} else {
				if (parm == KW_exact)
					NEXT_ARG();
				get_prefix(&G_filter.msrc, *argv, do_ipv6);
				G_filter.rsrc = G_filter.msrc;
			}
		} else { /* "to" is the default parameter */
			if (arg == KW_to) {
				NEXT_ARG();
				arg = index_in_substrings(keywords, *argv);
			}
			/* parm = arg; - would be more plausible, but we reuse 'arg' here */
			if (arg == KW_root) {
				NEXT_ARG();
				get_prefix(&G_filter.rdst, *argv, do_ipv6);
			} else if (arg == KW_match) {
				NEXT_ARG();
				get_prefix(&G_filter.mdst, *argv, do_ipv6);
			} else { /* "to exact" is the default */
				if (arg == KW_exact)
					NEXT_ARG();
				get_prefix(&G_filter.mdst, *argv, do_ipv6);
				G_filter.rdst = G_filter.mdst;
			}
		}
		argv++;
	}

	if (do_ipv6 == AF_UNSPEC && G_filter.tb) {
		do_ipv6 = AF_INET;
	}

	xrtnl_open(&rth);
	ll_init_map(&rth);

	if (id || od)  {
		int idx;

		if (id) {
			idx = xll_name_to_index(id);
			G_filter.iif = idx;
		}
		if (od) {
			idx = xll_name_to_index(od);
			G_filter.oif = idx;
		}
	}

	if (flush) {
		char flushb[4096-512];

		if (G_filter.tb == -1) { /* "flush table cache" */
			if (do_ipv6 != AF_INET6)
				iproute_flush_cache();
			if (do_ipv6 == AF_INET)
				return 0;
		}

		G_filter.flushb = flushb;
		G_filter.flushp = 0;
		G_filter.flushe = sizeof(flushb);
		G_filter.rth = &rth;

		for (;;) {
			xrtnl_wilddump_request(&rth, do_ipv6, RTM_GETROUTE);
			G_filter.flushed = 0;
			xrtnl_dump_filter(&rth, print_route, NULL);
			if (G_filter.flushed == 0)
				return 0;
			if (flush_update())
				return 1;
		}
	}

	if (G_filter.tb != -1) {
		xrtnl_wilddump_request(&rth, do_ipv6, RTM_GETROUTE);
	} else if (rtnl_rtcache_request(&rth, do_ipv6) < 0) {
		bb_perror_msg_and_die("can't send dump request");
	}
	xrtnl_dump_filter(&rth, print_route, NULL);

	return 0;
}


/* Return value becomes exitcode. It's okay to not return at all */
static int iproute_get(char **argv)
{
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr n;
		struct rtmsg    r;
		char            buf[1024];
	} req;
	char *idev = NULL;
	char *odev = NULL;
	bool connected = 0;
	bool from_ok = 0;
	static const char options[] ALIGN1 =
		"from\0""iif\0""oif\0""dev\0""notify\0""connected\0""to\0";

	memset(&req, 0, sizeof(req));

	iproute_reset_filter();

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	if (NLM_F_REQUEST)
		req.n.nlmsg_flags = NLM_F_REQUEST;
	if (RTM_GETROUTE)
		req.n.nlmsg_type = RTM_GETROUTE;
	req.r.rtm_family = preferred_family;
	/*req.r.rtm_table = 0; - memset did this already */
	/*req.r.rtm_protocol = 0;*/
	/*req.r.rtm_scope = 0;*/
	/*req.r.rtm_type = 0;*/
	/*req.r.rtm_src_len = 0;*/
	/*req.r.rtm_dst_len = 0;*/
	/*req.r.rtm_tos = 0;*/

	while (*argv) {
		switch (index_in_strings(options, *argv)) {
			case 0: /* from */
			{
				inet_prefix addr;
				NEXT_ARG();
				from_ok = 1;
				get_prefix(&addr, *argv, req.r.rtm_family);
				if (req.r.rtm_family == AF_UNSPEC) {
					req.r.rtm_family = addr.family;
				}
				if (addr.bytelen) {
					addattr_l(&req.n, sizeof(req), RTA_SRC, &addr.data, addr.bytelen);
				}
				req.r.rtm_src_len = addr.bitlen;
				break;
			}
			case 1: /* iif */
				NEXT_ARG();
				idev = *argv;
				break;
			case 2: /* oif */
			case 3: /* dev */
				NEXT_ARG();
				odev = *argv;
				break;
			case 4: /* notify */
				req.r.rtm_flags |= RTM_F_NOTIFY;
				break;
			case 5: /* connected */
				connected = 1;
				break;
			case 6: /* to */
				NEXT_ARG();
			default:
			{
				inet_prefix addr;
				get_prefix(&addr, *argv, req.r.rtm_family);
				if (req.r.rtm_family == AF_UNSPEC) {
					req.r.rtm_family = addr.family;
				}
				if (addr.bytelen) {
					addattr_l(&req.n, sizeof(req), RTA_DST, &addr.data, addr.bytelen);
				}
				req.r.rtm_dst_len = addr.bitlen;
			}
		}
		argv++;
	}

	if (req.r.rtm_dst_len == 0) {
		bb_error_msg_and_die("need at least destination address");
	}

	xrtnl_open(&rth);

	ll_init_map(&rth);

	if (idev || odev)  {
		int idx;

		if (idev) {
			idx = xll_name_to_index(idev);
			addattr32(&req.n, sizeof(req), RTA_IIF, idx);
		}
		if (odev) {
			idx = xll_name_to_index(odev);
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (req.r.rtm_family == AF_UNSPEC) {
		req.r.rtm_family = AF_INET;
	}

	if (rtnl_talk(&rth, &req.n, 0, 0, &req.n, NULL, NULL) < 0) {
		return 2;
	}

	if (connected && !from_ok) {
		struct rtmsg *r = NLMSG_DATA(&req.n);
		int len = req.n.nlmsg_len;
		struct rtattr * tb[RTA_MAX+1];

		print_route(NULL, &req.n, NULL);

		if (req.n.nlmsg_type != RTM_NEWROUTE) {
			bb_error_msg_and_die("not a route?");
		}
		len -= NLMSG_LENGTH(sizeof(*r));
		if (len < 0) {
			bb_error_msg_and_die("wrong len %d", len);
		}

		//memset(tb, 0, sizeof(tb)); - parse_rtattr does this
		parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

		if (tb[RTA_PREFSRC]) {
			tb[RTA_PREFSRC]->rta_type = RTA_SRC;
			r->rtm_src_len = 8*RTA_PAYLOAD(tb[RTA_PREFSRC]);
		} else if (!tb[RTA_SRC]) {
			bb_error_msg_and_die("can't connect the route");
		}
		if (!odev && tb[RTA_OIF]) {
			tb[RTA_OIF]->rta_type = 0;
		}
		if (tb[RTA_GATEWAY]) {
			tb[RTA_GATEWAY]->rta_type = 0;
		}
		if (!idev && tb[RTA_IIF]) {
			tb[RTA_IIF]->rta_type = 0;
		}
		req.n.nlmsg_flags = NLM_F_REQUEST;
		req.n.nlmsg_type = RTM_GETROUTE;

		if (rtnl_talk(&rth, &req.n, 0, 0, &req.n, NULL, NULL) < 0) {
			return 2;
		}
	}
	print_route(NULL, &req.n, NULL);
	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
int FAST_FUNC do_iproute(char **argv)
{
	static const char ip_route_commands[] ALIGN1 =
		"a\0""add\0""append\0""change\0""chg\0"
		"delete\0""get\0""list\0""show\0"
		"prepend\0""replace\0""test\0""flush\0"
	;
	enum {
		CMD_a = 0, CMD_add, CMD_append, CMD_change, CMD_chg,
		CMD_delete, CMD_get, CMD_list, CMD_show,
		CMD_prepend, CMD_replace, CMD_test, CMD_flush,
	};
	int command_num;
	unsigned flags = 0;
	int cmd = RTM_NEWROUTE;

	INIT_G();

	if (!*argv)
		return iproute_list_or_flush(argv, 0);

	/* "Standard" 'ip r a' treats 'a' as 'add', not 'append' */
	/* It probably means that it is using "first match" rule */
	command_num = index_in_substrings(ip_route_commands, *argv);

	switch (command_num) {
		case CMD_a:
		case CMD_add:
			flags = NLM_F_CREATE|NLM_F_EXCL;
			break;
		case CMD_append:
			flags = NLM_F_CREATE|NLM_F_APPEND;
			break;
		case CMD_change:
		case CMD_chg:
			flags = NLM_F_REPLACE;
			break;
		case CMD_delete:
			cmd = RTM_DELROUTE;
			break;
		case CMD_get:
			return iproute_get(argv + 1);
		case CMD_list:
		case CMD_show:
			return iproute_list_or_flush(argv + 1, 0);
		case CMD_prepend:
			flags = NLM_F_CREATE;
			break;
		case CMD_replace:
			flags = NLM_F_CREATE|NLM_F_REPLACE;
			break;
		case CMD_test:
			flags = NLM_F_EXCL;
			break;
		case CMD_flush:
			return iproute_list_or_flush(argv + 1, 1);
		default:
			invarg_1_to_2(*argv, applet_name);
	}

	return iproute_modify(cmd, flags, argv + 1);
}
