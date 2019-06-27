// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_csum.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_net_repr.h"

/* The kernel versions of TUNNEL_* are not ABI and therefore vulnerable
 * to change. Such changes will break our FW ABI.
 */
#define NFP_FL_TUNNEL_CSUM			cpu_to_be16(0x01)
#define NFP_FL_TUNNEL_KEY			cpu_to_be16(0x04)
#define NFP_FL_TUNNEL_GENEVE_OPT		cpu_to_be16(0x0800)
#define NFP_FL_SUPPORTED_TUNNEL_INFO_FLAGS	IP_TUNNEL_INFO_TX
#define NFP_FL_SUPPORTED_IPV4_UDP_TUN_FLAGS	(NFP_FL_TUNNEL_CSUM | \
						 NFP_FL_TUNNEL_KEY | \
						 NFP_FL_TUNNEL_GENEVE_OPT)

static void nfp_fl_pop_vlan(struct nfp_fl_pop_vlan *pop_vlan)
{
	size_t act_size = sizeof(struct nfp_fl_pop_vlan);

	pop_vlan->head.jump_id = NFP_FL_ACTION_OPCODE_POP_VLAN;
	pop_vlan->head.len_lw = act_size >> NFP_FL_LW_SIZ;
	pop_vlan->reserved = 0;
}

static void
nfp_fl_push_vlan(struct nfp_fl_push_vlan *push_vlan,
		 const struct flow_action_entry *act)
{
	size_t act_size = sizeof(struct nfp_fl_push_vlan);
	u16 tmp_push_vlan_tci;

	push_vlan->head.jump_id = NFP_FL_ACTION_OPCODE_PUSH_VLAN;
	push_vlan->head.len_lw = act_size >> NFP_FL_LW_SIZ;
	push_vlan->reserved = 0;
	push_vlan->vlan_tpid = act->vlan.proto;

	tmp_push_vlan_tci =
		FIELD_PREP(NFP_FL_PUSH_VLAN_PRIO, act->vlan.prio) |
		FIELD_PREP(NFP_FL_PUSH_VLAN_VID, act->vlan.vid);
	push_vlan->vlan_tci = cpu_to_be16(tmp_push_vlan_tci);
}

static int
nfp_fl_pre_lag(struct nfp_app *app, const struct flow_action_entry *act,
	       struct nfp_fl_payload *nfp_flow, int act_len,
	       struct netlink_ext_ack *extack)
{
	size_t act_size = sizeof(struct nfp_fl_pre_lag);
	struct nfp_fl_pre_lag *pre_lag;
	struct net_device *out_dev;
	int err;

	out_dev = act->dev;
	if (!out_dev || !netif_is_lag_master(out_dev))
		return 0;

	if (act_len + act_size > NFP_FL_MAX_A_SIZ) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed action list size exceeded at LAG action");
		return -EOPNOTSUPP;
	}

	/* Pre_lag action must be first on action list.
	 * If other actions already exist they need pushed forward.
	 */
	if (act_len)
		memmove(nfp_flow->action_data + act_size,
			nfp_flow->action_data, act_len);

	pre_lag = (struct nfp_fl_pre_lag *)nfp_flow->action_data;
	err = nfp_flower_lag_populate_pre_action(app, out_dev, pre_lag, extack);
	if (err)
		return err;

	pre_lag->head.jump_id = NFP_FL_ACTION_OPCODE_PRE_LAG;
	pre_lag->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	nfp_flow->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);

	return act_size;
}

static int
nfp_fl_output(struct nfp_app *app, struct nfp_fl_output *output,
	      const struct flow_action_entry *act,
	      struct nfp_fl_payload *nfp_flow,
	      bool last, struct net_device *in_dev,
	      enum nfp_flower_tun_type tun_type, int *tun_out_cnt,
	      struct netlink_ext_ack *extack)
{
	size_t act_size = sizeof(struct nfp_fl_output);
	struct nfp_flower_priv *priv = app->priv;
	struct net_device *out_dev;
	u16 tmp_flags;

	output->head.jump_id = NFP_FL_ACTION_OPCODE_OUTPUT;
	output->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	out_dev = act->dev;
	if (!out_dev) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid egress interface for mirred action");
		return -EOPNOTSUPP;
	}

	tmp_flags = last ? NFP_FL_OUT_FLAGS_LAST : 0;

	if (tun_type) {
		/* Verify the egress netdev matches the tunnel type. */
		if (!nfp_fl_netdev_is_tunnel_type(out_dev, tun_type)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: egress interface does not match the required tunnel type");
			return -EOPNOTSUPP;
		}

		if (*tun_out_cnt) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: cannot offload more than one tunnel mirred output per filter");
			return -EOPNOTSUPP;
		}
		(*tun_out_cnt)++;

		output->flags = cpu_to_be16(tmp_flags |
					    NFP_FL_OUT_FLAGS_USE_TUN);
		output->port = cpu_to_be32(NFP_FL_PORT_TYPE_TUN | tun_type);
	} else if (netif_is_lag_master(out_dev) &&
		   priv->flower_ext_feats & NFP_FL_FEATS_LAG) {
		int gid;

		output->flags = cpu_to_be16(tmp_flags);
		gid = nfp_flower_lag_get_output_id(app, out_dev);
		if (gid < 0) {
			NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot find group id for LAG action");
			return gid;
		}
		output->port = cpu_to_be32(NFP_FL_LAG_OUT | gid);
	} else {
		/* Set action output parameters. */
		output->flags = cpu_to_be16(tmp_flags);

		if (nfp_netdev_is_nfp_repr(in_dev)) {
			/* Confirm ingress and egress are on same device. */
			if (!netdev_port_same_parent_id(in_dev, out_dev)) {
				NL_SET_ERR_MSG_MOD(extack, "unsupported offload: ingress and egress interfaces are on different devices");
				return -EOPNOTSUPP;
			}
		}

		if (!nfp_netdev_is_nfp_repr(out_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: egress interface is not an nfp port");
			return -EOPNOTSUPP;
		}

		output->port = cpu_to_be32(nfp_repr_get_port_id(out_dev));
		if (!output->port) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid port id for egress interface");
			return -EOPNOTSUPP;
		}
	}
	nfp_flow->meta.shortcut = output->port;

	return 0;
}

static enum nfp_flower_tun_type
nfp_fl_get_tun_from_act(struct nfp_app *app,
			const struct flow_action_entry *act)
{
	const struct ip_tunnel_info *tun = act->tunnel;
	struct nfp_flower_priv *priv = app->priv;

	switch (tun->key.tp_dst) {
	case htons(IANA_VXLAN_UDP_PORT):
		return NFP_FL_TUNNEL_VXLAN;
	case htons(GENEVE_UDP_PORT):
		if (priv->flower_ext_feats & NFP_FL_FEATS_GENEVE)
			return NFP_FL_TUNNEL_GENEVE;
		/* FALLTHROUGH */
	default:
		return NFP_FL_TUNNEL_NONE;
	}
}

static struct nfp_fl_pre_tunnel *nfp_fl_pre_tunnel(char *act_data, int act_len)
{
	size_t act_size = sizeof(struct nfp_fl_pre_tunnel);
	struct nfp_fl_pre_tunnel *pre_tun_act;

	/* Pre_tunnel action must be first on action list.
	 * If other actions already exist they need to be pushed forward.
	 */
	if (act_len)
		memmove(act_data + act_size, act_data, act_len);

	pre_tun_act = (struct nfp_fl_pre_tunnel *)act_data;

	memset(pre_tun_act, 0, act_size);

	pre_tun_act->head.jump_id = NFP_FL_ACTION_OPCODE_PRE_TUNNEL;
	pre_tun_act->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	return pre_tun_act;
}

static int
nfp_fl_push_geneve_options(struct nfp_fl_payload *nfp_fl, int *list_len,
			   const struct flow_action_entry *act,
			   struct netlink_ext_ack *extack)
{
	struct ip_tunnel_info *ip_tun = (struct ip_tunnel_info *)act->tunnel;
	int opt_len, opt_cnt, act_start, tot_push_len;
	u8 *src = ip_tunnel_info_opts(ip_tun);

	/* We need to populate the options in reverse order for HW.
	 * Therefore we go through the options, calculating the
	 * number of options and the total size, then we populate
	 * them in reverse order in the action list.
	 */
	opt_cnt = 0;
	tot_push_len = 0;
	opt_len = ip_tun->options_len;
	while (opt_len > 0) {
		struct geneve_opt *opt = (struct geneve_opt *)src;

		opt_cnt++;
		if (opt_cnt > NFP_FL_MAX_GENEVE_OPT_CNT) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed number of geneve options exceeded");
			return -EOPNOTSUPP;
		}

		tot_push_len += sizeof(struct nfp_fl_push_geneve) +
			       opt->length * 4;
		if (tot_push_len > NFP_FL_MAX_GENEVE_OPT_ACT) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed action list size exceeded at push geneve options");
			return -EOPNOTSUPP;
		}

		opt_len -= sizeof(struct geneve_opt) + opt->length * 4;
		src += sizeof(struct geneve_opt) + opt->length * 4;
	}

	if (*list_len + tot_push_len > NFP_FL_MAX_A_SIZ) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed action list size exceeded at push geneve options");
		return -EOPNOTSUPP;
	}

	act_start = *list_len;
	*list_len += tot_push_len;
	src = ip_tunnel_info_opts(ip_tun);
	while (opt_cnt) {
		struct geneve_opt *opt = (struct geneve_opt *)src;
		struct nfp_fl_push_geneve *push;
		size_t act_size, len;

		opt_cnt--;
		act_size = sizeof(struct nfp_fl_push_geneve) + opt->length * 4;
		tot_push_len -= act_size;
		len = act_start + tot_push_len;

		push = (struct nfp_fl_push_geneve *)&nfp_fl->action_data[len];
		push->head.jump_id = NFP_FL_ACTION_OPCODE_PUSH_GENEVE;
		push->head.len_lw = act_size >> NFP_FL_LW_SIZ;
		push->reserved = 0;
		push->class = opt->opt_class;
		push->type = opt->type;
		push->length = opt->length;
		memcpy(&push->opt_data, opt->opt_data, opt->length * 4);

		src += sizeof(struct geneve_opt) + opt->length * 4;
	}

	return 0;
}

static int
nfp_fl_set_ipv4_tun(struct nfp_app *app, struct nfp_fl_set_ipv4_tun *set_tun,
		    const struct flow_action_entry *act,
		    struct nfp_fl_pre_tunnel *pre_tun,
		    enum nfp_flower_tun_type tun_type,
		    struct net_device *netdev, struct netlink_ext_ack *extack)
{
	size_t act_size = sizeof(struct nfp_fl_set_ipv4_tun);
	const struct ip_tunnel_info *ip_tun = act->tunnel;
	struct nfp_flower_priv *priv = app->priv;
	u32 tmp_set_ip_tun_type_index = 0;
	/* Currently support one pre-tunnel so index is always 0. */
	int pretun_idx = 0;

	BUILD_BUG_ON(NFP_FL_TUNNEL_CSUM != TUNNEL_CSUM ||
		     NFP_FL_TUNNEL_KEY	!= TUNNEL_KEY ||
		     NFP_FL_TUNNEL_GENEVE_OPT != TUNNEL_GENEVE_OPT);
	if (ip_tun->options_len &&
	    (tun_type != NFP_FL_TUNNEL_GENEVE ||
	    !(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE_OPT))) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support geneve options offload");
		return -EOPNOTSUPP;
	}

	set_tun->head.jump_id = NFP_FL_ACTION_OPCODE_SET_IPV4_TUNNEL;
	set_tun->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	/* Set tunnel type and pre-tunnel index. */
	tmp_set_ip_tun_type_index |=
		FIELD_PREP(NFP_FL_IPV4_TUNNEL_TYPE, tun_type) |
		FIELD_PREP(NFP_FL_IPV4_PRE_TUN_INDEX, pretun_idx);

	set_tun->tun_type_index = cpu_to_be32(tmp_set_ip_tun_type_index);
	set_tun->tun_id = ip_tun->key.tun_id;

	if (ip_tun->key.ttl) {
		set_tun->ttl = ip_tun->key.ttl;
	} else {
		struct net *net = dev_net(netdev);
		struct flowi4 flow = {};
		struct rtable *rt;
		int err;

		/* Do a route lookup to determine ttl - if fails then use
		 * default. Note that CONFIG_INET is a requirement of
		 * CONFIG_NET_SWITCHDEV so must be defined here.
		 */
		flow.daddr = ip_tun->key.u.ipv4.dst;
		flow.flowi4_proto = IPPROTO_UDP;
		rt = ip_route_output_key(net, &flow);
		err = PTR_ERR_OR_ZERO(rt);
		if (!err) {
			set_tun->ttl = ip4_dst_hoplimit(&rt->dst);
			ip_rt_put(rt);
		} else {
			set_tun->ttl = net->ipv4.sysctl_ip_default_ttl;
		}
	}

	set_tun->tos = ip_tun->key.tos;

	if (!(ip_tun->key.tun_flags & NFP_FL_TUNNEL_KEY) ||
	    ip_tun->key.tun_flags & ~NFP_FL_SUPPORTED_IPV4_UDP_TUN_FLAGS) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support tunnel flag offload");
		return -EOPNOTSUPP;
	}
	set_tun->tun_flags = ip_tun->key.tun_flags;

	if (tun_type == NFP_FL_TUNNEL_GENEVE) {
		set_tun->tun_proto = htons(ETH_P_TEB);
		set_tun->tun_len = ip_tun->options_len / 4;
	}

	/* Complete pre_tunnel action. */
	pre_tun->ipv4_dst = ip_tun->key.u.ipv4.dst;

	return 0;
}

static void nfp_fl_set_helper32(u32 value, u32 mask, u8 *p_exact, u8 *p_mask)
{
	u32 oldvalue = get_unaligned((u32 *)p_exact);
	u32 oldmask = get_unaligned((u32 *)p_mask);

	value &= mask;
	value |= oldvalue & ~mask;

	put_unaligned(oldmask | mask, (u32 *)p_mask);
	put_unaligned(value, (u32 *)p_exact);
}

static int
nfp_fl_set_eth(const struct flow_action_entry *act, u32 off,
	       struct nfp_fl_set_eth *set_eth, struct netlink_ext_ack *extack)
{
	u32 exact, mask;

	if (off + 4 > ETH_ALEN * 2) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit ethernet action");
		return -EOPNOTSUPP;
	}

	mask = ~act->mangle.mask;
	exact = act->mangle.val;

	if (exact & ~mask) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit ethernet action");
		return -EOPNOTSUPP;
	}

	nfp_fl_set_helper32(exact, mask, &set_eth->eth_addr_val[off],
			    &set_eth->eth_addr_mask[off]);

	set_eth->reserved = cpu_to_be16(0);
	set_eth->head.jump_id = NFP_FL_ACTION_OPCODE_SET_ETHERNET;
	set_eth->head.len_lw = sizeof(*set_eth) >> NFP_FL_LW_SIZ;

	return 0;
}

struct ipv4_ttl_word {
	__u8	ttl;
	__u8	protocol;
	__sum16	check;
};

static int
nfp_fl_set_ip4(const struct flow_action_entry *act, u32 off,
	       struct nfp_fl_set_ip4_addrs *set_ip_addr,
	       struct nfp_fl_set_ip4_ttl_tos *set_ip_ttl_tos,
	       struct netlink_ext_ack *extack)
{
	struct ipv4_ttl_word *ttl_word_mask;
	struct ipv4_ttl_word *ttl_word;
	struct iphdr *tos_word_mask;
	struct iphdr *tos_word;
	__be32 exact, mask;

	/* We are expecting tcf_pedit to return a big endian value */
	mask = (__force __be32)~act->mangle.mask;
	exact = (__force __be32)act->mangle.val;

	if (exact & ~mask) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit IPv4 action");
		return -EOPNOTSUPP;
	}

	switch (off) {
	case offsetof(struct iphdr, daddr):
		set_ip_addr->ipv4_dst_mask |= mask;
		set_ip_addr->ipv4_dst &= ~mask;
		set_ip_addr->ipv4_dst |= exact & mask;
		set_ip_addr->head.jump_id = NFP_FL_ACTION_OPCODE_SET_IPV4_ADDRS;
		set_ip_addr->head.len_lw = sizeof(*set_ip_addr) >>
					   NFP_FL_LW_SIZ;
		break;
	case offsetof(struct iphdr, saddr):
		set_ip_addr->ipv4_src_mask |= mask;
		set_ip_addr->ipv4_src &= ~mask;
		set_ip_addr->ipv4_src |= exact & mask;
		set_ip_addr->head.jump_id = NFP_FL_ACTION_OPCODE_SET_IPV4_ADDRS;
		set_ip_addr->head.len_lw = sizeof(*set_ip_addr) >>
					   NFP_FL_LW_SIZ;
		break;
	case offsetof(struct iphdr, ttl):
		ttl_word_mask = (struct ipv4_ttl_word *)&mask;
		ttl_word = (struct ipv4_ttl_word *)&exact;

		if (ttl_word_mask->protocol || ttl_word_mask->check) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit IPv4 ttl action");
			return -EOPNOTSUPP;
		}

		set_ip_ttl_tos->ipv4_ttl_mask |= ttl_word_mask->ttl;
		set_ip_ttl_tos->ipv4_ttl &= ~ttl_word_mask->ttl;
		set_ip_ttl_tos->ipv4_ttl |= ttl_word->ttl & ttl_word_mask->ttl;
		set_ip_ttl_tos->head.jump_id =
			NFP_FL_ACTION_OPCODE_SET_IPV4_TTL_TOS;
		set_ip_ttl_tos->head.len_lw = sizeof(*set_ip_ttl_tos) >>
					      NFP_FL_LW_SIZ;
		break;
	case round_down(offsetof(struct iphdr, tos), 4):
		tos_word_mask = (struct iphdr *)&mask;
		tos_word = (struct iphdr *)&exact;

		if (tos_word_mask->version || tos_word_mask->ihl ||
		    tos_word_mask->tot_len) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit IPv4 tos action");
			return -EOPNOTSUPP;
		}

		set_ip_ttl_tos->ipv4_tos_mask |= tos_word_mask->tos;
		set_ip_ttl_tos->ipv4_tos &= ~tos_word_mask->tos;
		set_ip_ttl_tos->ipv4_tos |= tos_word->tos & tos_word_mask->tos;
		set_ip_ttl_tos->head.jump_id =
			NFP_FL_ACTION_OPCODE_SET_IPV4_TTL_TOS;
		set_ip_ttl_tos->head.len_lw = sizeof(*set_ip_ttl_tos) >>
					      NFP_FL_LW_SIZ;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: pedit on unsupported section of IPv4 header");
		return -EOPNOTSUPP;
	}

	return 0;
}

static void
nfp_fl_set_ip6_helper(int opcode_tag, u8 word, __be32 exact, __be32 mask,
		      struct nfp_fl_set_ipv6_addr *ip6)
{
	ip6->ipv6[word].mask |= mask;
	ip6->ipv6[word].exact &= ~mask;
	ip6->ipv6[word].exact |= exact & mask;

	ip6->reserved = cpu_to_be16(0);
	ip6->head.jump_id = opcode_tag;
	ip6->head.len_lw = sizeof(*ip6) >> NFP_FL_LW_SIZ;
}

struct ipv6_hop_limit_word {
	__be16 payload_len;
	u8 nexthdr;
	u8 hop_limit;
};

static int
nfp_fl_set_ip6_hop_limit_flow_label(u32 off, __be32 exact, __be32 mask,
				    struct nfp_fl_set_ipv6_tc_hl_fl *ip_hl_fl,
				    struct netlink_ext_ack *extack)
{
	struct ipv6_hop_limit_word *fl_hl_mask;
	struct ipv6_hop_limit_word *fl_hl;

	switch (off) {
	case offsetof(struct ipv6hdr, payload_len):
		fl_hl_mask = (struct ipv6_hop_limit_word *)&mask;
		fl_hl = (struct ipv6_hop_limit_word *)&exact;

		if (fl_hl_mask->nexthdr || fl_hl_mask->payload_len) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit IPv6 hop limit action");
			return -EOPNOTSUPP;
		}

		ip_hl_fl->ipv6_hop_limit_mask |= fl_hl_mask->hop_limit;
		ip_hl_fl->ipv6_hop_limit &= ~fl_hl_mask->hop_limit;
		ip_hl_fl->ipv6_hop_limit |= fl_hl->hop_limit &
					    fl_hl_mask->hop_limit;
		break;
	case round_down(offsetof(struct ipv6hdr, flow_lbl), 4):
		if (mask & ~IPV6_FLOW_LABEL_MASK ||
		    exact & ~IPV6_FLOW_LABEL_MASK) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit IPv6 flow label action");
			return -EOPNOTSUPP;
		}

		ip_hl_fl->ipv6_label_mask |= mask;
		ip_hl_fl->ipv6_label &= ~mask;
		ip_hl_fl->ipv6_label |= exact & mask;
		break;
	}

	ip_hl_fl->head.jump_id = NFP_FL_ACTION_OPCODE_SET_IPV6_TC_HL_FL;
	ip_hl_fl->head.len_lw = sizeof(*ip_hl_fl) >> NFP_FL_LW_SIZ;

	return 0;
}

static int
nfp_fl_set_ip6(const struct flow_action_entry *act, u32 off,
	       struct nfp_fl_set_ipv6_addr *ip_dst,
	       struct nfp_fl_set_ipv6_addr *ip_src,
	       struct nfp_fl_set_ipv6_tc_hl_fl *ip_hl_fl,
	       struct netlink_ext_ack *extack)
{
	__be32 exact, mask;
	int err = 0;
	u8 word;

	/* We are expecting tcf_pedit to return a big endian value */
	mask = (__force __be32)~act->mangle.mask;
	exact = (__force __be32)act->mangle.val;

	if (exact & ~mask) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit IPv6 action");
		return -EOPNOTSUPP;
	}

	if (off < offsetof(struct ipv6hdr, saddr)) {
		err = nfp_fl_set_ip6_hop_limit_flow_label(off, exact, mask,
							  ip_hl_fl, extack);
	} else if (off < offsetof(struct ipv6hdr, daddr)) {
		word = (off - offsetof(struct ipv6hdr, saddr)) / sizeof(exact);
		nfp_fl_set_ip6_helper(NFP_FL_ACTION_OPCODE_SET_IPV6_SRC, word,
				      exact, mask, ip_src);
	} else if (off < offsetof(struct ipv6hdr, daddr) +
		       sizeof(struct in6_addr)) {
		word = (off - offsetof(struct ipv6hdr, daddr)) / sizeof(exact);
		nfp_fl_set_ip6_helper(NFP_FL_ACTION_OPCODE_SET_IPV6_DST, word,
				      exact, mask, ip_dst);
	} else {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: pedit on unsupported section of IPv6 header");
		return -EOPNOTSUPP;
	}

	return err;
}

static int
nfp_fl_set_tport(const struct flow_action_entry *act, u32 off,
		 struct nfp_fl_set_tport *set_tport, int opcode,
		 struct netlink_ext_ack *extack)
{
	u32 exact, mask;

	if (off) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: pedit on unsupported section of L4 header");
		return -EOPNOTSUPP;
	}

	mask = ~act->mangle.mask;
	exact = act->mangle.val;

	if (exact & ~mask) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid pedit L4 action");
		return -EOPNOTSUPP;
	}

	nfp_fl_set_helper32(exact, mask, set_tport->tp_port_val,
			    set_tport->tp_port_mask);

	set_tport->reserved = cpu_to_be16(0);
	set_tport->head.jump_id = opcode;
	set_tport->head.len_lw = sizeof(*set_tport) >> NFP_FL_LW_SIZ;

	return 0;
}

static u32 nfp_fl_csum_l4_to_flag(u8 ip_proto)
{
	switch (ip_proto) {
	case 0:
		/* Filter doesn't force proto match,
		 * both TCP and UDP will be updated if encountered
		 */
		return TCA_CSUM_UPDATE_FLAG_TCP | TCA_CSUM_UPDATE_FLAG_UDP;
	case IPPROTO_TCP:
		return TCA_CSUM_UPDATE_FLAG_TCP;
	case IPPROTO_UDP:
		return TCA_CSUM_UPDATE_FLAG_UDP;
	default:
		/* All other protocols will be ignored by FW */
		return 0;
	}
}

struct nfp_flower_pedit_acts {
	struct nfp_fl_set_ipv6_addr set_ip6_dst, set_ip6_src;
	struct nfp_fl_set_ipv6_tc_hl_fl set_ip6_tc_hl_fl;
	struct nfp_fl_set_ip4_ttl_tos set_ip_ttl_tos;
	struct nfp_fl_set_ip4_addrs set_ip_addr;
	struct nfp_fl_set_tport set_tport;
	struct nfp_fl_set_eth set_eth;
};

static int
nfp_fl_commit_mangle(struct tc_cls_flower_offload *flow, char *nfp_action,
		     int *a_len, struct nfp_flower_pedit_acts *set_act,
		     u32 *csum_updated)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(flow);
	size_t act_size = 0;
	u8 ip_proto = 0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ip_proto = match.key->ip_proto;
	}

	if (set_act->set_eth.head.len_lw) {
		act_size = sizeof(set_act->set_eth);
		memcpy(nfp_action, &set_act->set_eth, act_size);
		*a_len += act_size;
	}

	if (set_act->set_ip_ttl_tos.head.len_lw) {
		nfp_action += act_size;
		act_size = sizeof(set_act->set_ip_ttl_tos);
		memcpy(nfp_action, &set_act->set_ip_ttl_tos, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix IPv4 and TCP/UDP checksum. */
		*csum_updated |= TCA_CSUM_UPDATE_FLAG_IPV4HDR |
				nfp_fl_csum_l4_to_flag(ip_proto);
	}

	if (set_act->set_ip_addr.head.len_lw) {
		nfp_action += act_size;
		act_size = sizeof(set_act->set_ip_addr);
		memcpy(nfp_action, &set_act->set_ip_addr, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix IPv4 and TCP/UDP checksum. */
		*csum_updated |= TCA_CSUM_UPDATE_FLAG_IPV4HDR |
				nfp_fl_csum_l4_to_flag(ip_proto);
	}

	if (set_act->set_ip6_tc_hl_fl.head.len_lw) {
		nfp_action += act_size;
		act_size = sizeof(set_act->set_ip6_tc_hl_fl);
		memcpy(nfp_action, &set_act->set_ip6_tc_hl_fl, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	}

	if (set_act->set_ip6_dst.head.len_lw &&
	    set_act->set_ip6_src.head.len_lw) {
		/* TC compiles set src and dst IPv6 address as a single action,
		 * the hardware requires this to be 2 separate actions.
		 */
		nfp_action += act_size;
		act_size = sizeof(set_act->set_ip6_src);
		memcpy(nfp_action, &set_act->set_ip6_src, act_size);
		*a_len += act_size;

		act_size = sizeof(set_act->set_ip6_dst);
		memcpy(&nfp_action[sizeof(set_act->set_ip6_src)],
		       &set_act->set_ip6_dst, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	} else if (set_act->set_ip6_dst.head.len_lw) {
		nfp_action += act_size;
		act_size = sizeof(set_act->set_ip6_dst);
		memcpy(nfp_action, &set_act->set_ip6_dst, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	} else if (set_act->set_ip6_src.head.len_lw) {
		nfp_action += act_size;
		act_size = sizeof(set_act->set_ip6_src);
		memcpy(nfp_action, &set_act->set_ip6_src, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	}
	if (set_act->set_tport.head.len_lw) {
		nfp_action += act_size;
		act_size = sizeof(set_act->set_tport);
		memcpy(nfp_action, &set_act->set_tport, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	}

	return 0;
}

static int
nfp_fl_pedit(const struct flow_action_entry *act,
	     struct tc_cls_flower_offload *flow, char *nfp_action, int *a_len,
	     u32 *csum_updated, struct nfp_flower_pedit_acts *set_act,
	     struct netlink_ext_ack *extack)
{
	enum flow_action_mangle_base htype;
	u32 offset;

	htype = act->mangle.htype;
	offset = act->mangle.offset;

	switch (htype) {
	case TCA_PEDIT_KEY_EX_HDR_TYPE_ETH:
		return nfp_fl_set_eth(act, offset, &set_act->set_eth, extack);
	case TCA_PEDIT_KEY_EX_HDR_TYPE_IP4:
		return nfp_fl_set_ip4(act, offset, &set_act->set_ip_addr,
				      &set_act->set_ip_ttl_tos, extack);
	case TCA_PEDIT_KEY_EX_HDR_TYPE_IP6:
		return nfp_fl_set_ip6(act, offset, &set_act->set_ip6_dst,
				      &set_act->set_ip6_src,
				      &set_act->set_ip6_tc_hl_fl, extack);
	case TCA_PEDIT_KEY_EX_HDR_TYPE_TCP:
		return nfp_fl_set_tport(act, offset, &set_act->set_tport,
					NFP_FL_ACTION_OPCODE_SET_TCP, extack);
	case TCA_PEDIT_KEY_EX_HDR_TYPE_UDP:
		return nfp_fl_set_tport(act, offset, &set_act->set_tport,
					NFP_FL_ACTION_OPCODE_SET_UDP, extack);
	default:
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: pedit on unsupported header");
		return -EOPNOTSUPP;
	}
}

static int
nfp_flower_output_action(struct nfp_app *app,
			 const struct flow_action_entry *act,
			 struct nfp_fl_payload *nfp_fl, int *a_len,
			 struct net_device *netdev, bool last,
			 enum nfp_flower_tun_type *tun_type, int *tun_out_cnt,
			 int *out_cnt, u32 *csum_updated,
			 struct netlink_ext_ack *extack)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_output *output;
	int err, prelag_size;

	/* If csum_updated has not been reset by now, it means HW will
	 * incorrectly update csums when they are not requested.
	 */
	if (*csum_updated) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: set actions without updating checksums are not supported");
		return -EOPNOTSUPP;
	}

	if (*a_len + sizeof(struct nfp_fl_output) > NFP_FL_MAX_A_SIZ) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: mirred output increases action list size beyond the allowed maximum");
		return -EOPNOTSUPP;
	}

	output = (struct nfp_fl_output *)&nfp_fl->action_data[*a_len];
	err = nfp_fl_output(app, output, act, nfp_fl, last, netdev, *tun_type,
			    tun_out_cnt, extack);
	if (err)
		return err;

	*a_len += sizeof(struct nfp_fl_output);

	if (priv->flower_ext_feats & NFP_FL_FEATS_LAG) {
		/* nfp_fl_pre_lag returns -err or size of prelag action added.
		 * This will be 0 if it is not egressing to a lag dev.
		 */
		prelag_size = nfp_fl_pre_lag(app, act, nfp_fl, *a_len, extack);
		if (prelag_size < 0) {
			return prelag_size;
		} else if (prelag_size > 0 && (!last || *out_cnt)) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: LAG action has to be last action in action list");
			return -EOPNOTSUPP;
		}

		*a_len += prelag_size;
	}
	(*out_cnt)++;

	return 0;
}

static int
nfp_flower_loop_action(struct nfp_app *app, const struct flow_action_entry *act,
		       struct tc_cls_flower_offload *flow,
		       struct nfp_fl_payload *nfp_fl, int *a_len,
		       struct net_device *netdev,
		       enum nfp_flower_tun_type *tun_type, int *tun_out_cnt,
		       int *out_cnt, u32 *csum_updated,
		       struct nfp_flower_pedit_acts *set_act,
		       struct netlink_ext_ack *extack)
{
	struct nfp_fl_set_ipv4_tun *set_tun;
	struct nfp_fl_pre_tunnel *pre_tun;
	struct nfp_fl_push_vlan *psh_v;
	struct nfp_fl_pop_vlan *pop_v;
	int err;

	switch (act->id) {
	case FLOW_ACTION_DROP:
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_DROP);
		break;
	case FLOW_ACTION_REDIRECT:
		err = nfp_flower_output_action(app, act, nfp_fl, a_len, netdev,
					       true, tun_type, tun_out_cnt,
					       out_cnt, csum_updated, extack);
		if (err)
			return err;
		break;
	case FLOW_ACTION_MIRRED:
		err = nfp_flower_output_action(app, act, nfp_fl, a_len, netdev,
					       false, tun_type, tun_out_cnt,
					       out_cnt, csum_updated, extack);
		if (err)
			return err;
		break;
	case FLOW_ACTION_VLAN_POP:
		if (*a_len +
		    sizeof(struct nfp_fl_pop_vlan) > NFP_FL_MAX_A_SIZ) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed action list size exceeded at pop vlan");
			return -EOPNOTSUPP;
		}

		pop_v = (struct nfp_fl_pop_vlan *)&nfp_fl->action_data[*a_len];
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_POPV);

		nfp_fl_pop_vlan(pop_v);
		*a_len += sizeof(struct nfp_fl_pop_vlan);
		break;
	case FLOW_ACTION_VLAN_PUSH:
		if (*a_len +
		    sizeof(struct nfp_fl_push_vlan) > NFP_FL_MAX_A_SIZ) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed action list size exceeded at push vlan");
			return -EOPNOTSUPP;
		}

		psh_v = (struct nfp_fl_push_vlan *)&nfp_fl->action_data[*a_len];
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);

		nfp_fl_push_vlan(psh_v, act);
		*a_len += sizeof(struct nfp_fl_push_vlan);
		break;
	case FLOW_ACTION_TUNNEL_ENCAP: {
		const struct ip_tunnel_info *ip_tun = act->tunnel;

		*tun_type = nfp_fl_get_tun_from_act(app, act);
		if (*tun_type == NFP_FL_TUNNEL_NONE) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: unsupported tunnel type in action list");
			return -EOPNOTSUPP;
		}

		if (ip_tun->mode & ~NFP_FL_SUPPORTED_TUNNEL_INFO_FLAGS) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: unsupported tunnel flags in action list");
			return -EOPNOTSUPP;
		}

		/* Pre-tunnel action is required for tunnel encap.
		 * This checks for next hop entries on NFP.
		 * If none, the packet falls back before applying other actions.
		 */
		if (*a_len + sizeof(struct nfp_fl_pre_tunnel) +
		    sizeof(struct nfp_fl_set_ipv4_tun) > NFP_FL_MAX_A_SIZ) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: maximum allowed action list size exceeded at tunnel encap");
			return -EOPNOTSUPP;
		}

		pre_tun = nfp_fl_pre_tunnel(nfp_fl->action_data, *a_len);
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);
		*a_len += sizeof(struct nfp_fl_pre_tunnel);

		err = nfp_fl_push_geneve_options(nfp_fl, a_len, act, extack);
		if (err)
			return err;

		set_tun = (void *)&nfp_fl->action_data[*a_len];
		err = nfp_fl_set_ipv4_tun(app, set_tun, act, pre_tun,
					  *tun_type, netdev, extack);
		if (err)
			return err;
		*a_len += sizeof(struct nfp_fl_set_ipv4_tun);
		}
		break;
	case FLOW_ACTION_TUNNEL_DECAP:
		/* Tunnel decap is handled by default so accept action. */
		return 0;
	case FLOW_ACTION_MANGLE:
		if (nfp_fl_pedit(act, flow, &nfp_fl->action_data[*a_len],
				 a_len, csum_updated, set_act, extack))
			return -EOPNOTSUPP;
		break;
	case FLOW_ACTION_CSUM:
		/* csum action requests recalc of something we have not fixed */
		if (act->csum_flags & ~*csum_updated) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: unsupported csum update action in action list");
			return -EOPNOTSUPP;
		}
		/* If we will correctly fix the csum we can remove it from the
		 * csum update list. Which will later be used to check support.
		 */
		*csum_updated &= ~act->csum_flags;
		break;
	default:
		/* Currently we do not handle any other actions. */
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: unsupported action in action list");
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool nfp_fl_check_mangle_start(struct flow_action *flow_act,
				      int current_act_idx)
{
	struct flow_action_entry current_act;
	struct flow_action_entry prev_act;

	current_act = flow_act->entries[current_act_idx];
	if (current_act.id != FLOW_ACTION_MANGLE)
		return false;

	if (current_act_idx == 0)
		return true;

	prev_act = flow_act->entries[current_act_idx - 1];

	return prev_act.id != FLOW_ACTION_MANGLE;
}

static bool nfp_fl_check_mangle_end(struct flow_action *flow_act,
				    int current_act_idx)
{
	struct flow_action_entry current_act;
	struct flow_action_entry next_act;

	current_act = flow_act->entries[current_act_idx];
	if (current_act.id != FLOW_ACTION_MANGLE)
		return false;

	if (current_act_idx == flow_act->num_entries)
		return true;

	next_act = flow_act->entries[current_act_idx + 1];

	return next_act.id != FLOW_ACTION_MANGLE;
}

int nfp_flower_compile_action(struct nfp_app *app,
			      struct tc_cls_flower_offload *flow,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow,
			      struct netlink_ext_ack *extack)
{
	int act_len, act_cnt, err, tun_out_cnt, out_cnt, i;
	struct nfp_flower_pedit_acts set_act;
	enum nfp_flower_tun_type tun_type;
	struct flow_action_entry *act;
	u32 csum_updated = 0;

	memset(nfp_flow->action_data, 0, NFP_FL_MAX_A_SIZ);
	nfp_flow->meta.act_len = 0;
	tun_type = NFP_FL_TUNNEL_NONE;
	act_len = 0;
	act_cnt = 0;
	tun_out_cnt = 0;
	out_cnt = 0;

	flow_action_for_each(i, act, &flow->rule->action) {
		if (nfp_fl_check_mangle_start(&flow->rule->action, i))
			memset(&set_act, 0, sizeof(set_act));
		err = nfp_flower_loop_action(app, act, flow, nfp_flow, &act_len,
					     netdev, &tun_type, &tun_out_cnt,
					     &out_cnt, &csum_updated, &set_act,
					     extack);
		if (err)
			return err;
		act_cnt++;
		if (nfp_fl_check_mangle_end(&flow->rule->action, i))
			nfp_fl_commit_mangle(flow,
					     &nfp_flow->action_data[act_len],
					     &act_len, &set_act, &csum_updated);
	}

	/* We optimise when the action list is small, this can unfortunately
	 * not happen once we have more than one action in the action list.
	 */
	if (act_cnt > 1)
		nfp_flow->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);

	nfp_flow->meta.act_len = act_len;

	return 0;
}
