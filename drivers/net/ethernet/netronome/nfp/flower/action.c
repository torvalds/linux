/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/bitfield.h>
#include <net/geneve.h>
#include <net/pkt_cls.h>
#include <net/switchdev.h>
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
		 const struct tc_action *action)
{
	size_t act_size = sizeof(struct nfp_fl_push_vlan);
	u16 tmp_push_vlan_tci;

	push_vlan->head.jump_id = NFP_FL_ACTION_OPCODE_PUSH_VLAN;
	push_vlan->head.len_lw = act_size >> NFP_FL_LW_SIZ;
	push_vlan->reserved = 0;
	push_vlan->vlan_tpid = tcf_vlan_push_proto(action);

	tmp_push_vlan_tci =
		FIELD_PREP(NFP_FL_PUSH_VLAN_PRIO, tcf_vlan_push_prio(action)) |
		FIELD_PREP(NFP_FL_PUSH_VLAN_VID, tcf_vlan_push_vid(action)) |
		NFP_FL_PUSH_VLAN_CFI;
	push_vlan->vlan_tci = cpu_to_be16(tmp_push_vlan_tci);
}

static int
nfp_fl_pre_lag(struct nfp_app *app, const struct tc_action *action,
	       struct nfp_fl_payload *nfp_flow, int act_len)
{
	size_t act_size = sizeof(struct nfp_fl_pre_lag);
	struct nfp_fl_pre_lag *pre_lag;
	struct net_device *out_dev;
	int err;

	out_dev = tcf_mirred_dev(action);
	if (!out_dev || !netif_is_lag_master(out_dev))
		return 0;

	if (act_len + act_size > NFP_FL_MAX_A_SIZ)
		return -EOPNOTSUPP;

	/* Pre_lag action must be first on action list.
	 * If other actions already exist they need pushed forward.
	 */
	if (act_len)
		memmove(nfp_flow->action_data + act_size,
			nfp_flow->action_data, act_len);

	pre_lag = (struct nfp_fl_pre_lag *)nfp_flow->action_data;
	err = nfp_flower_lag_populate_pre_action(app, out_dev, pre_lag);
	if (err)
		return err;

	pre_lag->head.jump_id = NFP_FL_ACTION_OPCODE_PRE_LAG;
	pre_lag->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	nfp_flow->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);

	return act_size;
}

static bool nfp_fl_netdev_is_tunnel_type(struct net_device *out_dev,
					 enum nfp_flower_tun_type tun_type)
{
	if (!out_dev->rtnl_link_ops)
		return false;

	if (!strcmp(out_dev->rtnl_link_ops->kind, "vxlan"))
		return tun_type == NFP_FL_TUNNEL_VXLAN;

	if (!strcmp(out_dev->rtnl_link_ops->kind, "geneve"))
		return tun_type == NFP_FL_TUNNEL_GENEVE;

	return false;
}

static int
nfp_fl_output(struct nfp_app *app, struct nfp_fl_output *output,
	      const struct tc_action *action, struct nfp_fl_payload *nfp_flow,
	      bool last, struct net_device *in_dev,
	      enum nfp_flower_tun_type tun_type, int *tun_out_cnt)
{
	size_t act_size = sizeof(struct nfp_fl_output);
	struct nfp_flower_priv *priv = app->priv;
	struct net_device *out_dev;
	u16 tmp_flags;

	output->head.jump_id = NFP_FL_ACTION_OPCODE_OUTPUT;
	output->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	out_dev = tcf_mirred_dev(action);
	if (!out_dev)
		return -EOPNOTSUPP;

	tmp_flags = last ? NFP_FL_OUT_FLAGS_LAST : 0;

	if (tun_type) {
		/* Verify the egress netdev matches the tunnel type. */
		if (!nfp_fl_netdev_is_tunnel_type(out_dev, tun_type))
			return -EOPNOTSUPP;

		if (*tun_out_cnt)
			return -EOPNOTSUPP;
		(*tun_out_cnt)++;

		output->flags = cpu_to_be16(tmp_flags |
					    NFP_FL_OUT_FLAGS_USE_TUN);
		output->port = cpu_to_be32(NFP_FL_PORT_TYPE_TUN | tun_type);
	} else if (netif_is_lag_master(out_dev) &&
		   priv->flower_ext_feats & NFP_FL_FEATS_LAG) {
		int gid;

		output->flags = cpu_to_be16(tmp_flags);
		gid = nfp_flower_lag_get_output_id(app, out_dev);
		if (gid < 0)
			return gid;
		output->port = cpu_to_be32(NFP_FL_LAG_OUT | gid);
	} else {
		/* Set action output parameters. */
		output->flags = cpu_to_be16(tmp_flags);

		/* Only offload if egress ports are on the same device as the
		 * ingress port.
		 */
		if (!switchdev_port_same_parent_id(in_dev, out_dev))
			return -EOPNOTSUPP;
		if (!nfp_netdev_is_nfp_repr(out_dev))
			return -EOPNOTSUPP;

		output->port = cpu_to_be32(nfp_repr_get_port_id(out_dev));
		if (!output->port)
			return -EOPNOTSUPP;
	}
	nfp_flow->meta.shortcut = output->port;

	return 0;
}

static enum nfp_flower_tun_type
nfp_fl_get_tun_from_act_l4_port(struct nfp_app *app,
				const struct tc_action *action)
{
	struct ip_tunnel_info *tun = tcf_tunnel_info(action);
	struct nfp_flower_priv *priv = app->priv;

	switch (tun->key.tp_dst) {
	case htons(NFP_FL_VXLAN_PORT):
		return NFP_FL_TUNNEL_VXLAN;
	case htons(NFP_FL_GENEVE_PORT):
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
			   const struct tc_action *action)
{
	struct ip_tunnel_info *ip_tun = tcf_tunnel_info(action);
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
		if (opt_cnt > NFP_FL_MAX_GENEVE_OPT_CNT)
			return -EOPNOTSUPP;

		tot_push_len += sizeof(struct nfp_fl_push_geneve) +
			       opt->length * 4;
		if (tot_push_len > NFP_FL_MAX_GENEVE_OPT_ACT)
			return -EOPNOTSUPP;

		opt_len -= sizeof(struct geneve_opt) + opt->length * 4;
		src += sizeof(struct geneve_opt) + opt->length * 4;
	}

	if (*list_len + tot_push_len > NFP_FL_MAX_A_SIZ)
		return -EOPNOTSUPP;

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
nfp_fl_set_ipv4_udp_tun(struct nfp_app *app,
			struct nfp_fl_set_ipv4_udp_tun *set_tun,
			const struct tc_action *action,
			struct nfp_fl_pre_tunnel *pre_tun,
			enum nfp_flower_tun_type tun_type,
			struct net_device *netdev)
{
	size_t act_size = sizeof(struct nfp_fl_set_ipv4_udp_tun);
	struct ip_tunnel_info *ip_tun = tcf_tunnel_info(action);
	struct nfp_flower_priv *priv = app->priv;
	u32 tmp_set_ip_tun_type_index = 0;
	/* Currently support one pre-tunnel so index is always 0. */
	int pretun_idx = 0;

	BUILD_BUG_ON(NFP_FL_TUNNEL_CSUM != TUNNEL_CSUM ||
		     NFP_FL_TUNNEL_KEY	!= TUNNEL_KEY ||
		     NFP_FL_TUNNEL_GENEVE_OPT != TUNNEL_GENEVE_OPT);
	if (ip_tun->options_len &&
	    (tun_type != NFP_FL_TUNNEL_GENEVE ||
	    !(priv->flower_ext_feats & NFP_FL_FEATS_GENEVE_OPT)))
		return -EOPNOTSUPP;

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
	    ip_tun->key.tun_flags & ~NFP_FL_SUPPORTED_IPV4_UDP_TUN_FLAGS)
		return -EOPNOTSUPP;
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
nfp_fl_set_eth(const struct tc_action *action, int idx, u32 off,
	       struct nfp_fl_set_eth *set_eth)
{
	u32 exact, mask;

	if (off + 4 > ETH_ALEN * 2)
		return -EOPNOTSUPP;

	mask = ~tcf_pedit_mask(action, idx);
	exact = tcf_pedit_val(action, idx);

	if (exact & ~mask)
		return -EOPNOTSUPP;

	nfp_fl_set_helper32(exact, mask, &set_eth->eth_addr_val[off],
			    &set_eth->eth_addr_mask[off]);

	set_eth->reserved = cpu_to_be16(0);
	set_eth->head.jump_id = NFP_FL_ACTION_OPCODE_SET_ETHERNET;
	set_eth->head.len_lw = sizeof(*set_eth) >> NFP_FL_LW_SIZ;

	return 0;
}

static int
nfp_fl_set_ip4(const struct tc_action *action, int idx, u32 off,
	       struct nfp_fl_set_ip4_addrs *set_ip_addr)
{
	__be32 exact, mask;

	/* We are expecting tcf_pedit to return a big endian value */
	mask = (__force __be32)~tcf_pedit_mask(action, idx);
	exact = (__force __be32)tcf_pedit_val(action, idx);

	if (exact & ~mask)
		return -EOPNOTSUPP;

	switch (off) {
	case offsetof(struct iphdr, daddr):
		set_ip_addr->ipv4_dst_mask = mask;
		set_ip_addr->ipv4_dst = exact;
		break;
	case offsetof(struct iphdr, saddr):
		set_ip_addr->ipv4_src_mask = mask;
		set_ip_addr->ipv4_src = exact;
		break;
	default:
		return -EOPNOTSUPP;
	}

	set_ip_addr->reserved = cpu_to_be16(0);
	set_ip_addr->head.jump_id = NFP_FL_ACTION_OPCODE_SET_IPV4_ADDRS;
	set_ip_addr->head.len_lw = sizeof(*set_ip_addr) >> NFP_FL_LW_SIZ;

	return 0;
}

static void
nfp_fl_set_ip6_helper(int opcode_tag, int idx, __be32 exact, __be32 mask,
		      struct nfp_fl_set_ipv6_addr *ip6)
{
	ip6->ipv6[idx % 4].mask = mask;
	ip6->ipv6[idx % 4].exact = exact;

	ip6->reserved = cpu_to_be16(0);
	ip6->head.jump_id = opcode_tag;
	ip6->head.len_lw = sizeof(*ip6) >> NFP_FL_LW_SIZ;
}

static int
nfp_fl_set_ip6(const struct tc_action *action, int idx, u32 off,
	       struct nfp_fl_set_ipv6_addr *ip_dst,
	       struct nfp_fl_set_ipv6_addr *ip_src)
{
	__be32 exact, mask;

	/* We are expecting tcf_pedit to return a big endian value */
	mask = (__force __be32)~tcf_pedit_mask(action, idx);
	exact = (__force __be32)tcf_pedit_val(action, idx);

	if (exact & ~mask)
		return -EOPNOTSUPP;

	if (off < offsetof(struct ipv6hdr, saddr))
		return -EOPNOTSUPP;
	else if (off < offsetof(struct ipv6hdr, daddr))
		nfp_fl_set_ip6_helper(NFP_FL_ACTION_OPCODE_SET_IPV6_SRC, idx,
				      exact, mask, ip_src);
	else if (off < offsetof(struct ipv6hdr, daddr) +
		       sizeof(struct in6_addr))
		nfp_fl_set_ip6_helper(NFP_FL_ACTION_OPCODE_SET_IPV6_DST, idx,
				      exact, mask, ip_dst);
	else
		return -EOPNOTSUPP;

	return 0;
}

static int
nfp_fl_set_tport(const struct tc_action *action, int idx, u32 off,
		 struct nfp_fl_set_tport *set_tport, int opcode)
{
	u32 exact, mask;

	if (off)
		return -EOPNOTSUPP;

	mask = ~tcf_pedit_mask(action, idx);
	exact = tcf_pedit_val(action, idx);

	if (exact & ~mask)
		return -EOPNOTSUPP;

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

static int
nfp_fl_pedit(const struct tc_action *action, struct tc_cls_flower_offload *flow,
	     char *nfp_action, int *a_len, u32 *csum_updated)
{
	struct nfp_fl_set_ipv6_addr set_ip6_dst, set_ip6_src;
	struct nfp_fl_set_ip4_addrs set_ip_addr;
	struct nfp_fl_set_tport set_tport;
	struct nfp_fl_set_eth set_eth;
	enum pedit_header_type htype;
	int idx, nkeys, err;
	size_t act_size;
	u32 offset, cmd;
	u8 ip_proto = 0;

	memset(&set_ip6_dst, 0, sizeof(set_ip6_dst));
	memset(&set_ip6_src, 0, sizeof(set_ip6_src));
	memset(&set_ip_addr, 0, sizeof(set_ip_addr));
	memset(&set_tport, 0, sizeof(set_tport));
	memset(&set_eth, 0, sizeof(set_eth));
	nkeys = tcf_pedit_nkeys(action);

	for (idx = 0; idx < nkeys; idx++) {
		cmd = tcf_pedit_cmd(action, idx);
		htype = tcf_pedit_htype(action, idx);
		offset = tcf_pedit_offset(action, idx);

		if (cmd != TCA_PEDIT_KEY_EX_CMD_SET)
			return -EOPNOTSUPP;

		switch (htype) {
		case TCA_PEDIT_KEY_EX_HDR_TYPE_ETH:
			err = nfp_fl_set_eth(action, idx, offset, &set_eth);
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_IP4:
			err = nfp_fl_set_ip4(action, idx, offset, &set_ip_addr);
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_IP6:
			err = nfp_fl_set_ip6(action, idx, offset, &set_ip6_dst,
					     &set_ip6_src);
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_TCP:
			err = nfp_fl_set_tport(action, idx, offset, &set_tport,
					       NFP_FL_ACTION_OPCODE_SET_TCP);
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_UDP:
			err = nfp_fl_set_tport(action, idx, offset, &set_tport,
					       NFP_FL_ACTION_OPCODE_SET_UDP);
			break;
		default:
			return -EOPNOTSUPP;
		}
		if (err)
			return err;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *basic;

		basic = skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  flow->key);
		ip_proto = basic->ip_proto;
	}

	if (set_eth.head.len_lw) {
		act_size = sizeof(set_eth);
		memcpy(nfp_action, &set_eth, act_size);
		*a_len += act_size;
	} else if (set_ip_addr.head.len_lw) {
		act_size = sizeof(set_ip_addr);
		memcpy(nfp_action, &set_ip_addr, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix IPv4 and TCP/UDP checksum. */
		*csum_updated |= TCA_CSUM_UPDATE_FLAG_IPV4HDR |
				nfp_fl_csum_l4_to_flag(ip_proto);
	} else if (set_ip6_dst.head.len_lw && set_ip6_src.head.len_lw) {
		/* TC compiles set src and dst IPv6 address as a single action,
		 * the hardware requires this to be 2 separate actions.
		 */
		act_size = sizeof(set_ip6_src);
		memcpy(nfp_action, &set_ip6_src, act_size);
		*a_len += act_size;

		act_size = sizeof(set_ip6_dst);
		memcpy(&nfp_action[sizeof(set_ip6_src)], &set_ip6_dst,
		       act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	} else if (set_ip6_dst.head.len_lw) {
		act_size = sizeof(set_ip6_dst);
		memcpy(nfp_action, &set_ip6_dst, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	} else if (set_ip6_src.head.len_lw) {
		act_size = sizeof(set_ip6_src);
		memcpy(nfp_action, &set_ip6_src, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	} else if (set_tport.head.len_lw) {
		act_size = sizeof(set_tport);
		memcpy(nfp_action, &set_tport, act_size);
		*a_len += act_size;

		/* Hardware will automatically fix TCP/UDP checksum. */
		*csum_updated |= nfp_fl_csum_l4_to_flag(ip_proto);
	}

	return 0;
}

static int
nfp_flower_output_action(struct nfp_app *app, const struct tc_action *a,
			 struct nfp_fl_payload *nfp_fl, int *a_len,
			 struct net_device *netdev, bool last,
			 enum nfp_flower_tun_type *tun_type, int *tun_out_cnt,
			 int *out_cnt, u32 *csum_updated)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_output *output;
	int err, prelag_size;

	/* If csum_updated has not been reset by now, it means HW will
	 * incorrectly update csums when they are not requested.
	 */
	if (*csum_updated)
		return -EOPNOTSUPP;

	if (*a_len + sizeof(struct nfp_fl_output) > NFP_FL_MAX_A_SIZ)
		return -EOPNOTSUPP;

	output = (struct nfp_fl_output *)&nfp_fl->action_data[*a_len];
	err = nfp_fl_output(app, output, a, nfp_fl, last, netdev, *tun_type,
			    tun_out_cnt);
	if (err)
		return err;

	*a_len += sizeof(struct nfp_fl_output);

	if (priv->flower_ext_feats & NFP_FL_FEATS_LAG) {
		/* nfp_fl_pre_lag returns -err or size of prelag action added.
		 * This will be 0 if it is not egressing to a lag dev.
		 */
		prelag_size = nfp_fl_pre_lag(app, a, nfp_fl, *a_len);
		if (prelag_size < 0)
			return prelag_size;
		else if (prelag_size > 0 && (!last || *out_cnt))
			return -EOPNOTSUPP;

		*a_len += prelag_size;
	}
	(*out_cnt)++;

	return 0;
}

static int
nfp_flower_loop_action(struct nfp_app *app, const struct tc_action *a,
		       struct tc_cls_flower_offload *flow,
		       struct nfp_fl_payload *nfp_fl, int *a_len,
		       struct net_device *netdev,
		       enum nfp_flower_tun_type *tun_type, int *tun_out_cnt,
		       int *out_cnt, u32 *csum_updated)
{
	struct nfp_fl_set_ipv4_udp_tun *set_tun;
	struct nfp_fl_pre_tunnel *pre_tun;
	struct nfp_fl_push_vlan *psh_v;
	struct nfp_fl_pop_vlan *pop_v;
	int err;

	if (is_tcf_gact_shot(a)) {
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_DROP);
	} else if (is_tcf_mirred_egress_redirect(a)) {
		err = nfp_flower_output_action(app, a, nfp_fl, a_len, netdev,
					       true, tun_type, tun_out_cnt,
					       out_cnt, csum_updated);
		if (err)
			return err;

	} else if (is_tcf_mirred_egress_mirror(a)) {
		err = nfp_flower_output_action(app, a, nfp_fl, a_len, netdev,
					       false, tun_type, tun_out_cnt,
					       out_cnt, csum_updated);
		if (err)
			return err;

	} else if (is_tcf_vlan(a) && tcf_vlan_action(a) == TCA_VLAN_ACT_POP) {
		if (*a_len + sizeof(struct nfp_fl_pop_vlan) > NFP_FL_MAX_A_SIZ)
			return -EOPNOTSUPP;

		pop_v = (struct nfp_fl_pop_vlan *)&nfp_fl->action_data[*a_len];
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_POPV);

		nfp_fl_pop_vlan(pop_v);
		*a_len += sizeof(struct nfp_fl_pop_vlan);
	} else if (is_tcf_vlan(a) && tcf_vlan_action(a) == TCA_VLAN_ACT_PUSH) {
		if (*a_len + sizeof(struct nfp_fl_push_vlan) > NFP_FL_MAX_A_SIZ)
			return -EOPNOTSUPP;

		psh_v = (struct nfp_fl_push_vlan *)&nfp_fl->action_data[*a_len];
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);

		nfp_fl_push_vlan(psh_v, a);
		*a_len += sizeof(struct nfp_fl_push_vlan);
	} else if (is_tcf_tunnel_set(a)) {
		struct ip_tunnel_info *ip_tun = tcf_tunnel_info(a);
		struct nfp_repr *repr = netdev_priv(netdev);

		*tun_type = nfp_fl_get_tun_from_act_l4_port(repr->app, a);
		if (*tun_type == NFP_FL_TUNNEL_NONE)
			return -EOPNOTSUPP;

		if (ip_tun->mode & ~NFP_FL_SUPPORTED_TUNNEL_INFO_FLAGS)
			return -EOPNOTSUPP;

		/* Pre-tunnel action is required for tunnel encap.
		 * This checks for next hop entries on NFP.
		 * If none, the packet falls back before applying other actions.
		 */
		if (*a_len + sizeof(struct nfp_fl_pre_tunnel) +
		    sizeof(struct nfp_fl_set_ipv4_udp_tun) > NFP_FL_MAX_A_SIZ)
			return -EOPNOTSUPP;

		pre_tun = nfp_fl_pre_tunnel(nfp_fl->action_data, *a_len);
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);
		*a_len += sizeof(struct nfp_fl_pre_tunnel);

		err = nfp_fl_push_geneve_options(nfp_fl, a_len, a);
		if (err)
			return err;

		set_tun = (void *)&nfp_fl->action_data[*a_len];
		err = nfp_fl_set_ipv4_udp_tun(app, set_tun, a, pre_tun,
					      *tun_type, netdev);
		if (err)
			return err;
		*a_len += sizeof(struct nfp_fl_set_ipv4_udp_tun);
	} else if (is_tcf_tunnel_release(a)) {
		/* Tunnel decap is handled by default so accept action. */
		return 0;
	} else if (is_tcf_pedit(a)) {
		if (nfp_fl_pedit(a, flow, &nfp_fl->action_data[*a_len],
				 a_len, csum_updated))
			return -EOPNOTSUPP;
	} else if (is_tcf_csum(a)) {
		/* csum action requests recalc of something we have not fixed */
		if (tcf_csum_update_flags(a) & ~*csum_updated)
			return -EOPNOTSUPP;
		/* If we will correctly fix the csum we can remove it from the
		 * csum update list. Which will later be used to check support.
		 */
		*csum_updated &= ~tcf_csum_update_flags(a);
	} else {
		/* Currently we do not handle any other actions. */
		return -EOPNOTSUPP;
	}

	return 0;
}

int nfp_flower_compile_action(struct nfp_app *app,
			      struct tc_cls_flower_offload *flow,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow)
{
	int act_len, act_cnt, err, tun_out_cnt, out_cnt, i;
	enum nfp_flower_tun_type tun_type;
	const struct tc_action *a;
	u32 csum_updated = 0;

	memset(nfp_flow->action_data, 0, NFP_FL_MAX_A_SIZ);
	nfp_flow->meta.act_len = 0;
	tun_type = NFP_FL_TUNNEL_NONE;
	act_len = 0;
	act_cnt = 0;
	tun_out_cnt = 0;
	out_cnt = 0;

	tcf_exts_for_each_action(i, a, flow->exts) {
		err = nfp_flower_loop_action(app, a, flow, nfp_flow, &act_len,
					     netdev, &tun_type, &tun_out_cnt,
					     &out_cnt, &csum_updated);
		if (err)
			return err;
		act_cnt++;
	}

	/* We optimise when the action list is small, this can unfortunately
	 * not happen once we have more than one action in the action list.
	 */
	if (act_cnt > 1)
		nfp_flow->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);

	nfp_flow->meta.act_len = act_len;

	return 0;
}
