/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SWITCH_H_
#define _ICE_SWITCH_H_

#include "ice_common.h"

#define ICE_SW_CFG_MAX_BUF_LEN 2048
#define ICE_DFLT_VSI_INVAL 0xff
#define ICE_VSI_INVAL_ID 0xffff

/* VSI context structure for add/get/update/free operations */
struct ice_vsi_ctx {
	u16 vsi_num;
	u16 vsis_allocd;
	u16 vsis_unallocated;
	u16 flags;
	struct ice_aqc_vsi_props info;
	struct ice_sched_vsi_info sched;
	u8 alloc_from_pool;
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

/* type of filter src id */
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
#define ICE_FLTR_RX		BIT(0)
#define ICE_FLTR_TX		BIT(1)
#define ICE_FLTR_TX_RX		(ICE_FLTR_RX | ICE_FLTR_TX)

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
		/* queue id in case of ICE_FWD_TO_Q and starting
		 * queue id in case of ICE_FWD_TO_QGRP.
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

struct ice_sw_recipe {
	struct list_head l_entry;

	/* To protect modification of filt_rule list
	 * defined below
	 */
	struct mutex filt_rule_lock;

	/* List of type ice_fltr_mgmt_list_entry */
	struct list_head filt_rules;
	struct list_head filt_replay_rules;

	/* linked list of type recipe_list_entry */
	struct list_head rg_list;
	/* linked list of type ice_sw_fv_list_entry*/
	struct list_head fv_list;
	struct ice_aqc_recipe_data_elem *r_buf;
	u8 recp_count;
	u8 root_rid;
	u8 num_profs;
	u8 *prof_ids;

	/* recipe bitmap: what all recipes makes this recipe */
	DECLARE_BITMAP(r_bitmap, ICE_MAX_NUM_RECIPES);
};

/* Bookkeeping structure to hold bitmap of VSIs corresponding to VSI list id */
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
	/* back pointer to VSI list id to VSI list mapping */
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
enum ice_status ice_get_initial_sw_cfg(struct ice_hw *hw);

/* Switch/bridge related commands */
enum ice_status ice_update_sw_rule_bridge_mode(struct ice_hw *hw);
enum ice_status ice_add_mac(struct ice_hw *hw, struct list_head *m_lst);
enum ice_status ice_remove_mac(struct ice_hw *hw, struct list_head *m_lst);
void ice_remove_vsi_fltr(struct ice_hw *hw, u16 vsi_handle);
enum ice_status ice_add_vlan(struct ice_hw *hw, struct list_head *m_list);
enum ice_status ice_remove_vlan(struct ice_hw *hw, struct list_head *v_list);
enum ice_status
ice_cfg_dflt_vsi(struct ice_hw *hw, u16 vsi_handle, bool set, u8 direction);

enum ice_status ice_init_def_sw_recp(struct ice_hw *hw);
u16 ice_get_hw_vsi_num(struct ice_hw *hw, u16 vsi_handle);
bool ice_is_vsi_valid(struct ice_hw *hw, u16 vsi_handle);

enum ice_status ice_replay_vsi_all_fltr(struct ice_hw *hw, u16 vsi_handle);
void ice_rm_all_sw_replay_rule_info(struct ice_hw *hw);

#endif /* _ICE_SWITCH_H_ */
