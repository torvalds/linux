/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 * Laszlo Valko <valko@linux.karinthy.hu> 990223: address label must be zero terminated
 */
#include <fnmatch.h>
#include <net/if.h>
#include <net/if_arp.h>

#include "ip_common.h"  /* #include "libbb.h" is inside */
#include "common_bufsiz.h"
#include "rt_names.h"
#include "utils.h"

#ifndef IFF_LOWER_UP
/* from linux/if.h */
#define IFF_LOWER_UP  0x10000  /* driver signals L1 up */
#endif

struct filter_t {
	char *label;
	char *flushb;
	struct rtnl_handle *rth;
	int scope, scopemask;
	int flags, flagmask;
	int flushp;
	int flushe;
	int ifindex;
	family_t family;
	smallint showqueue;
	smallint oneline;
	smallint up;
	smallint flushed;
	inet_prefix pfx;
} FIX_ALIASING;
typedef struct filter_t filter_t;

#define G_filter (*(filter_t*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void print_link_flags(unsigned flags, unsigned mdown)
{
	static const int flag_masks[] = {
		IFF_LOOPBACK, IFF_BROADCAST, IFF_POINTOPOINT,
		IFF_MULTICAST, IFF_NOARP, IFF_UP, IFF_LOWER_UP };
	static const char flag_labels[] ALIGN1 =
		"LOOPBACK\0""BROADCAST\0""POINTOPOINT\0"
		"MULTICAST\0""NOARP\0""UP\0""LOWER_UP\0";

	bb_putchar('<');
	if (flags & IFF_UP && !(flags & IFF_RUNNING))
		printf("NO-CARRIER,");
	flags &= ~IFF_RUNNING;
#if 0
	_PF(ALLMULTI);
	_PF(PROMISC);
	_PF(MASTER);
	_PF(SLAVE);
	_PF(DEBUG);
	_PF(DYNAMIC);
	_PF(AUTOMEDIA);
	_PF(PORTSEL);
	_PF(NOTRAILERS);
#endif
	flags = print_flags_separated(flag_masks, flag_labels, flags, ",");
	if (flags)
		printf("%x", flags);
	if (mdown)
		printf(",M-DOWN");
	printf("> ");
}

static void print_queuelen(char *name)
{
	struct ifreq ifr;
	int s;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return;

	memset(&ifr, 0, sizeof(ifr));
	strncpy_IFNAMSIZ(ifr.ifr_name, name);
	if (ioctl_or_warn(s, SIOCGIFTXQLEN, &ifr) < 0) {
		close(s);
		return;
	}
	close(s);

	if (ifr.ifr_qlen)
		printf("qlen %d", ifr.ifr_qlen);
}

static NOINLINE int print_linkinfo(const struct nlmsghdr *n)
{
	struct ifinfomsg *ifi = NLMSG_DATA(n);
	struct rtattr *tb[IFLA_MAX+1];
	int len = n->nlmsg_len;

	if (n->nlmsg_type != RTM_NEWLINK && n->nlmsg_type != RTM_DELLINK)
		return 0;

	len -= NLMSG_LENGTH(sizeof(*ifi));
	if (len < 0)
		return -1;

	if (G_filter.ifindex && ifi->ifi_index != G_filter.ifindex)
		return 0;
	if (G_filter.up && !(ifi->ifi_flags & IFF_UP))
		return 0;

	//memset(tb, 0, sizeof(tb)); - parse_rtattr does this
	parse_rtattr(tb, IFLA_MAX, IFLA_RTA(ifi), len);
	if (tb[IFLA_IFNAME] == NULL) {
		bb_error_msg("nil ifname");
		return -1;
	}
	if (G_filter.label
	 && (!G_filter.family || G_filter.family == AF_PACKET)
	 && fnmatch(G_filter.label, RTA_DATA(tb[IFLA_IFNAME]), 0)
	) {
		return 0;
	}

	if (n->nlmsg_type == RTM_DELLINK)
		printf("Deleted ");

	printf("%d: %s", ifi->ifi_index,
		/*tb[IFLA_IFNAME] ? (char*)RTA_DATA(tb[IFLA_IFNAME]) : "<nil>" - we checked tb[IFLA_IFNAME] above*/
		(char*)RTA_DATA(tb[IFLA_IFNAME])
	);

	{
		unsigned m_flag = 0;
		if (tb[IFLA_LINK]) {
			int iflink = *(int*)RTA_DATA(tb[IFLA_LINK]);
			if (iflink == 0)
				printf("@NONE: ");
			else {
				printf("@%s: ", ll_index_to_name(iflink));
				m_flag = ll_index_to_flags(iflink);
				m_flag = !(m_flag & IFF_UP);
			}
		} else {
			printf(": ");
		}
		print_link_flags(ifi->ifi_flags, m_flag);
	}

	if (tb[IFLA_MTU])
		printf("mtu %u ", *(int*)RTA_DATA(tb[IFLA_MTU]));
	if (tb[IFLA_QDISC])
		printf("qdisc %s ", (char*)RTA_DATA(tb[IFLA_QDISC]));
#ifdef IFLA_MASTER
	if (tb[IFLA_MASTER]) {
		printf("master %s ", ll_index_to_name(*(int*)RTA_DATA(tb[IFLA_MASTER])));
	}
#endif
/* IFLA_OPERSTATE was added to kernel with the same commit as IFF_DORMANT */
#ifdef IFF_DORMANT
	if (tb[IFLA_OPERSTATE]) {
		static const char operstate_labels[] ALIGN1 =
			"UNKNOWN\0""NOTPRESENT\0""DOWN\0""LOWERLAYERDOWN\0"
			"TESTING\0""DORMANT\0""UP\0";
		printf("state %s ", nth_string(operstate_labels,
					*(uint8_t *)RTA_DATA(tb[IFLA_OPERSTATE])));
	}
#endif
	if (G_filter.showqueue)
		print_queuelen((char*)RTA_DATA(tb[IFLA_IFNAME]));

	if (!G_filter.family || G_filter.family == AF_PACKET) {
		SPRINT_BUF(b1);
		printf("%c    link/%s ", _SL_, ll_type_n2a(ifi->ifi_type, b1));

		if (tb[IFLA_ADDRESS]) {
			fputs(ll_addr_n2a(RTA_DATA(tb[IFLA_ADDRESS]),
						      RTA_PAYLOAD(tb[IFLA_ADDRESS]),
						      ifi->ifi_type,
						      b1, sizeof(b1)), stdout);
		}
		if (tb[IFLA_BROADCAST]) {
			if (ifi->ifi_flags & IFF_POINTOPOINT)
				printf(" peer ");
			else
				printf(" brd ");
			fputs(ll_addr_n2a(RTA_DATA(tb[IFLA_BROADCAST]),
						      RTA_PAYLOAD(tb[IFLA_BROADCAST]),
						      ifi->ifi_type,
						      b1, sizeof(b1)), stdout);
		}
	}
	bb_putchar('\n');
	/*fflush_all();*/
	return 0;
}

static int flush_update(void)
{
	if (rtnl_send(G_filter.rth, G_filter.flushb, G_filter.flushp) < 0) {
		bb_perror_msg("can't send flush request");
		return -1;
	}
	G_filter.flushp = 0;
	return 0;
}

static int FAST_FUNC print_addrinfo(const struct sockaddr_nl *who UNUSED_PARAM,
		struct nlmsghdr *n, void *arg UNUSED_PARAM)
{
	struct ifaddrmsg *ifa = NLMSG_DATA(n);
	int len = n->nlmsg_len;
	struct rtattr *rta_tb[IFA_MAX+1];

	if (n->nlmsg_type != RTM_NEWADDR && n->nlmsg_type != RTM_DELADDR)
		return 0;
	len -= NLMSG_LENGTH(sizeof(*ifa));
	if (len < 0) {
		bb_error_msg("wrong nlmsg len %d", len);
		return -1;
	}

	if (G_filter.flushb && n->nlmsg_type != RTM_NEWADDR)
		return 0;

	//memset(rta_tb, 0, sizeof(rta_tb)); - parse_rtattr does this
	parse_rtattr(rta_tb, IFA_MAX, IFA_RTA(ifa), n->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa)));

	if (!rta_tb[IFA_LOCAL])
		rta_tb[IFA_LOCAL] = rta_tb[IFA_ADDRESS];
	if (!rta_tb[IFA_ADDRESS])
		rta_tb[IFA_ADDRESS] = rta_tb[IFA_LOCAL];

	if (G_filter.ifindex && G_filter.ifindex != ifa->ifa_index)
		return 0;
	if ((G_filter.scope ^ ifa->ifa_scope) & G_filter.scopemask)
		return 0;
	if ((G_filter.flags ^ ifa->ifa_flags) & G_filter.flagmask)
		return 0;
	if (G_filter.label) {
		const char *label;
		if (rta_tb[IFA_LABEL])
			label = RTA_DATA(rta_tb[IFA_LABEL]);
		else
			label = ll_index_to_name(ifa->ifa_index);
		if (fnmatch(G_filter.label, label, 0) != 0)
			return 0;
	}
	if (G_filter.pfx.family) {
		if (rta_tb[IFA_LOCAL]) {
			inet_prefix dst;
			memset(&dst, 0, sizeof(dst));
			dst.family = ifa->ifa_family;
			memcpy(&dst.data, RTA_DATA(rta_tb[IFA_LOCAL]), RTA_PAYLOAD(rta_tb[IFA_LOCAL]));
			if (inet_addr_match(&dst, &G_filter.pfx, G_filter.pfx.bitlen))
				return 0;
		}
	}

	if (G_filter.flushb) {
		struct nlmsghdr *fn;
		if (NLMSG_ALIGN(G_filter.flushp) + n->nlmsg_len > G_filter.flushe) {
			if (flush_update())
				return -1;
		}
		fn = (struct nlmsghdr*)(G_filter.flushb + NLMSG_ALIGN(G_filter.flushp));
		memcpy(fn, n, n->nlmsg_len);
		fn->nlmsg_type = RTM_DELADDR;
		fn->nlmsg_flags = NLM_F_REQUEST;
		fn->nlmsg_seq = ++G_filter.rth->seq;
		G_filter.flushp = (((char*)fn) + n->nlmsg_len) - G_filter.flushb;
		G_filter.flushed = 1;
		return 0;
	}

	if (n->nlmsg_type == RTM_DELADDR)
		printf("Deleted ");

	if (G_filter.oneline)
		printf("%u: %s", ifa->ifa_index, ll_index_to_name(ifa->ifa_index));
	if (ifa->ifa_family == AF_INET)
		printf("    inet ");
	else if (ifa->ifa_family == AF_INET6)
		printf("    inet6 ");
	else
		printf("    family %d ", ifa->ifa_family);

	if (rta_tb[IFA_LOCAL]) {
		fputs(rt_addr_n2a(ifa->ifa_family, RTA_DATA(rta_tb[IFA_LOCAL])),
			stdout
		);

		if (rta_tb[IFA_ADDRESS] == NULL
		 || memcmp(RTA_DATA(rta_tb[IFA_ADDRESS]), RTA_DATA(rta_tb[IFA_LOCAL]), 4) == 0
		) {
			printf("/%d ", ifa->ifa_prefixlen);
		} else {
			printf(" peer %s/%d ",
				rt_addr_n2a(ifa->ifa_family, RTA_DATA(rta_tb[IFA_ADDRESS])),
				ifa->ifa_prefixlen
			);
		}
	}

	if (rta_tb[IFA_BROADCAST]) {
		printf("brd %s ",
			rt_addr_n2a(ifa->ifa_family,
				RTA_DATA(rta_tb[IFA_BROADCAST]))
		);
	}
	if (rta_tb[IFA_ANYCAST]) {
		printf("any %s ",
			rt_addr_n2a(ifa->ifa_family,
				RTA_DATA(rta_tb[IFA_ANYCAST]))
		);
	}
	printf("scope %s ", rtnl_rtscope_n2a(ifa->ifa_scope));
	if (ifa->ifa_flags & IFA_F_SECONDARY) {
		ifa->ifa_flags &= ~IFA_F_SECONDARY;
		printf("secondary ");
	}
	if (ifa->ifa_flags & IFA_F_TENTATIVE) {
		ifa->ifa_flags &= ~IFA_F_TENTATIVE;
		printf("tentative ");
	}
	if (ifa->ifa_flags & IFA_F_DEPRECATED) {
		ifa->ifa_flags &= ~IFA_F_DEPRECATED;
		printf("deprecated ");
	}
	if (!(ifa->ifa_flags & IFA_F_PERMANENT)) {
		printf("dynamic ");
	} else
		ifa->ifa_flags &= ~IFA_F_PERMANENT;
	if (ifa->ifa_flags)
		printf("flags %02x ", ifa->ifa_flags);
	if (rta_tb[IFA_LABEL])
		fputs((char*)RTA_DATA(rta_tb[IFA_LABEL]), stdout);
	if (rta_tb[IFA_CACHEINFO]) {
		struct ifa_cacheinfo *ci = RTA_DATA(rta_tb[IFA_CACHEINFO]);
		char buf[128];
		bb_putchar(_SL_);
		if (ci->ifa_valid == 0xFFFFFFFFU)
			sprintf(buf, "valid_lft forever");
		else
			sprintf(buf, "valid_lft %dsec", ci->ifa_valid);
		if (ci->ifa_prefered == 0xFFFFFFFFU)
			sprintf(buf+strlen(buf), " preferred_lft forever");
		else
			sprintf(buf+strlen(buf), " preferred_lft %dsec", ci->ifa_prefered);
		printf("       %s", buf);
	}
	bb_putchar('\n');
	/*fflush_all();*/
	return 0;
}


struct nlmsg_list {
	struct nlmsg_list *next;
	struct nlmsghdr   h;
};

static int print_selected_addrinfo(int ifindex, struct nlmsg_list *ainfo)
{
	for (; ainfo; ainfo = ainfo->next) {
		struct nlmsghdr *n = &ainfo->h;
		struct ifaddrmsg *ifa = NLMSG_DATA(n);

		if (n->nlmsg_type != RTM_NEWADDR)
			continue;
		if (n->nlmsg_len < NLMSG_LENGTH(sizeof(ifa)))
			return -1;
		if (ifa->ifa_index != ifindex
		 || (G_filter.family && G_filter.family != ifa->ifa_family)
		) {
			continue;
		}
		print_addrinfo(NULL, n, NULL);
	}
	return 0;
}


static int FAST_FUNC store_nlmsg(const struct sockaddr_nl *who, struct nlmsghdr *n, void *arg)
{
	struct nlmsg_list **linfo = (struct nlmsg_list**)arg;
	struct nlmsg_list *h;
	struct nlmsg_list **lp;

	h = xzalloc(n->nlmsg_len + sizeof(void*));

	memcpy(&h->h, n, n->nlmsg_len);
	/*h->next = NULL; - xzalloc did it */

	for (lp = linfo; *lp; lp = &(*lp)->next)
		continue;
	*lp = h;

	ll_remember_index(who, n, NULL);
	return 0;
}

static void ipaddr_reset_filter(int _oneline)
{
	memset(&G_filter, 0, sizeof(G_filter));
	G_filter.oneline = _oneline;
}

/* Return value becomes exitcode. It's okay to not return at all */
int FAST_FUNC ipaddr_list_or_flush(char **argv, int flush)
{
	static const char option[] ALIGN1 = "to\0""scope\0""up\0""label\0""dev\0";

	struct nlmsg_list *linfo = NULL;
	struct nlmsg_list *ainfo = NULL;
	struct nlmsg_list *l;
	struct rtnl_handle rth;
	char *filter_dev = NULL;
	int no_link = 0;

	ipaddr_reset_filter(oneline);
	G_filter.showqueue = 1;

	if (G_filter.family == AF_UNSPEC)
		G_filter.family = preferred_family;

	if (flush) {
		if (!*argv) {
			bb_error_msg_and_die(bb_msg_requires_arg, "flush");
		}
		if (G_filter.family == AF_PACKET) {
			bb_error_msg_and_die("can't flush link addresses");
		}
	}

	while (*argv) {
		const smalluint key = index_in_strings(option, *argv);
		if (key == 0) { /* to */
			NEXT_ARG();
			get_prefix(&G_filter.pfx, *argv, G_filter.family);
			if (G_filter.family == AF_UNSPEC) {
				G_filter.family = G_filter.pfx.family;
			}
		} else if (key == 1) { /* scope */
			uint32_t scope = 0;
			NEXT_ARG();
			G_filter.scopemask = -1;
			if (rtnl_rtscope_a2n(&scope, *argv)) {
				if (strcmp(*argv, "all") != 0) {
					invarg_1_to_2(*argv, "scope");
				}
				scope = RT_SCOPE_NOWHERE;
				G_filter.scopemask = 0;
			}
			G_filter.scope = scope;
		} else if (key == 2) { /* up */
			G_filter.up = 1;
		} else if (key == 3) { /* label */
			NEXT_ARG();
			G_filter.label = *argv;
		} else {
			if (key == 4) /* dev */
				NEXT_ARG();
			if (filter_dev)
				duparg2("dev", *argv);
			filter_dev = *argv;
		}
		argv++;
	}

	xrtnl_open(&rth);

	xrtnl_wilddump_request(&rth, preferred_family, RTM_GETLINK);
	xrtnl_dump_filter(&rth, store_nlmsg, &linfo);

	if (filter_dev) {
		G_filter.ifindex = xll_name_to_index(filter_dev);
	}

	if (flush) {
		char flushb[4096-512];

		G_filter.flushb = flushb;
		G_filter.flushp = 0;
		G_filter.flushe = sizeof(flushb);
		G_filter.rth = &rth;

		for (;;) {
			xrtnl_wilddump_request(&rth, G_filter.family, RTM_GETADDR);
			G_filter.flushed = 0;
			xrtnl_dump_filter(&rth, print_addrinfo, NULL);
			if (G_filter.flushed == 0) {
				return 0;
			}
			if (flush_update() < 0) {
				return 1;
			}
		}
	}

	if (G_filter.family != AF_PACKET) {
		xrtnl_wilddump_request(&rth, G_filter.family, RTM_GETADDR);
		xrtnl_dump_filter(&rth, store_nlmsg, &ainfo);
	}


	if (G_filter.family && G_filter.family != AF_PACKET) {
		struct nlmsg_list **lp;
		lp = &linfo;

		if (G_filter.oneline)
			no_link = 1;

		while ((l = *lp) != NULL) {
			int ok = 0;
			struct ifinfomsg *ifi = NLMSG_DATA(&l->h);
			struct nlmsg_list *a;

			for (a = ainfo; a; a = a->next) {
				struct nlmsghdr *n = &a->h;
				struct ifaddrmsg *ifa = NLMSG_DATA(n);

				if (ifa->ifa_index != ifi->ifi_index
				 || (G_filter.family && G_filter.family != ifa->ifa_family)
				) {
					continue;
				}
				if ((G_filter.scope ^ ifa->ifa_scope) & G_filter.scopemask)
					continue;
				if ((G_filter.flags ^ ifa->ifa_flags) & G_filter.flagmask)
					continue;
				if (G_filter.pfx.family || G_filter.label) {
					struct rtattr *tb[IFA_MAX+1];
					//memset(tb, 0, sizeof(tb)); - parse_rtattr does this
					parse_rtattr(tb, IFA_MAX, IFA_RTA(ifa), IFA_PAYLOAD(n));
					if (!tb[IFA_LOCAL])
						tb[IFA_LOCAL] = tb[IFA_ADDRESS];

					if (G_filter.pfx.family && tb[IFA_LOCAL]) {
						inet_prefix dst;
						memset(&dst, 0, sizeof(dst));
						dst.family = ifa->ifa_family;
						memcpy(&dst.data, RTA_DATA(tb[IFA_LOCAL]), RTA_PAYLOAD(tb[IFA_LOCAL]));
						if (inet_addr_match(&dst, &G_filter.pfx, G_filter.pfx.bitlen))
							continue;
					}
					if (G_filter.label) {
						const char *label;
						if (tb[IFA_LABEL])
							label = RTA_DATA(tb[IFA_LABEL]);
						else
							label = ll_index_to_name(ifa->ifa_index);
						if (fnmatch(G_filter.label, label, 0) != 0)
							continue;
					}
				}

				ok = 1;
				break;
			}
			if (!ok)
				*lp = l->next;
			else
				lp = &l->next;
		}
	}

	for (l = linfo; l; l = l->next) {
		if (no_link
		 || (oneline || print_linkinfo(&l->h) == 0)
		/* ^^^^^^^^^ "ip -oneline a" does not print link info */
		) {
			struct ifinfomsg *ifi = NLMSG_DATA(&l->h);
			if (G_filter.family != AF_PACKET)
				print_selected_addrinfo(ifi->ifi_index, ainfo);
		}
	}

	return 0;
}

static int default_scope(inet_prefix *lcl)
{
	if (lcl->family == AF_INET) {
		if (lcl->bytelen >= 1 && *(uint8_t*)&lcl->data == 127)
			return RT_SCOPE_HOST;
	}
	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
static int ipaddr_modify(int cmd, int flags, char **argv)
{
	/* If you add stuff here, update ipaddr_full_usage */
	static const char option[] ALIGN1 =
		"peer\0""remote\0""broadcast\0""brd\0"
		"anycast\0""scope\0""dev\0""label\0""local\0";
#define option_peer      option
#define option_broadcast (option           + sizeof("peer") + sizeof("remote"))
#define option_anycast   (option_broadcast + sizeof("broadcast") + sizeof("brd"))
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr  n;
		struct ifaddrmsg ifa;
		char             buf[256];
	} req;
	char *d = NULL;
	char *l = NULL;
	inet_prefix lcl;
	inet_prefix peer;
	int local_len = 0;
	int peer_len = 0;
	int brd_len = 0;
	int any_len = 0;
	bool scoped = 0;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | flags;
	req.n.nlmsg_type = cmd;
	req.ifa.ifa_family = preferred_family;

	while (*argv) {
		unsigned arg = index_in_strings(option, *argv);
		/* if search fails, "local" is assumed */
		if ((int)arg >= 0)
			NEXT_ARG();

		if (arg <= 1) { /* peer, remote */
			if (peer_len) {
				duparg(option_peer, *argv);
			}
			get_prefix(&peer, *argv, req.ifa.ifa_family);
			peer_len = peer.bytelen;
			if (req.ifa.ifa_family == AF_UNSPEC) {
				req.ifa.ifa_family = peer.family;
			}
			addattr_l(&req.n, sizeof(req), IFA_ADDRESS, &peer.data, peer.bytelen);
			req.ifa.ifa_prefixlen = peer.bitlen;
		} else if (arg <= 3) { /* broadcast, brd */
			inet_prefix addr;
			if (brd_len) {
				duparg(option_broadcast, *argv);
			}
			if (LONE_CHAR(*argv, '+')) {
				brd_len = -1;
			} else if (LONE_DASH(*argv)) {
				brd_len = -2;
			} else {
				get_addr(&addr, *argv, req.ifa.ifa_family);
				if (req.ifa.ifa_family == AF_UNSPEC)
					req.ifa.ifa_family = addr.family;
				addattr_l(&req.n, sizeof(req), IFA_BROADCAST, &addr.data, addr.bytelen);
				brd_len = addr.bytelen;
			}
		} else if (arg == 4) { /* anycast */
			inet_prefix addr;
			if (any_len) {
				duparg(option_anycast, *argv);
			}
			get_addr(&addr, *argv, req.ifa.ifa_family);
			if (req.ifa.ifa_family == AF_UNSPEC) {
				req.ifa.ifa_family = addr.family;
			}
			addattr_l(&req.n, sizeof(req), IFA_ANYCAST, &addr.data, addr.bytelen);
			any_len = addr.bytelen;
		} else if (arg == 5) { /* scope */
			uint32_t scope = 0;
			if (rtnl_rtscope_a2n(&scope, *argv)) {
				invarg_1_to_2(*argv, "scope");
			}
			req.ifa.ifa_scope = scope;
			scoped = 1;
		} else if (arg == 6) { /* dev */
			d = *argv;
		} else if (arg == 7) { /* label */
			l = *argv;
			addattr_l(&req.n, sizeof(req), IFA_LABEL, l, strlen(l) + 1);
		} else {
			/* local (specified or assumed) */
			if (local_len) {
				duparg2("local", *argv);
			}
			get_prefix(&lcl, *argv, req.ifa.ifa_family);
			if (req.ifa.ifa_family == AF_UNSPEC) {
				req.ifa.ifa_family = lcl.family;
			}
			addattr_l(&req.n, sizeof(req), IFA_LOCAL, &lcl.data, lcl.bytelen);
			local_len = lcl.bytelen;
		}
		argv++;
	}

	if (!d) {
		/* There was no "dev IFACE", but we need that */
		bb_error_msg_and_die("need \"dev IFACE\"");
	}
	if (l && !is_prefixed_with(l, d)) {
		bb_error_msg_and_die("\"dev\" (%s) must match \"label\" (%s)", d, l);
	}

	if (peer_len == 0 && local_len && cmd != RTM_DELADDR) {
		peer = lcl;
		addattr_l(&req.n, sizeof(req), IFA_ADDRESS, &lcl.data, lcl.bytelen);
	}
	if (req.ifa.ifa_prefixlen == 0)
		req.ifa.ifa_prefixlen = lcl.bitlen;

	if (brd_len < 0 && cmd != RTM_DELADDR) {
		inet_prefix brd;
		int i;
		if (req.ifa.ifa_family != AF_INET) {
			bb_error_msg_and_die("broadcast can be set only for IPv4 addresses");
		}
		brd = peer;
		if (brd.bitlen <= 30) {
			for (i = 31; i >= brd.bitlen; i--) {
				if (brd_len == -1)
					brd.data[0] |= htonl(1<<(31-i));
				else
					brd.data[0] &= ~htonl(1<<(31-i));
			}
			addattr_l(&req.n, sizeof(req), IFA_BROADCAST, &brd.data, brd.bytelen);
			brd_len = brd.bytelen;
		}
	}
	if (!scoped && cmd != RTM_DELADDR)
		req.ifa.ifa_scope = default_scope(&lcl);

	xrtnl_open(&rth);

	ll_init_map(&rth);

	req.ifa.ifa_index = xll_name_to_index(d);

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL, NULL, NULL) < 0)
		return 2;

	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
int FAST_FUNC do_ipaddr(char **argv)
{
	static const char commands[] ALIGN1 =
		/* 0    1         2      3          4         5       6       7      8 */
		"add\0""change\0""chg\0""replace\0""delete\0""list\0""show\0""lst\0""flush\0";
	int cmd = 2;

	INIT_G();

	if (*argv) {
		cmd = index_in_substrings(commands, *argv);
		if (cmd < 0)
			invarg_1_to_2(*argv, applet_name);
		argv++;
		if (cmd <= 4) {
			return ipaddr_modify(
				/*cmd:*/ cmd == 4 ? RTM_DELADDR : RTM_NEWADDR,
				/*flags:*/
					cmd == 0 ? NLM_F_CREATE|NLM_F_EXCL : /* add */
					cmd == 1 || cmd == 2 ? NLM_F_REPLACE : /* change */
					cmd == 3 ? NLM_F_CREATE|NLM_F_REPLACE : /* replace */
					0 /* delete */
			, argv);
		}
	}
	return ipaddr_list_or_flush(argv, cmd == 8);
}
