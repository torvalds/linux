/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020, Intel Corporation. */

#ifndef _ICE_FDIR_H_
#define _ICE_FDIR_H_

enum ice_fltr_prgm_desc_dest {
	ICE_FLTR_PRGM_DESC_DEST_DROP_PKT,
	ICE_FLTR_PRGM_DESC_DEST_DIRECT_PKT_QINDEX,
};

struct ice_fdir_v4 {
	__be32 dst_ip;
	__be32 src_ip;
	__be16 dst_port;
	__be16 src_port;
	__be32 l4_header;
	__be32 sec_parm_idx;	/* security parameter index */
	u8 tos;
	u8 ip_ver;
	u8 proto;
};

struct ice_fdir_extra {
	u8 dst_mac[ETH_ALEN];	/* dest MAC address */
	u32 usr_def[2];		/* user data */
	__be16 vlan_type;	/* VLAN ethertype */
	__be16 vlan_tag;	/* VLAN tag info */
};

struct ice_fdir_fltr {
	struct list_head fltr_node;
	enum ice_fltr_ptype flow_type;

	struct ice_fdir_v4 ip;
	struct ice_fdir_v4 mask;

	struct ice_fdir_extra ext_data;
	struct ice_fdir_extra ext_mask;

	/* filter control */
	u16 q_index;
	u16 dest_vsi;
	u8 dest_ctl;
	u8 fltr_status;
	u16 cnt_index;
	u32 fltr_id;
};

enum ice_status ice_alloc_fd_res_cntr(struct ice_hw *hw, u16 *cntr_id);
enum ice_status ice_free_fd_res_cntr(struct ice_hw *hw, u16 cntr_id);
enum ice_status
ice_alloc_fd_guar_item(struct ice_hw *hw, u16 *cntr_id, u16 num_fltr);
enum ice_status
ice_alloc_fd_shrd_item(struct ice_hw *hw, u16 *cntr_id, u16 num_fltr);
int ice_get_fdir_cnt_all(struct ice_hw *hw);
struct ice_fdir_fltr *
ice_fdir_find_fltr_by_idx(struct ice_hw *hw, u32 fltr_idx);
#endif /* _ICE_FDIR_H_ */
