/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_FLEX_PIPE_H_
#define _ICE_FLEX_PIPE_H_

#include "ice_type.h"

/* Package minimal version supported */
#define ICE_PKG_SUPP_VER_MAJ	1
#define ICE_PKG_SUPP_VER_MNR	3

/* Package format version */
#define ICE_PKG_FMT_VER_MAJ	1
#define ICE_PKG_FMT_VER_MNR	0
#define ICE_PKG_FMT_VER_UPD	0
#define ICE_PKG_FMT_VER_DFT	0

#define ICE_PKG_CNT 4

enum ice_ddp_state {
	/* Indicates that this call to ice_init_pkg
	 * successfully loaded the requested DDP package
	 */
	ICE_DDP_PKG_SUCCESS			= 0,

	/* Generic error for already loaded errors, it is mapped later to
	 * the more specific one (one of the next 3)
	 */
	ICE_DDP_PKG_ALREADY_LOADED			= -1,

	/* Indicates that a DDP package of the same version has already been
	 * loaded onto the device by a previous call or by another PF
	 */
	ICE_DDP_PKG_SAME_VERSION_ALREADY_LOADED		= -2,

	/* The device has a DDP package that is not supported by the driver */
	ICE_DDP_PKG_ALREADY_LOADED_NOT_SUPPORTED	= -3,

	/* The device has a compatible package
	 * (but different from the request) already loaded
	 */
	ICE_DDP_PKG_COMPATIBLE_ALREADY_LOADED		= -4,

	/* The firmware loaded on the device is not compatible with
	 * the DDP package loaded
	 */
	ICE_DDP_PKG_FW_MISMATCH				= -5,

	/* The DDP package file is invalid */
	ICE_DDP_PKG_INVALID_FILE			= -6,

	/* The version of the DDP package provided is higher than
	 * the driver supports
	 */
	ICE_DDP_PKG_FILE_VERSION_TOO_HIGH		= -7,

	/* The version of the DDP package provided is lower than the
	 * driver supports
	 */
	ICE_DDP_PKG_FILE_VERSION_TOO_LOW		= -8,

	/* The signature of the DDP package file provided is invalid */
	ICE_DDP_PKG_FILE_SIGNATURE_INVALID		= -9,

	/* The DDP package file security revision is too low and not
	 * supported by firmware
	 */
	ICE_DDP_PKG_FILE_REVISION_TOO_LOW		= -10,

	/* An error occurred in firmware while loading the DDP package */
	ICE_DDP_PKG_LOAD_ERROR				= -11,

	/* Other errors */
	ICE_DDP_PKG_ERR					= -12
};

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
ice_get_sw_fv_list(struct ice_hw *hw, u8 *prot_ids, u16 ids_cnt,
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
