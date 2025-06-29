/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_TC_LIB_H_
#define _ICE_TC_LIB_H_

#include <linux/bits.h>
#include <net/pfcp.h>

#define ICE_TC_FLWR_FIELD_DST_MAC		BIT(0)
#define ICE_TC_FLWR_FIELD_SRC_MAC		BIT(1)
#define ICE_TC_FLWR_FIELD_VLAN			BIT(2)
#define ICE_TC_FLWR_FIELD_DEST_IPV4		BIT(3)
#define ICE_TC_FLWR_FIELD_SRC_IPV4		BIT(4)
#define ICE_TC_FLWR_FIELD_DEST_IPV6		BIT(5)
#define ICE_TC_FLWR_FIELD_SRC_IPV6		BIT(6)
#define ICE_TC_FLWR_FIELD_DEST_L4_PORT		BIT(7)
#define ICE_TC_FLWR_FIELD_SRC_L4_PORT		BIT(8)
#define ICE_TC_FLWR_FIELD_TENANT_ID		BIT(9)
#define ICE_TC_FLWR_FIELD_ENC_DEST_IPV4		BIT(10)
#define ICE_TC_FLWR_FIELD_ENC_SRC_IPV4		BIT(11)
#define ICE_TC_FLWR_FIELD_ENC_DEST_IPV6		BIT(12)
#define ICE_TC_FLWR_FIELD_ENC_SRC_IPV6		BIT(13)
#define ICE_TC_FLWR_FIELD_ENC_DEST_L4_PORT	BIT(14)
#define ICE_TC_FLWR_FIELD_ENC_SRC_L4_PORT	BIT(15)
#define ICE_TC_FLWR_FIELD_ENC_DST_MAC		BIT(16)
#define ICE_TC_FLWR_FIELD_ETH_TYPE_ID		BIT(17)
#define ICE_TC_FLWR_FIELD_GTP_OPTS		BIT(18)
#define ICE_TC_FLWR_FIELD_CVLAN			BIT(19)
#define ICE_TC_FLWR_FIELD_PPPOE_SESSID		BIT(20)
#define ICE_TC_FLWR_FIELD_PPP_PROTO		BIT(21)
#define ICE_TC_FLWR_FIELD_IP_TOS		BIT(22)
#define ICE_TC_FLWR_FIELD_IP_TTL		BIT(23)
#define ICE_TC_FLWR_FIELD_ENC_IP_TOS		BIT(24)
#define ICE_TC_FLWR_FIELD_ENC_IP_TTL		BIT(25)
#define ICE_TC_FLWR_FIELD_L2TPV3_SESSID		BIT(26)
#define ICE_TC_FLWR_FIELD_VLAN_PRIO		BIT(27)
#define ICE_TC_FLWR_FIELD_CVLAN_PRIO		BIT(28)
#define ICE_TC_FLWR_FIELD_VLAN_TPID		BIT(29)
#define ICE_TC_FLWR_FIELD_PFCP_OPTS		BIT(30)

#define ICE_TC_FLOWER_MASK_32   0xFFFFFFFF

#define ICE_IPV6_HDR_TC_MASK 0xFF00000

struct ice_indr_block_priv {
	struct net_device *netdev;
	struct ice_netdev_priv *np;
	struct list_head list;
};

struct ice_tc_flower_action {
	/* forward action specific params */
	union {
		struct {
			u32 tc_class; /* forward to hw_tc */
			u32 rsvd;
		} tc;
		struct {
			u16 queue; /* forward to queue */
			/* To add filter in HW, absolute queue number in global
			 * space of queues (between 0...N) is needed
			 */
			u16 hw_queue;
		} q;
	} fwd;
	enum ice_sw_fwd_act_type fltr_act;
};

struct ice_tc_vlan_hdr {
	__be16 vlan_id; /* Only last 12 bits valid */
	__be16 vlan_prio; /* Only last 3 bits valid (valid values: 0..7) */
	__be16 vlan_tpid;
};

struct ice_tc_pppoe_hdr {
	__be16 session_id;
	__be16 ppp_proto;
};

struct ice_tc_l2_hdr {
	u8 dst_mac[ETH_ALEN];
	u8 src_mac[ETH_ALEN];
	__be16 n_proto;    /* Ethernet Protocol */
};

struct ice_tc_l3_hdr {
	u8 ip_proto;    /* IPPROTO value */
	union {
		struct {
			struct in_addr dst_ip;
			struct in_addr src_ip;
		} v4;
		struct {
			struct in6_addr dst_ip6;
			struct in6_addr src_ip6;
		} v6;
	} ip;
#define dst_ipv6	ip.v6.dst_ip6.s6_addr32
#define dst_ipv6_addr	ip.v6.dst_ip6.s6_addr
#define src_ipv6	ip.v6.src_ip6.s6_addr32
#define src_ipv6_addr	ip.v6.src_ip6.s6_addr
#define dst_ipv4	ip.v4.dst_ip.s_addr
#define src_ipv4	ip.v4.src_ip.s_addr

	u8 tos;
	u8 ttl;
};

struct ice_tc_l2tpv3_hdr {
	__be32 session_id;
};

struct ice_tc_l4_hdr {
	__be16 dst_port;
	__be16 src_port;
};

struct ice_tc_flower_lyr_2_4_hdrs {
	/* L2 layer fields with their mask */
	struct ice_tc_l2_hdr l2_key;
	struct ice_tc_l2_hdr l2_mask;
	struct ice_tc_vlan_hdr vlan_hdr;
	struct ice_tc_vlan_hdr cvlan_hdr;
	struct ice_tc_pppoe_hdr pppoe_hdr;
	struct ice_tc_l2tpv3_hdr l2tpv3_hdr;
	/* L3 (IPv4[6]) layer fields with their mask */
	struct ice_tc_l3_hdr l3_key;
	struct ice_tc_l3_hdr l3_mask;

	/* L4 layer fields with their mask */
	struct ice_tc_l4_hdr l4_key;
	struct ice_tc_l4_hdr l4_mask;
};

enum ice_eswitch_fltr_direction {
	ICE_ESWITCH_FLTR_INGRESS,
	ICE_ESWITCH_FLTR_EGRESS,
};

struct ice_tc_flower_fltr {
	struct hlist_node tc_flower_node;

	/* cookie becomes filter_rule_id if rule is added successfully */
	unsigned long cookie;

	/* add_adv_rule returns information like recipe ID, rule_id. Store
	 * those values since they are needed to remove advanced rule
	 */
	u16 rid;
	u16 rule_id;
	/* VSI handle of the destination VSI (it could be main PF VSI, CHNL_VSI,
	 * VF VSI)
	 */
	u16 dest_vsi_handle;
	/* ptr to destination VSI */
	struct ice_vsi *dest_vsi;
	/* direction of fltr for eswitch use case */
	enum ice_eswitch_fltr_direction direction;

	/* Parsed TC flower configuration params */
	struct ice_tc_flower_lyr_2_4_hdrs outer_headers;
	struct ice_tc_flower_lyr_2_4_hdrs inner_headers;
	struct ice_vsi *src_vsi;
	__be32 tenant_id;
	struct gtp_pdu_session_info gtp_pdu_info_keys;
	struct gtp_pdu_session_info gtp_pdu_info_masks;
	struct pfcp_metadata pfcp_meta_keys;
	struct pfcp_metadata pfcp_meta_masks;
	u32 flags;
	u8 tunnel_type;
	struct ice_tc_flower_action	action;

	/* cache ptr which is used wherever needed to communicate netlink
	 * messages
	 */
	struct netlink_ext_ack *extack;
};

/**
 * ice_is_chnl_fltr - is this a valid channel filter
 * @f: Pointer to tc-flower filter
 *
 * Criteria to determine of given filter is valid channel filter
 * or not is based on its destination.
 * For forward to VSI action, if destination is valid hw_tc (aka tc_class)
 * and in supported range of TCs for ADQ, then return true.
 * For forward to queue, as long as dest_vsi is valid and it is of type
 * VSI_CHNL (PF ADQ VSI is of type VSI_CHNL), return true.
 * NOTE: For forward to queue, correct dest_vsi is still set in tc_fltr based
 * on destination queue specified.
 */
static inline bool ice_is_chnl_fltr(struct ice_tc_flower_fltr *f)
{
	if (f->action.fltr_act == ICE_FWD_TO_VSI)
		return f->action.fwd.tc.tc_class >= ICE_CHNL_START_TC &&
		       f->action.fwd.tc.tc_class < ICE_CHNL_MAX_TC;
	else if (f->action.fltr_act == ICE_FWD_TO_Q)
		return f->dest_vsi && f->dest_vsi->type == ICE_VSI_CHNL;

	return false;
}

/**
 * ice_chnl_dmac_fltr_cnt - DMAC based CHNL filter count
 * @pf: Pointer to PF
 */
static inline int ice_chnl_dmac_fltr_cnt(struct ice_pf *pf)
{
	return pf->num_dmac_chnl_fltrs;
}

struct ice_vsi *ice_locate_vsi_using_queue(struct ice_vsi *vsi, int queue);
int ice_add_cls_flower(struct net_device *netdev, struct ice_vsi *vsi,
		       struct flow_cls_offload *cls_flower, bool ingress);
int ice_del_cls_flower(struct ice_vsi *vsi,
		       struct flow_cls_offload *cls_flower);
void ice_replay_tc_fltrs(struct ice_pf *pf);
bool ice_is_tunnel_supported(struct net_device *dev);
int ice_drop_vf_tx_lldp(struct ice_vsi *vsi, bool init);
int ice_pass_vf_tx_lldp(struct ice_vsi *vsi, bool deinit);

static inline bool ice_is_forward_action(enum ice_sw_fwd_act_type fltr_act)
{
	switch (fltr_act) {
	case ICE_FWD_TO_VSI:
	case ICE_FWD_TO_Q:
		return true;
	default:
		return false;
	}
}
#endif /* _ICE_TC_LIB_H_ */
