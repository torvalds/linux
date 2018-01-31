/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <net/flow_dissector.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_skbedit.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_sriov.h"
#include "bnxt_tc.h"
#include "bnxt_vfr.h"

#define BNXT_FID_INVALID			0xffff
#define VLAN_TCI(vid, prio)	((vid) | ((prio) << VLAN_PRIO_SHIFT))

/* Return the dst fid of the func for flow forwarding
 * For PFs: src_fid is the fid of the PF
 * For VF-reps: src_fid the fid of the VF
 */
static u16 bnxt_flow_get_dst_fid(struct bnxt *pf_bp, struct net_device *dev)
{
	struct bnxt *bp;

	/* check if dev belongs to the same switch */
	if (!switchdev_port_same_parent_id(pf_bp->dev, dev)) {
		netdev_info(pf_bp->dev, "dev(ifindex=%d) not on same switch",
			    dev->ifindex);
		return BNXT_FID_INVALID;
	}

	/* Is dev a VF-rep? */
	if (bnxt_dev_is_vf_rep(dev))
		return bnxt_vf_rep_get_fid(dev);

	bp = netdev_priv(dev);
	return bp->pf.fw_fid;
}

static int bnxt_tc_parse_redir(struct bnxt *bp,
			       struct bnxt_tc_actions *actions,
			       const struct tc_action *tc_act)
{
	struct net_device *dev = tcf_mirred_dev(tc_act);

	if (!dev) {
		netdev_info(bp->dev, "no dev in mirred action");
		return -EINVAL;
	}

	actions->flags |= BNXT_TC_ACTION_FLAG_FWD;
	actions->dst_dev = dev;
	return 0;
}

static void bnxt_tc_parse_vlan(struct bnxt *bp,
			       struct bnxt_tc_actions *actions,
			       const struct tc_action *tc_act)
{
	if (tcf_vlan_action(tc_act) == TCA_VLAN_ACT_POP) {
		actions->flags |= BNXT_TC_ACTION_FLAG_POP_VLAN;
	} else if (tcf_vlan_action(tc_act) == TCA_VLAN_ACT_PUSH) {
		actions->flags |= BNXT_TC_ACTION_FLAG_PUSH_VLAN;
		actions->push_vlan_tci = htons(tcf_vlan_push_vid(tc_act));
		actions->push_vlan_tpid = tcf_vlan_push_proto(tc_act);
	}
}

static int bnxt_tc_parse_tunnel_set(struct bnxt *bp,
				    struct bnxt_tc_actions *actions,
				    const struct tc_action *tc_act)
{
	struct ip_tunnel_info *tun_info = tcf_tunnel_info(tc_act);
	struct ip_tunnel_key *tun_key = &tun_info->key;

	if (ip_tunnel_info_af(tun_info) != AF_INET) {
		netdev_info(bp->dev, "only IPv4 tunnel-encap is supported");
		return -EOPNOTSUPP;
	}

	actions->tun_encap_key = *tun_key;
	actions->flags |= BNXT_TC_ACTION_FLAG_TUNNEL_ENCAP;
	return 0;
}

static int bnxt_tc_parse_actions(struct bnxt *bp,
				 struct bnxt_tc_actions *actions,
				 struct tcf_exts *tc_exts)
{
	const struct tc_action *tc_act;
	LIST_HEAD(tc_actions);
	int rc;

	if (!tcf_exts_has_actions(tc_exts)) {
		netdev_info(bp->dev, "no actions");
		return -EINVAL;
	}

	tcf_exts_to_list(tc_exts, &tc_actions);
	list_for_each_entry(tc_act, &tc_actions, list) {
		/* Drop action */
		if (is_tcf_gact_shot(tc_act)) {
			actions->flags |= BNXT_TC_ACTION_FLAG_DROP;
			return 0; /* don't bother with other actions */
		}

		/* Redirect action */
		if (is_tcf_mirred_egress_redirect(tc_act)) {
			rc = bnxt_tc_parse_redir(bp, actions, tc_act);
			if (rc)
				return rc;
			continue;
		}

		/* Push/pop VLAN */
		if (is_tcf_vlan(tc_act)) {
			bnxt_tc_parse_vlan(bp, actions, tc_act);
			continue;
		}

		/* Tunnel encap */
		if (is_tcf_tunnel_set(tc_act)) {
			rc = bnxt_tc_parse_tunnel_set(bp, actions, tc_act);
			if (rc)
				return rc;
			continue;
		}

		/* Tunnel decap */
		if (is_tcf_tunnel_release(tc_act)) {
			actions->flags |= BNXT_TC_ACTION_FLAG_TUNNEL_DECAP;
			continue;
		}
	}

	if (actions->flags & BNXT_TC_ACTION_FLAG_FWD) {
		if (actions->flags & BNXT_TC_ACTION_FLAG_TUNNEL_ENCAP) {
			/* dst_fid is PF's fid */
			actions->dst_fid = bp->pf.fw_fid;
		} else {
			/* find the FID from dst_dev */
			actions->dst_fid =
				bnxt_flow_get_dst_fid(bp, actions->dst_dev);
			if (actions->dst_fid == BNXT_FID_INVALID)
				return -EINVAL;
		}
	}

	return 0;
}

#define GET_KEY(flow_cmd, key_type)					\
		skb_flow_dissector_target((flow_cmd)->dissector, key_type,\
					  (flow_cmd)->key)
#define GET_MASK(flow_cmd, key_type)					\
		skb_flow_dissector_target((flow_cmd)->dissector, key_type,\
					  (flow_cmd)->mask)

static int bnxt_tc_parse_flow(struct bnxt *bp,
			      struct tc_cls_flower_offload *tc_flow_cmd,
			      struct bnxt_tc_flow *flow)
{
	struct flow_dissector *dissector = tc_flow_cmd->dissector;
	u16 addr_type = 0;

	/* KEY_CONTROL and KEY_BASIC are needed for forming a meaningful key */
	if ((dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CONTROL)) == 0 ||
	    (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_BASIC)) == 0) {
		netdev_info(bp->dev, "cannot form TC key: used_keys = 0x%x",
			    dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_CONTROL);

		addr_type = key->addr_type;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_BASIC);
		struct flow_dissector_key_basic *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_BASIC);

		flow->l2_key.ether_type = key->n_proto;
		flow->l2_mask.ether_type = mask->n_proto;

		if (key->n_proto == htons(ETH_P_IP) ||
		    key->n_proto == htons(ETH_P_IPV6)) {
			flow->l4_key.ip_proto = key->ip_proto;
			flow->l4_mask.ip_proto = mask->ip_proto;
		}
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_dissector_key_eth_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ETH_ADDRS);
		struct flow_dissector_key_eth_addrs *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_ETH_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_ETH_ADDRS;
		ether_addr_copy(flow->l2_key.dmac, key->dst);
		ether_addr_copy(flow->l2_mask.dmac, mask->dst);
		ether_addr_copy(flow->l2_key.smac, key->src);
		ether_addr_copy(flow->l2_mask.smac, mask->src);
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_VLAN);
		struct flow_dissector_key_vlan *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_VLAN);

		flow->l2_key.inner_vlan_tci =
		   cpu_to_be16(VLAN_TCI(key->vlan_id, key->vlan_priority));
		flow->l2_mask.inner_vlan_tci =
		   cpu_to_be16((VLAN_TCI(mask->vlan_id, mask->vlan_priority)));
		flow->l2_key.inner_vlan_tpid = htons(ETH_P_8021Q);
		flow->l2_mask.inner_vlan_tpid = htons(0xffff);
		flow->l2_key.num_vlans = 1;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_dissector_key_ipv4_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV4_ADDRS);
		struct flow_dissector_key_ipv4_addrs *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV4_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_IPV4_ADDRS;
		flow->l3_key.ipv4.daddr.s_addr = key->dst;
		flow->l3_mask.ipv4.daddr.s_addr = mask->dst;
		flow->l3_key.ipv4.saddr.s_addr = key->src;
		flow->l3_mask.ipv4.saddr.s_addr = mask->src;
	} else if (dissector_uses_key(dissector,
				      FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_dissector_key_ipv6_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV6_ADDRS);
		struct flow_dissector_key_ipv6_addrs *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV6_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_IPV6_ADDRS;
		flow->l3_key.ipv6.daddr = key->dst;
		flow->l3_mask.ipv6.daddr = mask->dst;
		flow->l3_key.ipv6.saddr = key->src;
		flow->l3_mask.ipv6.saddr = mask->src;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_dissector_key_ports *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_PORTS);
		struct flow_dissector_key_ports *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_PORTS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_PORTS;
		flow->l4_key.ports.dport = key->dst;
		flow->l4_mask.ports.dport = mask->dst;
		flow->l4_key.ports.sport = key->src;
		flow->l4_mask.ports.sport = mask->src;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ICMP)) {
		struct flow_dissector_key_icmp *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ICMP);
		struct flow_dissector_key_icmp *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_ICMP);

		flow->flags |= BNXT_TC_FLOW_FLAGS_ICMP;
		flow->l4_key.icmp.type = key->type;
		flow->l4_key.icmp.code = key->code;
		flow->l4_mask.icmp.type = mask->type;
		flow->l4_mask.icmp.code = mask->code;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_dissector_key_control *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ENC_CONTROL);

		addr_type = key->addr_type;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		struct flow_dissector_key_ipv4_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS);
		struct flow_dissector_key_ipv4_addrs *mask =
				GET_MASK(tc_flow_cmd,
					 FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_TUNL_IPV4_ADDRS;
		flow->tun_key.u.ipv4.dst = key->dst;
		flow->tun_mask.u.ipv4.dst = mask->dst;
		flow->tun_key.u.ipv4.src = key->src;
		flow->tun_mask.u.ipv4.src = mask->src;
	} else if (dissector_uses_key(dissector,
				      FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS)) {
		return -EOPNOTSUPP;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_dissector_key_keyid *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ENC_KEYID);
		struct flow_dissector_key_keyid *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_ENC_KEYID);

		flow->flags |= BNXT_TC_FLOW_FLAGS_TUNL_ID;
		flow->tun_key.tun_id = key32_to_tunnel_id(key->keyid);
		flow->tun_mask.tun_id = key32_to_tunnel_id(mask->keyid);
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
		struct flow_dissector_key_ports *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ENC_PORTS);
		struct flow_dissector_key_ports *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_ENC_PORTS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_TUNL_PORTS;
		flow->tun_key.tp_dst = key->dst;
		flow->tun_mask.tp_dst = mask->dst;
		flow->tun_key.tp_src = key->src;
		flow->tun_mask.tp_src = mask->src;
	}

	return bnxt_tc_parse_actions(bp, &flow->actions, tc_flow_cmd->exts);
}

static int bnxt_hwrm_cfa_flow_free(struct bnxt *bp, __le16 flow_handle)
{
	struct hwrm_cfa_flow_free_input req = { 0 };
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_FLOW_FREE, -1, -1);
	req.flow_handle = flow_handle;

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_info(bp->dev, "Error: %s: flow_handle=0x%x rc=%d",
			    __func__, flow_handle, rc);
	return rc;
}

static int ipv6_mask_len(struct in6_addr *mask)
{
	int mask_len = 0, i;

	for (i = 0; i < 4; i++)
		mask_len += inet_mask_len(mask->s6_addr32[i]);

	return mask_len;
}

static bool is_wildcard(void *mask, int len)
{
	const u8 *p = mask;
	int i;

	for (i = 0; i < len; i++) {
		if (p[i] != 0)
			return false;
	}
	return true;
}

static int bnxt_hwrm_cfa_flow_alloc(struct bnxt *bp, struct bnxt_tc_flow *flow,
				    __le16 ref_flow_handle,
				    __le32 tunnel_handle, __le16 *flow_handle)
{
	struct hwrm_cfa_flow_alloc_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_tc_actions *actions = &flow->actions;
	struct bnxt_tc_l3_key *l3_mask = &flow->l3_mask;
	struct bnxt_tc_l3_key *l3_key = &flow->l3_key;
	struct hwrm_cfa_flow_alloc_input req = { 0 };
	u16 flow_flags = 0, action_flags = 0;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_FLOW_ALLOC, -1, -1);

	req.src_fid = cpu_to_le16(flow->src_fid);
	req.ref_flow_handle = ref_flow_handle;

	if (actions->flags & BNXT_TC_ACTION_FLAG_TUNNEL_DECAP ||
	    actions->flags & BNXT_TC_ACTION_FLAG_TUNNEL_ENCAP) {
		req.tunnel_handle = tunnel_handle;
		flow_flags |= CFA_FLOW_ALLOC_REQ_FLAGS_TUNNEL;
		action_flags |= CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_TUNNEL;
	}

	req.ethertype = flow->l2_key.ether_type;
	req.ip_proto = flow->l4_key.ip_proto;

	if (flow->flags & BNXT_TC_FLOW_FLAGS_ETH_ADDRS) {
		memcpy(req.dmac, flow->l2_key.dmac, ETH_ALEN);
		memcpy(req.smac, flow->l2_key.smac, ETH_ALEN);
	}

	if (flow->l2_key.num_vlans > 0) {
		flow_flags |= CFA_FLOW_ALLOC_REQ_FLAGS_NUM_VLAN_ONE;
		/* FW expects the inner_vlan_tci value to be set
		 * in outer_vlan_tci when num_vlans is 1 (which is
		 * always the case in TC.)
		 */
		req.outer_vlan_tci = flow->l2_key.inner_vlan_tci;
	}

	/* If all IP and L4 fields are wildcarded then this is an L2 flow */
	if (is_wildcard(l3_mask, sizeof(*l3_mask)) &&
	    is_wildcard(&flow->l4_mask, sizeof(flow->l4_mask))) {
		flow_flags |= CFA_FLOW_ALLOC_REQ_FLAGS_FLOWTYPE_L2;
	} else {
		flow_flags |= flow->l2_key.ether_type == htons(ETH_P_IP) ?
				CFA_FLOW_ALLOC_REQ_FLAGS_FLOWTYPE_IPV4 :
				CFA_FLOW_ALLOC_REQ_FLAGS_FLOWTYPE_IPV6;

		if (flow->flags & BNXT_TC_FLOW_FLAGS_IPV4_ADDRS) {
			req.ip_dst[0] = l3_key->ipv4.daddr.s_addr;
			req.ip_dst_mask_len =
				inet_mask_len(l3_mask->ipv4.daddr.s_addr);
			req.ip_src[0] = l3_key->ipv4.saddr.s_addr;
			req.ip_src_mask_len =
				inet_mask_len(l3_mask->ipv4.saddr.s_addr);
		} else if (flow->flags & BNXT_TC_FLOW_FLAGS_IPV6_ADDRS) {
			memcpy(req.ip_dst, l3_key->ipv6.daddr.s6_addr32,
			       sizeof(req.ip_dst));
			req.ip_dst_mask_len =
					ipv6_mask_len(&l3_mask->ipv6.daddr);
			memcpy(req.ip_src, l3_key->ipv6.saddr.s6_addr32,
			       sizeof(req.ip_src));
			req.ip_src_mask_len =
					ipv6_mask_len(&l3_mask->ipv6.saddr);
		}
	}

	if (flow->flags & BNXT_TC_FLOW_FLAGS_PORTS) {
		req.l4_src_port = flow->l4_key.ports.sport;
		req.l4_src_port_mask = flow->l4_mask.ports.sport;
		req.l4_dst_port = flow->l4_key.ports.dport;
		req.l4_dst_port_mask = flow->l4_mask.ports.dport;
	} else if (flow->flags & BNXT_TC_FLOW_FLAGS_ICMP) {
		/* l4 ports serve as type/code when ip_proto is ICMP */
		req.l4_src_port = htons(flow->l4_key.icmp.type);
		req.l4_src_port_mask = htons(flow->l4_mask.icmp.type);
		req.l4_dst_port = htons(flow->l4_key.icmp.code);
		req.l4_dst_port_mask = htons(flow->l4_mask.icmp.code);
	}
	req.flags = cpu_to_le16(flow_flags);

	if (actions->flags & BNXT_TC_ACTION_FLAG_DROP) {
		action_flags |= CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_DROP;
	} else {
		if (actions->flags & BNXT_TC_ACTION_FLAG_FWD) {
			action_flags |= CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_FWD;
			req.dst_fid = cpu_to_le16(actions->dst_fid);
		}
		if (actions->flags & BNXT_TC_ACTION_FLAG_PUSH_VLAN) {
			action_flags |=
			    CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_L2_HEADER_REWRITE;
			req.l2_rewrite_vlan_tpid = actions->push_vlan_tpid;
			req.l2_rewrite_vlan_tci = actions->push_vlan_tci;
			memcpy(&req.l2_rewrite_dmac, &req.dmac, ETH_ALEN);
			memcpy(&req.l2_rewrite_smac, &req.smac, ETH_ALEN);
		}
		if (actions->flags & BNXT_TC_ACTION_FLAG_POP_VLAN) {
			action_flags |=
			    CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_L2_HEADER_REWRITE;
			/* Rewrite config with tpid = 0 implies vlan pop */
			req.l2_rewrite_vlan_tpid = 0;
			memcpy(&req.l2_rewrite_dmac, &req.dmac, ETH_ALEN);
			memcpy(&req.l2_rewrite_smac, &req.smac, ETH_ALEN);
		}
	}
	req.action_flags = cpu_to_le16(action_flags);

	mutex_lock(&bp->hwrm_cmd_lock);

	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		*flow_handle = resp->flow_handle;

	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static int hwrm_cfa_decap_filter_alloc(struct bnxt *bp,
				       struct bnxt_tc_flow *flow,
				       struct bnxt_tc_l2_key *l2_info,
				       __le32 ref_decap_handle,
				       __le32 *decap_filter_handle)
{
	struct hwrm_cfa_decap_filter_alloc_output *resp =
						bp->hwrm_cmd_resp_addr;
	struct hwrm_cfa_decap_filter_alloc_input req = { 0 };
	struct ip_tunnel_key *tun_key = &flow->tun_key;
	u32 enables = 0;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_DECAP_FILTER_ALLOC, -1, -1);

	req.flags = cpu_to_le32(CFA_DECAP_FILTER_ALLOC_REQ_FLAGS_OVS_TUNNEL);
	enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_TUNNEL_TYPE |
		   CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_IP_PROTOCOL;
	req.tunnel_type = CFA_DECAP_FILTER_ALLOC_REQ_TUNNEL_TYPE_VXLAN;
	req.ip_protocol = CFA_DECAP_FILTER_ALLOC_REQ_IP_PROTOCOL_UDP;

	if (flow->flags & BNXT_TC_FLOW_FLAGS_TUNL_ID) {
		enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_TUNNEL_ID;
		/* tunnel_id is wrongly defined in hsi defn. as __le32 */
		req.tunnel_id = tunnel_id_to_key32(tun_key->tun_id);
	}

	if (flow->flags & BNXT_TC_FLOW_FLAGS_TUNL_ETH_ADDRS) {
		enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_DST_MACADDR;
		ether_addr_copy(req.dst_macaddr, l2_info->dmac);
	}
	if (l2_info->num_vlans) {
		enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_T_IVLAN_VID;
		req.t_ivlan_vid = l2_info->inner_vlan_tci;
	}

	enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_ETHERTYPE;
	req.ethertype = htons(ETH_P_IP);

	if (flow->flags & BNXT_TC_FLOW_FLAGS_TUNL_IPV4_ADDRS) {
		enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR |
			   CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR |
			   CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_IPADDR_TYPE;
		req.ip_addr_type = CFA_DECAP_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV4;
		req.dst_ipaddr[0] = tun_key->u.ipv4.dst;
		req.src_ipaddr[0] = tun_key->u.ipv4.src;
	}

	if (flow->flags & BNXT_TC_FLOW_FLAGS_TUNL_PORTS) {
		enables |= CFA_DECAP_FILTER_ALLOC_REQ_ENABLES_DST_PORT;
		req.dst_port = tun_key->tp_dst;
	}

	/* Eventhough the decap_handle returned by hwrm_cfa_decap_filter_alloc
	 * is defined as __le32, l2_ctxt_ref_id is defined in HSI as __le16.
	 */
	req.l2_ctxt_ref_id = (__force __le16)ref_decap_handle;
	req.enables = cpu_to_le32(enables);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		*decap_filter_handle = resp->decap_filter_id;
	else
		netdev_info(bp->dev, "%s: Error rc=%d", __func__, rc);
	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static int hwrm_cfa_decap_filter_free(struct bnxt *bp,
				      __le32 decap_filter_handle)
{
	struct hwrm_cfa_decap_filter_free_input req = { 0 };
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_DECAP_FILTER_FREE, -1, -1);
	req.decap_filter_id = decap_filter_handle;

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_info(bp->dev, "%s: Error rc=%d", __func__, rc);
	return rc;
}

static int hwrm_cfa_encap_record_alloc(struct bnxt *bp,
				       struct ip_tunnel_key *encap_key,
				       struct bnxt_tc_l2_key *l2_info,
				       __le32 *encap_record_handle)
{
	struct hwrm_cfa_encap_record_alloc_output *resp =
						bp->hwrm_cmd_resp_addr;
	struct hwrm_cfa_encap_record_alloc_input req = { 0 };
	struct hwrm_cfa_encap_data_vxlan *encap =
			(struct hwrm_cfa_encap_data_vxlan *)&req.encap_data;
	struct hwrm_vxlan_ipv4_hdr *encap_ipv4 =
				(struct hwrm_vxlan_ipv4_hdr *)encap->l3;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_ENCAP_RECORD_ALLOC, -1, -1);

	req.encap_type = CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_VXLAN;

	ether_addr_copy(encap->dst_mac_addr, l2_info->dmac);
	ether_addr_copy(encap->src_mac_addr, l2_info->smac);
	if (l2_info->num_vlans) {
		encap->num_vlan_tags = l2_info->num_vlans;
		encap->ovlan_tci = l2_info->inner_vlan_tci;
		encap->ovlan_tpid = l2_info->inner_vlan_tpid;
	}

	encap_ipv4->ver_hlen = 4 << VXLAN_IPV4_HDR_VER_HLEN_VERSION_SFT;
	encap_ipv4->ver_hlen |= 5 << VXLAN_IPV4_HDR_VER_HLEN_HEADER_LENGTH_SFT;
	encap_ipv4->ttl = encap_key->ttl;

	encap_ipv4->dest_ip_addr = encap_key->u.ipv4.dst;
	encap_ipv4->src_ip_addr = encap_key->u.ipv4.src;
	encap_ipv4->protocol = IPPROTO_UDP;

	encap->dst_port = encap_key->tp_dst;
	encap->vni = tunnel_id_to_key32(encap_key->tun_id);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		*encap_record_handle = resp->encap_record_id;
	else
		netdev_info(bp->dev, "%s: Error rc=%d", __func__, rc);
	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static int hwrm_cfa_encap_record_free(struct bnxt *bp,
				      __le32 encap_record_handle)
{
	struct hwrm_cfa_encap_record_free_input req = { 0 };
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_ENCAP_RECORD_FREE, -1, -1);
	req.encap_record_id = encap_record_handle;

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_info(bp->dev, "%s: Error rc=%d", __func__, rc);
	return rc;
}

static int bnxt_tc_put_l2_node(struct bnxt *bp,
			       struct bnxt_tc_flow_node *flow_node)
{
	struct bnxt_tc_l2_node *l2_node = flow_node->l2_node;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int rc;

	/* remove flow_node from the L2 shared flow list */
	list_del(&flow_node->l2_list_node);
	if (--l2_node->refcount == 0) {
		rc =  rhashtable_remove_fast(&tc_info->l2_table, &l2_node->node,
					     tc_info->l2_ht_params);
		if (rc)
			netdev_err(bp->dev,
				   "Error: %s: rhashtable_remove_fast: %d",
				   __func__, rc);
		kfree_rcu(l2_node, rcu);
	}
	return 0;
}

static struct bnxt_tc_l2_node *
bnxt_tc_get_l2_node(struct bnxt *bp, struct rhashtable *l2_table,
		    struct rhashtable_params ht_params,
		    struct bnxt_tc_l2_key *l2_key)
{
	struct bnxt_tc_l2_node *l2_node;
	int rc;

	l2_node = rhashtable_lookup_fast(l2_table, l2_key, ht_params);
	if (!l2_node) {
		l2_node = kzalloc(sizeof(*l2_node), GFP_KERNEL);
		if (!l2_node) {
			rc = -ENOMEM;
			return NULL;
		}

		l2_node->key = *l2_key;
		rc = rhashtable_insert_fast(l2_table, &l2_node->node,
					    ht_params);
		if (rc) {
			kfree_rcu(l2_node, rcu);
			netdev_err(bp->dev,
				   "Error: %s: rhashtable_insert_fast: %d",
				   __func__, rc);
			return NULL;
		}
		INIT_LIST_HEAD(&l2_node->common_l2_flows);
	}
	return l2_node;
}

/* Get the ref_flow_handle for a flow by checking if there are any other
 * flows that share the same L2 key as this flow.
 */
static int
bnxt_tc_get_ref_flow_handle(struct bnxt *bp, struct bnxt_tc_flow *flow,
			    struct bnxt_tc_flow_node *flow_node,
			    __le16 *ref_flow_handle)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_flow_node *ref_flow_node;
	struct bnxt_tc_l2_node *l2_node;

	l2_node = bnxt_tc_get_l2_node(bp, &tc_info->l2_table,
				      tc_info->l2_ht_params,
				      &flow->l2_key);
	if (!l2_node)
		return -1;

	/* If any other flow is using this l2_node, use it's flow_handle
	 * as the ref_flow_handle
	 */
	if (l2_node->refcount > 0) {
		ref_flow_node = list_first_entry(&l2_node->common_l2_flows,
						 struct bnxt_tc_flow_node,
						 l2_list_node);
		*ref_flow_handle = ref_flow_node->flow_handle;
	} else {
		*ref_flow_handle = cpu_to_le16(0xffff);
	}

	/* Insert the l2_node into the flow_node so that subsequent flows
	 * with a matching l2 key can use the flow_handle of this flow
	 * as their ref_flow_handle
	 */
	flow_node->l2_node = l2_node;
	list_add(&flow_node->l2_list_node, &l2_node->common_l2_flows);
	l2_node->refcount++;
	return 0;
}

/* After the flow parsing is done, this routine is used for checking
 * if there are any aspects of the flow that prevent it from being
 * offloaded.
 */
static bool bnxt_tc_can_offload(struct bnxt *bp, struct bnxt_tc_flow *flow)
{
	/* If L4 ports are specified then ip_proto must be TCP or UDP */
	if ((flow->flags & BNXT_TC_FLOW_FLAGS_PORTS) &&
	    (flow->l4_key.ip_proto != IPPROTO_TCP &&
	     flow->l4_key.ip_proto != IPPROTO_UDP)) {
		netdev_info(bp->dev, "Cannot offload non-TCP/UDP (%d) ports",
			    flow->l4_key.ip_proto);
		return false;
	}

	return true;
}

/* Returns the final refcount of the node on success
 * or a -ve error code on failure
 */
static int bnxt_tc_put_tunnel_node(struct bnxt *bp,
				   struct rhashtable *tunnel_table,
				   struct rhashtable_params *ht_params,
				   struct bnxt_tc_tunnel_node *tunnel_node)
{
	int rc;

	if (--tunnel_node->refcount == 0) {
		rc =  rhashtable_remove_fast(tunnel_table, &tunnel_node->node,
					     *ht_params);
		if (rc) {
			netdev_err(bp->dev, "rhashtable_remove_fast rc=%d", rc);
			rc = -1;
		}
		kfree_rcu(tunnel_node, rcu);
		return rc;
	} else {
		return tunnel_node->refcount;
	}
}

/* Get (or add) either encap or decap tunnel node from/to the supplied
 * hash table.
 */
static struct bnxt_tc_tunnel_node *
bnxt_tc_get_tunnel_node(struct bnxt *bp, struct rhashtable *tunnel_table,
			struct rhashtable_params *ht_params,
			struct ip_tunnel_key *tun_key)
{
	struct bnxt_tc_tunnel_node *tunnel_node;
	int rc;

	tunnel_node = rhashtable_lookup_fast(tunnel_table, tun_key, *ht_params);
	if (!tunnel_node) {
		tunnel_node = kzalloc(sizeof(*tunnel_node), GFP_KERNEL);
		if (!tunnel_node) {
			rc = -ENOMEM;
			goto err;
		}

		tunnel_node->key = *tun_key;
		tunnel_node->tunnel_handle = INVALID_TUNNEL_HANDLE;
		rc = rhashtable_insert_fast(tunnel_table, &tunnel_node->node,
					    *ht_params);
		if (rc) {
			kfree_rcu(tunnel_node, rcu);
			goto err;
		}
	}
	tunnel_node->refcount++;
	return tunnel_node;
err:
	netdev_info(bp->dev, "error rc=%d", rc);
	return NULL;
}

static int bnxt_tc_get_ref_decap_handle(struct bnxt *bp,
					struct bnxt_tc_flow *flow,
					struct bnxt_tc_l2_key *l2_key,
					struct bnxt_tc_flow_node *flow_node,
					__le32 *ref_decap_handle)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_flow_node *ref_flow_node;
	struct bnxt_tc_l2_node *decap_l2_node;

	decap_l2_node = bnxt_tc_get_l2_node(bp, &tc_info->decap_l2_table,
					    tc_info->decap_l2_ht_params,
					    l2_key);
	if (!decap_l2_node)
		return -1;

	/* If any other flow is using this decap_l2_node, use it's decap_handle
	 * as the ref_decap_handle
	 */
	if (decap_l2_node->refcount > 0) {
		ref_flow_node =
			list_first_entry(&decap_l2_node->common_l2_flows,
					 struct bnxt_tc_flow_node,
					 decap_l2_list_node);
		*ref_decap_handle = ref_flow_node->decap_node->tunnel_handle;
	} else {
		*ref_decap_handle = INVALID_TUNNEL_HANDLE;
	}

	/* Insert the l2_node into the flow_node so that subsequent flows
	 * with a matching decap l2 key can use the decap_filter_handle of
	 * this flow as their ref_decap_handle
	 */
	flow_node->decap_l2_node = decap_l2_node;
	list_add(&flow_node->decap_l2_list_node,
		 &decap_l2_node->common_l2_flows);
	decap_l2_node->refcount++;
	return 0;
}

static void bnxt_tc_put_decap_l2_node(struct bnxt *bp,
				      struct bnxt_tc_flow_node *flow_node)
{
	struct bnxt_tc_l2_node *decap_l2_node = flow_node->decap_l2_node;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int rc;

	/* remove flow_node from the decap L2 sharing flow list */
	list_del(&flow_node->decap_l2_list_node);
	if (--decap_l2_node->refcount == 0) {
		rc =  rhashtable_remove_fast(&tc_info->decap_l2_table,
					     &decap_l2_node->node,
					     tc_info->decap_l2_ht_params);
		if (rc)
			netdev_err(bp->dev, "rhashtable_remove_fast rc=%d", rc);
		kfree_rcu(decap_l2_node, rcu);
	}
}

static void bnxt_tc_put_decap_handle(struct bnxt *bp,
				     struct bnxt_tc_flow_node *flow_node)
{
	__le32 decap_handle = flow_node->decap_node->tunnel_handle;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int rc;

	if (flow_node->decap_l2_node)
		bnxt_tc_put_decap_l2_node(bp, flow_node);

	rc = bnxt_tc_put_tunnel_node(bp, &tc_info->decap_table,
				     &tc_info->decap_ht_params,
				     flow_node->decap_node);
	if (!rc && decap_handle != INVALID_TUNNEL_HANDLE)
		hwrm_cfa_decap_filter_free(bp, decap_handle);
}

static int bnxt_tc_resolve_tunnel_hdrs(struct bnxt *bp,
				       struct ip_tunnel_key *tun_key,
				       struct bnxt_tc_l2_key *l2_info)
{
#ifdef CONFIG_INET
	struct net_device *real_dst_dev = bp->dev;
	struct flowi4 flow = { {0} };
	struct net_device *dst_dev;
	struct neighbour *nbr;
	struct rtable *rt;
	int rc;

	flow.flowi4_proto = IPPROTO_UDP;
	flow.fl4_dport = tun_key->tp_dst;
	flow.daddr = tun_key->u.ipv4.dst;

	rt = ip_route_output_key(dev_net(real_dst_dev), &flow);
	if (IS_ERR(rt)) {
		netdev_info(bp->dev, "no route to %pI4b", &flow.daddr);
		return -EOPNOTSUPP;
	}

	/* The route must either point to the real_dst_dev or a dst_dev that
	 * uses the real_dst_dev.
	 */
	dst_dev = rt->dst.dev;
	if (is_vlan_dev(dst_dev)) {
#if IS_ENABLED(CONFIG_VLAN_8021Q)
		struct vlan_dev_priv *vlan = vlan_dev_priv(dst_dev);

		if (vlan->real_dev != real_dst_dev) {
			netdev_info(bp->dev,
				    "dst_dev(%s) doesn't use PF-if(%s)",
				    netdev_name(dst_dev),
				    netdev_name(real_dst_dev));
			rc = -EOPNOTSUPP;
			goto put_rt;
		}
		l2_info->inner_vlan_tci = htons(vlan->vlan_id);
		l2_info->inner_vlan_tpid = vlan->vlan_proto;
		l2_info->num_vlans = 1;
#endif
	} else if (dst_dev != real_dst_dev) {
		netdev_info(bp->dev,
			    "dst_dev(%s) for %pI4b is not PF-if(%s)",
			    netdev_name(dst_dev), &flow.daddr,
			    netdev_name(real_dst_dev));
		rc = -EOPNOTSUPP;
		goto put_rt;
	}

	nbr = dst_neigh_lookup(&rt->dst, &flow.daddr);
	if (!nbr) {
		netdev_info(bp->dev, "can't lookup neighbor for %pI4b",
			    &flow.daddr);
		rc = -EOPNOTSUPP;
		goto put_rt;
	}

	tun_key->u.ipv4.src = flow.saddr;
	tun_key->ttl = ip4_dst_hoplimit(&rt->dst);
	neigh_ha_snapshot(l2_info->dmac, nbr, dst_dev);
	ether_addr_copy(l2_info->smac, dst_dev->dev_addr);
	neigh_release(nbr);
	ip_rt_put(rt);

	return 0;
put_rt:
	ip_rt_put(rt);
	return rc;
#else
	return -EOPNOTSUPP;
#endif
}

static int bnxt_tc_get_decap_handle(struct bnxt *bp, struct bnxt_tc_flow *flow,
				    struct bnxt_tc_flow_node *flow_node,
				    __le32 *decap_filter_handle)
{
	struct ip_tunnel_key *decap_key = &flow->tun_key;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_l2_key l2_info = { {0} };
	struct bnxt_tc_tunnel_node *decap_node;
	struct ip_tunnel_key tun_key = { 0 };
	struct bnxt_tc_l2_key *decap_l2_info;
	__le32 ref_decap_handle;
	int rc;

	/* Check if there's another flow using the same tunnel decap.
	 * If not, add this tunnel to the table and resolve the other
	 * tunnel header fileds
	 */
	decap_node = bnxt_tc_get_tunnel_node(bp, &tc_info->decap_table,
					     &tc_info->decap_ht_params,
					     decap_key);
	if (!decap_node)
		return -ENOMEM;

	flow_node->decap_node = decap_node;

	if (decap_node->tunnel_handle != INVALID_TUNNEL_HANDLE)
		goto done;

	/* Resolve the L2 fields for tunnel decap
	 * Resolve the route for remote vtep (saddr) of the decap key
	 * Find it's next-hop mac addrs
	 */
	tun_key.u.ipv4.dst = flow->tun_key.u.ipv4.src;
	tun_key.tp_dst = flow->tun_key.tp_dst;
	rc = bnxt_tc_resolve_tunnel_hdrs(bp, &tun_key, &l2_info);
	if (rc)
		goto put_decap;

	decap_l2_info = &decap_node->l2_info;
	/* decap smac is wildcarded */
	ether_addr_copy(decap_l2_info->dmac, l2_info.smac);
	if (l2_info.num_vlans) {
		decap_l2_info->num_vlans = l2_info.num_vlans;
		decap_l2_info->inner_vlan_tpid = l2_info.inner_vlan_tpid;
		decap_l2_info->inner_vlan_tci = l2_info.inner_vlan_tci;
	}
	flow->flags |= BNXT_TC_FLOW_FLAGS_TUNL_ETH_ADDRS;

	/* For getting a decap_filter_handle we first need to check if
	 * there are any other decap flows that share the same tunnel L2
	 * key and if so, pass that flow's decap_filter_handle as the
	 * ref_decap_handle for this flow.
	 */
	rc = bnxt_tc_get_ref_decap_handle(bp, flow, decap_l2_info, flow_node,
					  &ref_decap_handle);
	if (rc)
		goto put_decap;

	/* Issue the hwrm cmd to allocate a decap filter handle */
	rc = hwrm_cfa_decap_filter_alloc(bp, flow, decap_l2_info,
					 ref_decap_handle,
					 &decap_node->tunnel_handle);
	if (rc)
		goto put_decap_l2;

done:
	*decap_filter_handle = decap_node->tunnel_handle;
	return 0;

put_decap_l2:
	bnxt_tc_put_decap_l2_node(bp, flow_node);
put_decap:
	bnxt_tc_put_tunnel_node(bp, &tc_info->decap_table,
				&tc_info->decap_ht_params,
				flow_node->decap_node);
	return rc;
}

static void bnxt_tc_put_encap_handle(struct bnxt *bp,
				     struct bnxt_tc_tunnel_node *encap_node)
{
	__le32 encap_handle = encap_node->tunnel_handle;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int rc;

	rc = bnxt_tc_put_tunnel_node(bp, &tc_info->encap_table,
				     &tc_info->encap_ht_params, encap_node);
	if (!rc && encap_handle != INVALID_TUNNEL_HANDLE)
		hwrm_cfa_encap_record_free(bp, encap_handle);
}

/* Lookup the tunnel encap table and check if there's an encap_handle
 * alloc'd already.
 * If not, query L2 info via a route lookup and issue an encap_record_alloc
 * cmd to FW.
 */
static int bnxt_tc_get_encap_handle(struct bnxt *bp, struct bnxt_tc_flow *flow,
				    struct bnxt_tc_flow_node *flow_node,
				    __le32 *encap_handle)
{
	struct ip_tunnel_key *encap_key = &flow->actions.tun_encap_key;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_tunnel_node *encap_node;
	int rc;

	/* Check if there's another flow using the same tunnel encap.
	 * If not, add this tunnel to the table and resolve the other
	 * tunnel header fileds
	 */
	encap_node = bnxt_tc_get_tunnel_node(bp, &tc_info->encap_table,
					     &tc_info->encap_ht_params,
					     encap_key);
	if (!encap_node)
		return -ENOMEM;

	flow_node->encap_node = encap_node;

	if (encap_node->tunnel_handle != INVALID_TUNNEL_HANDLE)
		goto done;

	rc = bnxt_tc_resolve_tunnel_hdrs(bp, encap_key, &encap_node->l2_info);
	if (rc)
		goto put_encap;

	/* Allocate a new tunnel encap record */
	rc = hwrm_cfa_encap_record_alloc(bp, encap_key, &encap_node->l2_info,
					 &encap_node->tunnel_handle);
	if (rc)
		goto put_encap;

done:
	*encap_handle = encap_node->tunnel_handle;
	return 0;

put_encap:
	bnxt_tc_put_tunnel_node(bp, &tc_info->encap_table,
				&tc_info->encap_ht_params, encap_node);
	return rc;
}

static void bnxt_tc_put_tunnel_handle(struct bnxt *bp,
				      struct bnxt_tc_flow *flow,
				      struct bnxt_tc_flow_node *flow_node)
{
	if (flow->actions.flags & BNXT_TC_ACTION_FLAG_TUNNEL_DECAP)
		bnxt_tc_put_decap_handle(bp, flow_node);
	else if (flow->actions.flags & BNXT_TC_ACTION_FLAG_TUNNEL_ENCAP)
		bnxt_tc_put_encap_handle(bp, flow_node->encap_node);
}

static int bnxt_tc_get_tunnel_handle(struct bnxt *bp,
				     struct bnxt_tc_flow *flow,
				     struct bnxt_tc_flow_node *flow_node,
				     __le32 *tunnel_handle)
{
	if (flow->actions.flags & BNXT_TC_ACTION_FLAG_TUNNEL_DECAP)
		return bnxt_tc_get_decap_handle(bp, flow, flow_node,
						tunnel_handle);
	else if (flow->actions.flags & BNXT_TC_ACTION_FLAG_TUNNEL_ENCAP)
		return bnxt_tc_get_encap_handle(bp, flow, flow_node,
						tunnel_handle);
	else
		return 0;
}
static int __bnxt_tc_del_flow(struct bnxt *bp,
			      struct bnxt_tc_flow_node *flow_node)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int rc;

	/* send HWRM cmd to free the flow-id */
	bnxt_hwrm_cfa_flow_free(bp, flow_node->flow_handle);

	mutex_lock(&tc_info->lock);

	/* release references to any tunnel encap/decap nodes */
	bnxt_tc_put_tunnel_handle(bp, &flow_node->flow, flow_node);

	/* release reference to l2 node */
	bnxt_tc_put_l2_node(bp, flow_node);

	mutex_unlock(&tc_info->lock);

	rc = rhashtable_remove_fast(&tc_info->flow_table, &flow_node->node,
				    tc_info->flow_ht_params);
	if (rc)
		netdev_err(bp->dev, "Error: %s: rhashtable_remove_fast rc=%d",
			   __func__, rc);

	kfree_rcu(flow_node, rcu);
	return 0;
}

static void bnxt_tc_set_src_fid(struct bnxt *bp, struct bnxt_tc_flow *flow,
				u16 src_fid)
{
	if (flow->actions.flags & BNXT_TC_ACTION_FLAG_TUNNEL_DECAP)
		flow->src_fid = bp->pf.fw_fid;
	else
		flow->src_fid = src_fid;
}

/* Add a new flow or replace an existing flow.
 * Notes on locking:
 * There are essentially two critical sections here.
 * 1. while adding a new flow
 *    a) lookup l2-key
 *    b) issue HWRM cmd and get flow_handle
 *    c) link l2-key with flow
 * 2. while deleting a flow
 *    a) unlinking l2-key from flow
 * A lock is needed to protect these two critical sections.
 *
 * The hash-tables are already protected by the rhashtable API.
 */
static int bnxt_tc_add_flow(struct bnxt *bp, u16 src_fid,
			    struct tc_cls_flower_offload *tc_flow_cmd)
{
	struct bnxt_tc_flow_node *new_node, *old_node;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_flow *flow;
	__le32 tunnel_handle = 0;
	__le16 ref_flow_handle;
	int rc;

	/* allocate memory for the new flow and it's node */
	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node) {
		rc = -ENOMEM;
		goto done;
	}
	new_node->cookie = tc_flow_cmd->cookie;
	flow = &new_node->flow;

	rc = bnxt_tc_parse_flow(bp, tc_flow_cmd, flow);
	if (rc)
		goto free_node;

	bnxt_tc_set_src_fid(bp, flow, src_fid);

	if (!bnxt_tc_can_offload(bp, flow)) {
		rc = -ENOSPC;
		goto free_node;
	}

	/* If a flow exists with the same cookie, delete it */
	old_node = rhashtable_lookup_fast(&tc_info->flow_table,
					  &tc_flow_cmd->cookie,
					  tc_info->flow_ht_params);
	if (old_node)
		__bnxt_tc_del_flow(bp, old_node);

	/* Check if the L2 part of the flow has been offloaded already.
	 * If so, bump up it's refcnt and get it's reference handle.
	 */
	mutex_lock(&tc_info->lock);
	rc = bnxt_tc_get_ref_flow_handle(bp, flow, new_node, &ref_flow_handle);
	if (rc)
		goto unlock;

	/* If the flow involves tunnel encap/decap, get tunnel_handle */
	rc = bnxt_tc_get_tunnel_handle(bp, flow, new_node, &tunnel_handle);
	if (rc)
		goto put_l2;

	/* send HWRM cmd to alloc the flow */
	rc = bnxt_hwrm_cfa_flow_alloc(bp, flow, ref_flow_handle,
				      tunnel_handle, &new_node->flow_handle);
	if (rc)
		goto put_tunnel;

	flow->lastused = jiffies;
	spin_lock_init(&flow->stats_lock);
	/* add new flow to flow-table */
	rc = rhashtable_insert_fast(&tc_info->flow_table, &new_node->node,
				    tc_info->flow_ht_params);
	if (rc)
		goto hwrm_flow_free;

	mutex_unlock(&tc_info->lock);
	return 0;

hwrm_flow_free:
	bnxt_hwrm_cfa_flow_free(bp, new_node->flow_handle);
put_tunnel:
	bnxt_tc_put_tunnel_handle(bp, flow, new_node);
put_l2:
	bnxt_tc_put_l2_node(bp, new_node);
unlock:
	mutex_unlock(&tc_info->lock);
free_node:
	kfree_rcu(new_node, rcu);
done:
	netdev_err(bp->dev, "Error: %s: cookie=0x%lx error=%d",
		   __func__, tc_flow_cmd->cookie, rc);
	return rc;
}

static int bnxt_tc_del_flow(struct bnxt *bp,
			    struct tc_cls_flower_offload *tc_flow_cmd)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_flow_node *flow_node;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_info(bp->dev, "ERROR: no flow_node for cookie %lx",
			    tc_flow_cmd->cookie);
		return -EINVAL;
	}

	return __bnxt_tc_del_flow(bp, flow_node);
}

static int bnxt_tc_get_flow_stats(struct bnxt *bp,
				  struct tc_cls_flower_offload *tc_flow_cmd)
{
	struct bnxt_tc_flow_stats stats, *curr_stats, *prev_stats;
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct bnxt_tc_flow_node *flow_node;
	struct bnxt_tc_flow *flow;
	unsigned long lastused;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_info(bp->dev, "Error: no flow_node for cookie %lx",
			    tc_flow_cmd->cookie);
		return -1;
	}

	flow = &flow_node->flow;
	curr_stats = &flow->stats;
	prev_stats = &flow->prev_stats;

	spin_lock(&flow->stats_lock);
	stats.packets = curr_stats->packets - prev_stats->packets;
	stats.bytes = curr_stats->bytes - prev_stats->bytes;
	*prev_stats = *curr_stats;
	lastused = flow->lastused;
	spin_unlock(&flow->stats_lock);

	tcf_exts_stats_update(tc_flow_cmd->exts, stats.bytes, stats.packets,
			      lastused);
	return 0;
}

static int
bnxt_hwrm_cfa_flow_stats_get(struct bnxt *bp, int num_flows,
			     struct bnxt_tc_stats_batch stats_batch[])
{
	struct hwrm_cfa_flow_stats_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_cfa_flow_stats_input req = { 0 };
	__le16 *req_flow_handles = &req.flow_handle_0;
	int rc, i;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_FLOW_STATS, -1, -1);
	req.num_flows = cpu_to_le16(num_flows);
	for (i = 0; i < num_flows; i++) {
		struct bnxt_tc_flow_node *flow_node = stats_batch[i].flow_node;

		req_flow_handles[i] = flow_node->flow_handle;
	}

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		__le64 *resp_packets = &resp->packet_0;
		__le64 *resp_bytes = &resp->byte_0;

		for (i = 0; i < num_flows; i++) {
			stats_batch[i].hw_stats.packets =
						le64_to_cpu(resp_packets[i]);
			stats_batch[i].hw_stats.bytes =
						le64_to_cpu(resp_bytes[i]);
		}
	} else {
		netdev_info(bp->dev, "error rc=%d", rc);
	}

	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

/* Add val to accum while handling a possible wraparound
 * of val. Eventhough val is of type u64, its actual width
 * is denoted by mask and will wrap-around beyond that width.
 */
static void accumulate_val(u64 *accum, u64 val, u64 mask)
{
#define low_bits(x, mask)		((x) & (mask))
#define high_bits(x, mask)		((x) & ~(mask))
	bool wrapped = val < low_bits(*accum, mask);

	*accum = high_bits(*accum, mask) + val;
	if (wrapped)
		*accum += (mask + 1);
}

/* The HW counters' width is much less than 64bits.
 * Handle possible wrap-around while updating the stat counters
 */
static void bnxt_flow_stats_accum(struct bnxt_tc_info *tc_info,
				  struct bnxt_tc_flow_stats *acc_stats,
				  struct bnxt_tc_flow_stats *hw_stats)
{
	accumulate_val(&acc_stats->bytes, hw_stats->bytes, tc_info->bytes_mask);
	accumulate_val(&acc_stats->packets, hw_stats->packets,
		       tc_info->packets_mask);
}

static int
bnxt_tc_flow_stats_batch_update(struct bnxt *bp, int num_flows,
				struct bnxt_tc_stats_batch stats_batch[])
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int rc, i;

	rc = bnxt_hwrm_cfa_flow_stats_get(bp, num_flows, stats_batch);
	if (rc)
		return rc;

	for (i = 0; i < num_flows; i++) {
		struct bnxt_tc_flow_node *flow_node = stats_batch[i].flow_node;
		struct bnxt_tc_flow *flow = &flow_node->flow;

		spin_lock(&flow->stats_lock);
		bnxt_flow_stats_accum(tc_info, &flow->stats,
				      &stats_batch[i].hw_stats);
		if (flow->stats.packets != flow->prev_stats.packets)
			flow->lastused = jiffies;
		spin_unlock(&flow->stats_lock);
	}

	return 0;
}

static int
bnxt_tc_flow_stats_batch_prep(struct bnxt *bp,
			      struct bnxt_tc_stats_batch stats_batch[],
			      int *num_flows)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	struct rhashtable_iter *iter = &tc_info->iter;
	void *flow_node;
	int rc, i;

	rhashtable_walk_start(iter);

	rc = 0;
	for (i = 0; i < BNXT_FLOW_STATS_BATCH_MAX; i++) {
		flow_node = rhashtable_walk_next(iter);
		if (IS_ERR(flow_node)) {
			i = 0;
			if (PTR_ERR(flow_node) == -EAGAIN) {
				continue;
			} else {
				rc = PTR_ERR(flow_node);
				goto done;
			}
		}

		/* No more flows */
		if (!flow_node)
			goto done;

		stats_batch[i].flow_node = flow_node;
	}
done:
	rhashtable_walk_stop(iter);
	*num_flows = i;
	return rc;
}

void bnxt_tc_flow_stats_work(struct bnxt *bp)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;
	int num_flows, rc;

	num_flows = atomic_read(&tc_info->flow_table.nelems);
	if (!num_flows)
		return;

	rhashtable_walk_enter(&tc_info->flow_table, &tc_info->iter);

	for (;;) {
		rc = bnxt_tc_flow_stats_batch_prep(bp, tc_info->stats_batch,
						   &num_flows);
		if (rc) {
			if (rc == -EAGAIN)
				continue;
			break;
		}

		if (!num_flows)
			break;

		bnxt_tc_flow_stats_batch_update(bp, num_flows,
						tc_info->stats_batch);
	}

	rhashtable_walk_exit(&tc_info->iter);
}

int bnxt_tc_setup_flower(struct bnxt *bp, u16 src_fid,
			 struct tc_cls_flower_offload *cls_flower)
{
	int rc = 0;

	switch (cls_flower->command) {
	case TC_CLSFLOWER_REPLACE:
		rc = bnxt_tc_add_flow(bp, src_fid, cls_flower);
		break;

	case TC_CLSFLOWER_DESTROY:
		rc = bnxt_tc_del_flow(bp, cls_flower);
		break;

	case TC_CLSFLOWER_STATS:
		rc = bnxt_tc_get_flow_stats(bp, cls_flower);
		break;
	}
	return rc;
}

static const struct rhashtable_params bnxt_tc_flow_ht_params = {
	.head_offset = offsetof(struct bnxt_tc_flow_node, node),
	.key_offset = offsetof(struct bnxt_tc_flow_node, cookie),
	.key_len = sizeof(((struct bnxt_tc_flow_node *)0)->cookie),
	.automatic_shrinking = true
};

static const struct rhashtable_params bnxt_tc_l2_ht_params = {
	.head_offset = offsetof(struct bnxt_tc_l2_node, node),
	.key_offset = offsetof(struct bnxt_tc_l2_node, key),
	.key_len = BNXT_TC_L2_KEY_LEN,
	.automatic_shrinking = true
};

static const struct rhashtable_params bnxt_tc_decap_l2_ht_params = {
	.head_offset = offsetof(struct bnxt_tc_l2_node, node),
	.key_offset = offsetof(struct bnxt_tc_l2_node, key),
	.key_len = BNXT_TC_L2_KEY_LEN,
	.automatic_shrinking = true
};

static const struct rhashtable_params bnxt_tc_tunnel_ht_params = {
	.head_offset = offsetof(struct bnxt_tc_tunnel_node, node),
	.key_offset = offsetof(struct bnxt_tc_tunnel_node, key),
	.key_len = sizeof(struct ip_tunnel_key),
	.automatic_shrinking = true
};

/* convert counter width in bits to a mask */
#define mask(width)		((u64)~0 >> (64 - (width)))

int bnxt_init_tc(struct bnxt *bp)
{
	struct bnxt_tc_info *tc_info;
	int rc;

	if (bp->hwrm_spec_code < 0x10803) {
		netdev_warn(bp->dev,
			    "Firmware does not support TC flower offload.\n");
		return -ENOTSUPP;
	}

	tc_info = kzalloc(sizeof(*tc_info), GFP_KERNEL);
	if (!tc_info)
		return -ENOMEM;
	mutex_init(&tc_info->lock);

	/* Counter widths are programmed by FW */
	tc_info->bytes_mask = mask(36);
	tc_info->packets_mask = mask(28);

	tc_info->flow_ht_params = bnxt_tc_flow_ht_params;
	rc = rhashtable_init(&tc_info->flow_table, &tc_info->flow_ht_params);
	if (rc)
		goto free_tc_info;

	tc_info->l2_ht_params = bnxt_tc_l2_ht_params;
	rc = rhashtable_init(&tc_info->l2_table, &tc_info->l2_ht_params);
	if (rc)
		goto destroy_flow_table;

	tc_info->decap_l2_ht_params = bnxt_tc_decap_l2_ht_params;
	rc = rhashtable_init(&tc_info->decap_l2_table,
			     &tc_info->decap_l2_ht_params);
	if (rc)
		goto destroy_l2_table;

	tc_info->decap_ht_params = bnxt_tc_tunnel_ht_params;
	rc = rhashtable_init(&tc_info->decap_table,
			     &tc_info->decap_ht_params);
	if (rc)
		goto destroy_decap_l2_table;

	tc_info->encap_ht_params = bnxt_tc_tunnel_ht_params;
	rc = rhashtable_init(&tc_info->encap_table,
			     &tc_info->encap_ht_params);
	if (rc)
		goto destroy_decap_table;

	tc_info->enabled = true;
	bp->dev->hw_features |= NETIF_F_HW_TC;
	bp->dev->features |= NETIF_F_HW_TC;
	bp->tc_info = tc_info;
	return 0;

destroy_decap_table:
	rhashtable_destroy(&tc_info->decap_table);
destroy_decap_l2_table:
	rhashtable_destroy(&tc_info->decap_l2_table);
destroy_l2_table:
	rhashtable_destroy(&tc_info->l2_table);
destroy_flow_table:
	rhashtable_destroy(&tc_info->flow_table);
free_tc_info:
	kfree(tc_info);
	return rc;
}

void bnxt_shutdown_tc(struct bnxt *bp)
{
	struct bnxt_tc_info *tc_info = bp->tc_info;

	if (!bnxt_tc_flower_enabled(bp))
		return;

	rhashtable_destroy(&tc_info->flow_table);
	rhashtable_destroy(&tc_info->l2_table);
	rhashtable_destroy(&tc_info->decap_l2_table);
	rhashtable_destroy(&tc_info->decap_table);
	rhashtable_destroy(&tc_info->encap_table);
	kfree(tc_info);
	bp->tc_info = NULL;
}
