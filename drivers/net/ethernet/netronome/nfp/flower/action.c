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
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_net_repr.h"

static void nfp_fl_pop_vlan(struct nfp_fl_pop_vlan *pop_vlan)
{
	size_t act_size = sizeof(struct nfp_fl_pop_vlan);
	u16 tmp_pop_vlan_op;

	tmp_pop_vlan_op =
		FIELD_PREP(NFP_FL_ACT_LEN_LW, act_size >> NFP_FL_LW_SIZ) |
		FIELD_PREP(NFP_FL_ACT_JMP_ID, NFP_FL_ACTION_OPCODE_POP_VLAN);

	pop_vlan->a_op = cpu_to_be16(tmp_pop_vlan_op);
	pop_vlan->reserved = 0;
}

static void
nfp_fl_push_vlan(struct nfp_fl_push_vlan *push_vlan,
		 const struct tc_action *action)
{
	size_t act_size = sizeof(struct nfp_fl_push_vlan);
	struct tcf_vlan *vlan = to_vlan(action);
	u16 tmp_push_vlan_tci;
	u16 tmp_push_vlan_op;

	tmp_push_vlan_op =
		FIELD_PREP(NFP_FL_ACT_LEN_LW, act_size >> NFP_FL_LW_SIZ) |
		FIELD_PREP(NFP_FL_ACT_JMP_ID, NFP_FL_ACTION_OPCODE_PUSH_VLAN);

	push_vlan->a_op = cpu_to_be16(tmp_push_vlan_op);
	/* Set action push vlan parameters. */
	push_vlan->reserved = 0;
	push_vlan->vlan_tpid = tcf_vlan_push_proto(action);

	tmp_push_vlan_tci =
		FIELD_PREP(NFP_FL_PUSH_VLAN_PRIO, vlan->tcfv_push_prio) |
		FIELD_PREP(NFP_FL_PUSH_VLAN_VID, vlan->tcfv_push_vid) |
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
	u16 tmp_output_op, tmp_flags;
	struct net_device *out_dev;
	int ifindex;

	/* Set action opcode to output action. */
	tmp_output_op =
		FIELD_PREP(NFP_FL_ACT_LEN_LW, act_size >> NFP_FL_LW_SIZ) |
		FIELD_PREP(NFP_FL_ACT_JMP_ID, NFP_FL_ACTION_OPCODE_OUTPUT);

	output->a_op = cpu_to_be16(tmp_output_op);

	ifindex = tcf_mirred_ifindex(action);
	out_dev = __dev_get_by_index(dev_net(in_dev), ifindex);
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
	u16 tmp_pre_tun_op;

	/* Pre_tunnel action must be first on action list.
	 * If other actions already exist they need pushed forward.
	 */
	if (act_len)
		memmove(act_data + act_size, act_data, act_len);

	pre_tun_act = (struct nfp_fl_pre_tunnel *)act_data;

	memset(pre_tun_act, 0, act_size);

	tmp_pre_tun_op =
		FIELD_PREP(NFP_FL_ACT_LEN_LW, act_size >> NFP_FL_LW_SIZ) |
		FIELD_PREP(NFP_FL_ACT_JMP_ID, NFP_FL_ACTION_OPCODE_PRE_TUNNEL);

	pre_tun_act->a_op = cpu_to_be16(tmp_pre_tun_op);

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
	u16 tmp_set_vxlan_op;
	/* Currently support one pre-tunnel so index is always 0. */
	int pretun_idx = 0;

	if (vxlan->options_len) {
		/* Do not support options e.g. vxlan gpe. */
		return -EOPNOTSUPP;
	}

	tmp_set_vxlan_op =
		FIELD_PREP(NFP_FL_ACT_LEN_LW, act_size >> NFP_FL_LW_SIZ) |
		FIELD_PREP(NFP_FL_ACT_JMP_ID,
			   NFP_FL_ACTION_OPCODE_SET_IPV4_TUNNEL);

	set_vxlan->a_op = cpu_to_be16(tmp_set_vxlan_op);

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
