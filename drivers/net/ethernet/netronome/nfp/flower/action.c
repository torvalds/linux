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
#include <net/pkt_cls.h>
#include <net/switchdev.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_net_repr.h"

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

static bool nfp_fl_netdev_is_tunnel_type(struct net_device *out_dev,
					 enum nfp_flower_tun_type tun_type)
{
	if (!out_dev->rtnl_link_ops)
		return false;

	if (!strcmp(out_dev->rtnl_link_ops->kind, "vxlan"))
		return tun_type == NFP_FL_TUNNEL_VXLAN;

	return false;
}

static int
nfp_fl_output(struct nfp_fl_output *output, const struct tc_action *action,
	      struct nfp_fl_payload *nfp_flow, bool last,
	      struct net_device *in_dev, enum nfp_flower_tun_type tun_type,
	      int *tun_out_cnt)
{
	size_t act_size = sizeof(struct nfp_fl_output);
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

static bool nfp_fl_supported_tun_port(const struct tc_action *action)
{
	struct ip_tunnel_info *tun = tcf_tunnel_info(action);

	return tun->key.tp_dst == htons(NFP_FL_VXLAN_PORT);
}

static struct nfp_fl_pre_tunnel *nfp_fl_pre_tunnel(char *act_data, int act_len)
{
	size_t act_size = sizeof(struct nfp_fl_pre_tunnel);
	struct nfp_fl_pre_tunnel *pre_tun_act;

	/* Pre_tunnel action must be first on action list.
	 * If other actions already exist they need pushed forward.
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
nfp_fl_set_vxlan(struct nfp_fl_set_vxlan *set_vxlan,
		 const struct tc_action *action,
		 struct nfp_fl_pre_tunnel *pre_tun)
{
	struct ip_tunnel_info *vxlan = tcf_tunnel_info(action);
	size_t act_size = sizeof(struct nfp_fl_set_vxlan);
	u32 tmp_set_vxlan_type_index = 0;
	/* Currently support one pre-tunnel so index is always 0. */
	int pretun_idx = 0;

	if (vxlan->options_len) {
		/* Do not support options e.g. vxlan gpe. */
		return -EOPNOTSUPP;
	}

	set_vxlan->head.jump_id = NFP_FL_ACTION_OPCODE_SET_IPV4_TUNNEL;
	set_vxlan->head.len_lw = act_size >> NFP_FL_LW_SIZ;

	/* Set tunnel type and pre-tunnel index. */
	tmp_set_vxlan_type_index |=
		FIELD_PREP(NFP_FL_IPV4_TUNNEL_TYPE, NFP_FL_TUNNEL_VXLAN) |
		FIELD_PREP(NFP_FL_IPV4_PRE_TUN_INDEX, pretun_idx);

	set_vxlan->tun_type_index = cpu_to_be32(tmp_set_vxlan_type_index);

	set_vxlan->tun_id = vxlan->key.tun_id;
	set_vxlan->tun_flags = vxlan->key.tun_flags;
	set_vxlan->ipv4_ttl = vxlan->key.ttl;
	set_vxlan->ipv4_tos = vxlan->key.tos;

	/* Complete pre_tunnel action. */
	pre_tun->ipv4_dst = vxlan->key.u.ipv4.dst;

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

static int
nfp_fl_pedit(const struct tc_action *action, char *nfp_action, int *a_len)
{
	struct nfp_fl_set_ipv6_addr set_ip6_dst, set_ip6_src;
	struct nfp_fl_set_ip4_addrs set_ip_addr;
	struct nfp_fl_set_tport set_tport;
	struct nfp_fl_set_eth set_eth;
	enum pedit_header_type htype;
	int idx, nkeys, err;
	size_t act_size;
	u32 offset, cmd;

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

	if (set_eth.head.len_lw) {
		act_size = sizeof(set_eth);
		memcpy(nfp_action, &set_eth, act_size);
		*a_len += act_size;
	} else if (set_ip_addr.head.len_lw) {
		act_size = sizeof(set_ip_addr);
		memcpy(nfp_action, &set_ip_addr, act_size);
		*a_len += act_size;
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
	} else if (set_ip6_dst.head.len_lw) {
		act_size = sizeof(set_ip6_dst);
		memcpy(nfp_action, &set_ip6_dst, act_size);
		*a_len += act_size;
	} else if (set_ip6_src.head.len_lw) {
		act_size = sizeof(set_ip6_src);
		memcpy(nfp_action, &set_ip6_src, act_size);
		*a_len += act_size;
	} else if (set_tport.head.len_lw) {
		act_size = sizeof(set_tport);
		memcpy(nfp_action, &set_tport, act_size);
		*a_len += act_size;
	}

	return 0;
}

static int
nfp_flower_loop_action(const struct tc_action *a,
		       struct nfp_fl_payload *nfp_fl, int *a_len,
		       struct net_device *netdev,
		       enum nfp_flower_tun_type *tun_type, int *tun_out_cnt)
{
	struct nfp_fl_pre_tunnel *pre_tun;
	struct nfp_fl_set_vxlan *s_vxl;
	struct nfp_fl_push_vlan *psh_v;
	struct nfp_fl_pop_vlan *pop_v;
	struct nfp_fl_output *output;
	int err;

	if (is_tcf_gact_shot(a)) {
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_DROP);
	} else if (is_tcf_mirred_egress_redirect(a)) {
		if (*a_len + sizeof(struct nfp_fl_output) > NFP_FL_MAX_A_SIZ)
			return -EOPNOTSUPP;

		output = (struct nfp_fl_output *)&nfp_fl->action_data[*a_len];
		err = nfp_fl_output(output, a, nfp_fl, true, netdev, *tun_type,
				    tun_out_cnt);
		if (err)
			return err;

		*a_len += sizeof(struct nfp_fl_output);
	} else if (is_tcf_mirred_egress_mirror(a)) {
		if (*a_len + sizeof(struct nfp_fl_output) > NFP_FL_MAX_A_SIZ)
			return -EOPNOTSUPP;

		output = (struct nfp_fl_output *)&nfp_fl->action_data[*a_len];
		err = nfp_fl_output(output, a, nfp_fl, false, netdev, *tun_type,
				    tun_out_cnt);
		if (err)
			return err;

		*a_len += sizeof(struct nfp_fl_output);
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
	} else if (is_tcf_tunnel_set(a) && nfp_fl_supported_tun_port(a)) {
		/* Pre-tunnel action is required for tunnel encap.
		 * This checks for next hop entries on NFP.
		 * If none, the packet falls back before applying other actions.
		 */
		if (*a_len + sizeof(struct nfp_fl_pre_tunnel) +
		    sizeof(struct nfp_fl_set_vxlan) > NFP_FL_MAX_A_SIZ)
			return -EOPNOTSUPP;

		*tun_type = NFP_FL_TUNNEL_VXLAN;
		pre_tun = nfp_fl_pre_tunnel(nfp_fl->action_data, *a_len);
		nfp_fl->meta.shortcut = cpu_to_be32(NFP_FL_SC_ACT_NULL);
		*a_len += sizeof(struct nfp_fl_pre_tunnel);

		s_vxl = (struct nfp_fl_set_vxlan *)&nfp_fl->action_data[*a_len];
		err = nfp_fl_set_vxlan(s_vxl, a, pre_tun);
		if (err)
			return err;

		*a_len += sizeof(struct nfp_fl_set_vxlan);
	} else if (is_tcf_tunnel_release(a)) {
		/* Tunnel decap is handled by default so accept action. */
		return 0;
	} else if (is_tcf_pedit(a)) {
		if (nfp_fl_pedit(a, &nfp_fl->action_data[*a_len], a_len))
			return -EOPNOTSUPP;
	} else {
		/* Currently we do not handle any other actions. */
		return -EOPNOTSUPP;
	}

	return 0;
}

int nfp_flower_compile_action(struct tc_cls_flower_offload *flow,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow)
{
	int act_len, act_cnt, err, tun_out_cnt;
	enum nfp_flower_tun_type tun_type;
	const struct tc_action *a;
	LIST_HEAD(actions);

	memset(nfp_flow->action_data, 0, NFP_FL_MAX_A_SIZ);
	nfp_flow->meta.act_len = 0;
	tun_type = NFP_FL_TUNNEL_NONE;
	act_len = 0;
	act_cnt = 0;
	tun_out_cnt = 0;

	tcf_exts_to_list(flow->exts, &actions);
	list_for_each_entry(a, &actions, list) {
		err = nfp_flower_loop_action(a, nfp_flow, &act_len, netdev,
					     &tun_type, &tun_out_cnt);
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
