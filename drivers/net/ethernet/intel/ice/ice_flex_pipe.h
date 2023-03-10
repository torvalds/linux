/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_FLEX_PIPE_H_
#define _ICE_FLEX_PIPE_H_

#include "ice_type.h"

int
ice_acquire_change_lock(struct ice_hw *hw, enum ice_aq_res_access_type access);
void ice_release_change_lock(struct ice_hw *hw);
int
ice_find_prot_off(struct ice_hw *hw, enum ice_block blk, u8 prof, u16 fv_idx,
		  u8 *prot, u16 *off);
void
ice_get_sw_fv_bitmap(struct ice_hw *hw, enum ice_prof_type type,
		     unsigned long *bm);
void
ice_init_prof_result_bm(struct ice_hw *hw);
int
ice_get_sw_fv_list(struct ice_hw *hw, struct ice_prot_lkup_ext *lkups,
		   unsigned long *bm, struct list_head *fv_list);
int
ice_pkg_buf_unreserve_section(struct ice_buf_build *bld, u16 count);
u16 ice_pkg_buf_get_free_space(struct ice_buf_build *bld);
int
ice_aq_upload_section(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
		      u16 buf_size, struct ice_sq_cd *cd);
bool
ice_get_open_tunnel_port(struct ice_hw *hw, u16 *port,
			 enum ice_tunnel_type type);
int ice_udp_tunnel_set_port(struct net_device *netdev, unsigned int table,
			    unsigned int idx, struct udp_tunnel_info *ti);
int ice_udp_tunnel_unset_port(struct net_device *netdev, unsigned int table,
			      unsigned int idx, struct udp_tunnel_info *ti);
int ice_set_dvm_boost_entries(struct ice_hw *hw);

/* Rx parser PTYPE functions */
bool ice_hw_ptype_ena(struct ice_hw *hw, u16 ptype);

/* XLT2/VSI group functions */
int
ice_add_prof(struct ice_hw *hw, enum ice_block blk, u64 id, u8 ptypes[],
	     const struct ice_ptype_attributes *attr, u16 attr_cnt,
	     struct ice_fv_word *es, u16 *masks);
int
ice_add_prof_id_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl);
int
ice_rem_prof_id_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl);
enum ice_ddp_state ice_init_pkg(struct ice_hw *hw, u8 *buff, u32 len);
enum ice_ddp_state
ice_copy_and_init_pkg(struct ice_hw *hw, const u8 *buf, u32 len);
bool ice_is_init_pkg_successful(enum ice_ddp_state state);
int ice_init_hw_tbls(struct ice_hw *hw);
void ice_free_seg(struct ice_hw *hw);
void ice_fill_blk_tbls(struct ice_hw *hw);
void ice_clear_hw_tbls(struct ice_hw *hw);
void ice_free_hw_tbls(struct ice_hw *hw);
int ice_rem_prof(struct ice_hw *hw, enum ice_block blk, u64 id);
struct ice_buf_build *
ice_pkg_buf_alloc_single_section(struct ice_hw *hw, u32 type, u16 size,
				 void **section);
struct ice_buf *ice_pkg_buf(struct ice_buf_build *bld);
void ice_pkg_buf_free(struct ice_hw *hw, struct ice_buf_build *bld);

#endif /* _ICE_FLEX_PIPE_H_ */
