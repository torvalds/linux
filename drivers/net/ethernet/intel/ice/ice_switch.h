/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SWITCH_H_
#define _ICE_SWITCH_H_

#include "ice_common.h"

#define ICE_SW_CFG_MAX_BUF_LEN 2048
#define ICE_DFLT_VSI_INVAL 0xff
#define ICE_FLTR_RX BIT(0)
#define ICE_FLTR_TX BIT(1)
#define ICE_FLTR_TX_RX (ICE_FLTR_RX | ICE_FLTR_TX)
#define ICE_VSI_INVAL_ID 0xffff
#define ICE_INVAL_Q_HANDLE 0xFFFF

#define ICE_SW_RULE_RX_TX_NO_HDR_SIZE \
	(offsetof(struct ice_aqc_sw_rules_elem, pdata.lkup_tx_rx.hdr))

/* VSI context structure for add/get/update/free operations */
struct ice_vsi_ctx {
	u16 vsi_num;
	u16 vsis_allocd;
	u16 vsis_unallocated;
	u16 flags;
	struct ice_aqc_vsi_props info;
	struct ice_sched_vsi_info sched;
	u8 alloc_from_pool;
	u8 vf_num;
	u16 num_lan_q_entries[ICE_MAX_TRAFFIC_CLASS];
	struct ice_q_ctx *lan_q_ctx[ICE_MAX_TRAFFIC_CLASS];
	u16 num_rdma_q_entries[ICE_MAX_TRAFFIC_CLASS];
	struct ice_q_ctx *rdma_q_ctx[ICE_MAX_TRAFFIC_CLASS];
};

enum ice_sw_fwd_act_type {
	ICE_FWD_TO_VSI = 0,
	ICE_FWD_TO_VSI_LIST, /* Do not use this when adding filter */
	ICE_FWD_TO_Q,
	ICE_FWD_TO_QGRP,
	ICE_DROP_PACKET,
	ICE_INVAL_ACT
};

/* Switch recipe ID enum values are specific to hardware */
enum ice_sw_lkup_type {
	ICE_SW_LKUP_ETHERTYPE = 0,
	ICE_SW_LKUP_MAC = 1,
	ICE_SW_LKUP_MAC_VLAN = 2,
	ICE_SW_LKUP_PROMISC = 3,
	ICE_SW_LKUP_VLAN = 4,
	ICE_SW_LKUP_DFLT = 5,
	ICE_SW_LKUP_ETHERTYPE_MAC = 8,
	ICE_SW_LKUP_PROMISC_VLAN = 9,
	ICE_SW_LKUP_LAST
};

/* type of filter src ID */
enum ice_src_id {
	ICE_SRC_ID_UNKNOWN = 0,
	ICE_SRC_ID_VSI,
	ICE_SRC_ID_QUEUE,
	ICE_SRC_ID_LPORT,
};

struct ice_fltr_info {
	/* Look up information: how to look up packet */
	enum ice_sw_lkup_type lkup_type;
	/* Forward action: filter action to do after lookup */
	enum ice_sw_fwd_act_type fltr_act;
	/* rule ID returned by firmware once filter rule is created */
	u16 fltr_rule_id;
	u16 flag;

	/* Source VSI for LOOKUP_TX or source port for LOOKUP_RX */
	u16 src;
	enum ice_src_id src_id;

	union {
		struct {
			u8 mac_addr[ETH_ALEN];
		} mac;
		struct {
			u8 mac_addr[ETH_ALEN];
			u16 vlan_id;
		} mac_vlan;
		struct {
			u16 vlan_id;
		} vlan;
		/* Set lkup_type as ICE_SW_LKUP_ETHERTYPE
		 * if just using ethertype as filter. Set lkup_type as
		 * ICE_SW_LKUP_ETHERTYPE_MAC if MAC also needs to be
		 * passed in as filter.
		 */
		struct {
			u16 ethertype;
			u8 mac_addr[ETH_ALEN]; /* optional */
		} ethertype_mac;
	} l_data; /* Make sure to zero out the memory of l_data before using
		   * it or only set the data associated with lookup match
		   * rest everything should be zero
		   */

	/* Depending on filter action */
	union {
		/* queue ID in case of ICE_FWD_TO_Q and starting
		 * queue ID in case of ICE_FWD_TO_QGRP.
		 */
		u16 q_id:11;
		u16 hw_vsi_id:10;
		u16 vsi_list_id:10;
	} fwd_id;

	/* Sw VSI handle */
	u16 vsi_handle;

	/* Set to num_queues if action is ICE_FWD_TO_QGRP. This field
	 * determines the range of queues the packet needs to be forwarded to.
	 * Note that qgrp_size must be set to a power of 2.
	 */
	u8 qgrp_size;

	/* Rule creations populate these indicators basing on the switch type */
	u8 lb_en;	/* Indicate if packet can be looped back */
	u8 lan_en;	/* Indicate if packet can be forwarded to the uplink */
};

struct ice_adv_lkup_elem {
	enum ice_protocol_type type;
	union ice_prot_hdr h_u;	/* Header values */
	union ice_prot_hdr m_u;	/* Mask of header values to match */
};

struct ice_sw_act_ctrl {
	/* Source VSI for LOOKUP_TX or source port for LOOKUP_RX */
	u16 src;
	u16 flag;
	enum ice_sw_fwd_act_type fltr_act;
	/* Depending on filter action */
	union {
		/* This is a queue ID in case of ICE_FWD_TO_Q and starting
		 * queue ID in case of ICE_FWD_TO_QGRP.
		 */
		u16 q_id:11;
		u16 vsi_id:10;
		u16 hw_vsi_id:10;
		u16 vsi_list_id:10;
	} fwd_id;
	/* software VSI handle */
	u16 vsi_handle;
	u8 qgrp_size;
};

struct ice_rule_query_data {
	/* Recipe ID for which the requested rule was added */
	u16 rid;
	/* Rule ID that was added or is supposed to be removed */
	u16 rule_id;
	/* vsi_handle for which Rule was added or is supposed to be removed */
	u16 vsi_handle;
};

struct ice_adv_rule_info {
	struct ice_sw_act_ctrl sw_act;
	u32 priority;
	u8 rx; /* true means LOOKUP_RX otherwise LOOKUP_TX */
	u16 fltr_rule_id;
};

/* A collection of one or more four word recipe */
struct ice_sw_recipe {
	/* For a chained recipe the root recipe is what should be used for
	 * programming rules
	 */
	u8 is_root;
	u8 root_rid;
	u8 recp_created;

	/* Number of extraction words */
	u8 n_ext_words;
	/* Protocol ID and Offset pair (extraction word) to describe the
	 * recipe
	 */
	struct ice_fv_word ext_words[ICE_MAX_CHAIN_WORDS];
	u16 word_masks[ICE_MAX_CHAIN_WORDS];

	/* if this recipe is a collection of other recipe */
	u8 big_recp;

	/* if this recipe is part of another bigger recipe then chain index
	 * corresponding to this recipe
	 */
	u8 chain_idx;

	/* if this recipe is a collection of other recipe then count of other
	 * recipes and recipe IDs of those recipes
	 */
	u8 n_grp_count;

	/* Bit map specifying the IDs associated with this group of recipe */
	DECLARE_BITMAP(r_bitmap, ICE_MAX_NUM_RECIPES);

	/* List of type ice_fltr_mgmt_list_entry or adv_rule */
	u8 adv_rule;
	struct list_head filt_rules;
	struct list_head filt_replay_rules;

	struct mutex filt_rule_lock;	/* protect filter rule structure */

	/* Profiles this recipe should be associated with */
	struct list_head fv_list;

	/* Profiles this recipe is associated with */
	u8 num_profs, *prof_ids;

	/* Bit map for possible result indexes */
	DECLARE_BITMAP(res_idxs, ICE_MAX_FV_WORDS);

	/* This allows user to specify the recipe priority.
	 * For now, this becomes 'fwd_priority' when recipe
	 * is created, usually recipes can have 'fwd' and 'join'
	 * priority.
	 */
	u8 priority;

	struct list_head rg_list;

	/* AQ buffer associated with this recipe */
	struct ice_aqc_recipe_data_elem *root_buf;
	/* This struct saves the fv_words for a given lookup */
	struct ice_prot_lkup_ext lkup_exts;
};

/* Bookkeeping structure to hold bitmap of VSIs corresponding to VSI list ID */
struct ice_vsi_list_map_info {
	struct list_head list_entry;
	DECLARE_BITMAP(vsi_map, ICE_MAX_VSI);
	u16 vsi_list_id;
	/* counter to track how many rules are reusing this VSI list */
	u16 ref_cnt;
};

struct ice_fltr_list_entry {
	struct list_head list_entry;
	enum ice_status status;
	struct ice_fltr_info fltr_info;
};

/* This defines an entry in the list that maintains MAC or VLAN membership
 * to HW list mapping, since multiple VSIs can subscribe to the same MAC or
 * VLAN. As an optimization the VSI list should be created only when a
 * second VSI becomes a subscriber to the same MAC address. VSI lists are always
 * used for VLAN membership.
 */
struct ice_fltr_mgmt_list_entry {
	/* back pointer to VSI list ID to VSI list mapping */
	struct ice_vsi_list_map_info *vsi_list_info;
	u16 vsi_count;
#define ICE_INVAL_LG_ACT_INDEX 0xffff
	u16 lg_act_idx;
#define ICE_INVAL_SW_MARKER_ID 0xffff
	u16 sw_marker_id;
	struct list_head list_entry;
	struct ice_fltr_info fltr_info;
#define ICE_INVAL_COUNTER_ID 0xff
	u8 counter_index;
};

enum ice_promisc_flags {
	ICE_PROMISC_UCAST_RX = 0x1,
	ICE_PROMISC_UCAST_TX = 0x2,
	ICE_PROMISC_MCAST_RX = 0x4,
	ICE_PROMISC_MCAST_TX = 0x8,
	ICE_PROMISC_BCAST_RX = 0x10,
	ICE_PROMISC_BCAST_TX = 0x20,
	ICE_PROMISC_VLAN_RX = 0x40,
	ICE_PROMISC_VLAN_TX = 0x80,
};

/* VSI related commands */
enum ice_status
ice_add_vsi(struct ice_hw *hw, u16 vsi_handle, struct ice_vsi_ctx *vsi_ctx,
	    struct ice_sq_cd *cd);
enum ice_status
ice_free_vsi(struct ice_hw *hw, u16 vsi_handle, struct ice_vsi_ctx *vsi_ctx,
	     bool keep_vsi_alloc, struct ice_sq_cd *cd);
enum ice_status
ice_update_vsi(struct ice_hw *hw, u16 vsi_handle, struct ice_vsi_ctx *vsi_ctx,
	       struct ice_sq_cd *cd);
bool ice_is_vsi_valid(struct ice_hw *hw, u16 vsi_handle);
struct ice_vsi_ctx *ice_get_vsi_ctx(struct ice_hw *hw, u16 vsi_handle);
void ice_clear_all_vsi_ctx(struct ice_hw *hw);
/* Switch config */
enum ice_status ice_get_initial_sw_cfg(struct ice_hw *hw);

enum ice_status
ice_alloc_res_cntr(struct ice_hw *hw, u8 type, u8 alloc_shared, u16 num_items,
		   u16 *counter_id);
enum ice_status
ice_free_res_cntr(struct ice_hw *hw, u8 type, u8 alloc_shared, u16 num_items,
		  u16 counter_id);

/* Switch/bridge related commands */
enum ice_status ice_update_sw_rule_bridge_mode(struct ice_hw *hw);
enum ice_status ice_add_mac(struct ice_hw *hw, struct list_head *m_lst);
enum ice_status ice_remove_mac(struct ice_hw *hw, struct list_head *m_lst);
enum ice_status
ice_add_eth_mac(struct ice_hw *hw, struct list_head *em_list);
enum ice_status
ice_remove_eth_mac(struct ice_hw *hw, struct list_head *em_list);
int
ice_cfg_rdma_fltr(struct ice_hw *hw, u16 vsi_handle, bool enable);
void ice_remove_vsi_fltr(struct ice_hw *hw, u16 vsi_handle);
enum ice_status
ice_add_vlan(struct ice_hw *hw, struct list_head *m_list);
enum ice_status ice_remove_vlan(struct ice_hw *hw, struct list_head *v_list);

/* Promisc/defport setup for VSIs */
enum ice_status
ice_cfg_dflt_vsi(struct ice_hw *hw, u16 vsi_handle, bool set, u8 direction);
enum ice_status
ice_set_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
		    u16 vid);
enum ice_status
ice_clear_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
		      u16 vid);
enum ice_status
ice_set_vlan_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
			 bool rm_vlan_promisc);

enum ice_status ice_init_def_sw_recp(struct ice_hw *hw);
u16 ice_get_hw_vsi_num(struct ice_hw *hw, u16 vsi_handle);

enum ice_status ice_replay_vsi_all_fltr(struct ice_hw *hw, u16 vsi_handle);
void ice_rm_all_sw_replay_rule_info(struct ice_hw *hw);

enum ice_status
ice_aq_sw_rules(struct ice_hw *hw, void *rule_list, u16 rule_list_sz,
		u8 num_rules, enum ice_adminq_opc opc, struct ice_sq_cd *cd);
#endif /* _ICE_SWITCH_H_ */
