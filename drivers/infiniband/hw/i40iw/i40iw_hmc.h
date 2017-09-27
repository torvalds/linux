/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#ifndef I40IW_HMC_H
#define I40IW_HMC_H

#include "i40iw_d.h"

struct i40iw_hw;
enum i40iw_status_code;

#define I40IW_HMC_MAX_BP_COUNT 512
#define I40IW_MAX_SD_ENTRIES 11
#define I40IW_HW_DBG_HMC_INVALID_BP_MARK     0xCA

#define I40IW_HMC_INFO_SIGNATURE	0x484D5347
#define I40IW_HMC_PD_CNT_IN_SD		512
#define I40IW_HMC_DIRECT_BP_SIZE	0x200000
#define I40IW_HMC_MAX_SD_COUNT		4096
#define I40IW_HMC_PAGED_BP_SIZE		4096
#define I40IW_HMC_PD_BP_BUF_ALIGNMENT	4096
#define I40IW_FIRST_VF_FPM_ID		16
#define FPM_MULTIPLIER			1024

#define I40IW_INC_SD_REFCNT(sd_table)   ((sd_table)->ref_cnt++)
#define I40IW_INC_PD_REFCNT(pd_table)   ((pd_table)->ref_cnt++)
#define I40IW_INC_BP_REFCNT(bp)         ((bp)->ref_cnt++)

#define I40IW_DEC_SD_REFCNT(sd_table)   ((sd_table)->ref_cnt--)
#define I40IW_DEC_PD_REFCNT(pd_table)   ((pd_table)->ref_cnt--)
#define I40IW_DEC_BP_REFCNT(bp)         ((bp)->ref_cnt--)

/**
 * I40IW_INVALIDATE_PF_HMC_PD - Invalidates the pd cache in the hardware
 * @hw: pointer to our hw struct
 * @sd_idx: segment descriptor index
 * @pd_idx: page descriptor index
 */
#define I40IW_INVALIDATE_PF_HMC_PD(hw, sd_idx, pd_idx)                  \
	i40iw_wr32((hw), I40E_PFHMC_PDINV,                                    \
		(((sd_idx) << I40E_PFHMC_PDINV_PMSDIDX_SHIFT) |             \
		(0x1 << I40E_PFHMC_PDINV_PMSDPARTSEL_SHIFT) | \
		((pd_idx) << I40E_PFHMC_PDINV_PMPDIDX_SHIFT)))

/**
 * I40IW_INVALIDATE_VF_HMC_PD - Invalidates the pd cache in the hardware
 * @hw: pointer to our hw struct
 * @sd_idx: segment descriptor index
 * @pd_idx: page descriptor index
 * @hmc_fn_id: VF's function id
 */
#define I40IW_INVALIDATE_VF_HMC_PD(hw, sd_idx, pd_idx, hmc_fn_id)        \
	i40iw_wr32(hw, I40E_GLHMC_VFPDINV(hmc_fn_id - I40IW_FIRST_VF_FPM_ID),  \
	     ((sd_idx << I40E_PFHMC_PDINV_PMSDIDX_SHIFT) |              \
	      (pd_idx << I40E_PFHMC_PDINV_PMPDIDX_SHIFT)))

struct i40iw_hmc_obj_info {
	u64 base;
	u32 max_cnt;
	u32 cnt;
	u64 size;
};

enum i40iw_sd_entry_type {
	I40IW_SD_TYPE_INVALID = 0,
	I40IW_SD_TYPE_PAGED = 1,
	I40IW_SD_TYPE_DIRECT = 2
};

struct i40iw_hmc_bp {
	enum i40iw_sd_entry_type entry_type;
	struct i40iw_dma_mem addr;
	u32 sd_pd_index;
	u32 ref_cnt;
};

struct i40iw_hmc_pd_entry {
	struct i40iw_hmc_bp bp;
	u32 sd_index;
	bool rsrc_pg;
	bool valid;
};

struct i40iw_hmc_pd_table {
	struct i40iw_dma_mem pd_page_addr;
	struct i40iw_hmc_pd_entry *pd_entry;
	struct i40iw_virt_mem pd_entry_virt_mem;
	u32 ref_cnt;
	u32 sd_index;
};

struct i40iw_hmc_sd_entry {
	enum i40iw_sd_entry_type entry_type;
	bool valid;

	union {
		struct i40iw_hmc_pd_table pd_table;
		struct i40iw_hmc_bp bp;
	} u;
};

struct i40iw_hmc_sd_table {
	struct i40iw_virt_mem addr;
	u32 sd_cnt;
	u32 ref_cnt;
	struct i40iw_hmc_sd_entry *sd_entry;
};

struct i40iw_hmc_info {
	u32 signature;
	u8 hmc_fn_id;
	u16 first_sd_index;

	struct i40iw_hmc_obj_info *hmc_obj;
	struct i40iw_virt_mem hmc_obj_virt_mem;
	struct i40iw_hmc_sd_table sd_table;
	u16 sd_indexes[I40IW_HMC_MAX_SD_COUNT];
};

struct update_sd_entry {
	u64 cmd;
	u64 data;
};

struct i40iw_update_sds_info {
	u32 cnt;
	u8 hmc_fn_id;
	struct update_sd_entry entry[I40IW_MAX_SD_ENTRIES];
};

struct i40iw_ccq_cqe_info;
struct i40iw_hmc_fcn_info {
	void (*callback_fcn)(struct i40iw_sc_dev *, void *,
			     struct i40iw_ccq_cqe_info *);
	void *cqp_callback_param;
	u32 vf_id;
	u16 iw_vf_idx;
	bool free_fcn;
};

enum i40iw_hmc_rsrc_type {
	I40IW_HMC_IW_QP = 0,
	I40IW_HMC_IW_CQ = 1,
	I40IW_HMC_IW_SRQ = 2,
	I40IW_HMC_IW_HTE = 3,
	I40IW_HMC_IW_ARP = 4,
	I40IW_HMC_IW_APBVT_ENTRY = 5,
	I40IW_HMC_IW_MR = 6,
	I40IW_HMC_IW_XF = 7,
	I40IW_HMC_IW_XFFL = 8,
	I40IW_HMC_IW_Q1 = 9,
	I40IW_HMC_IW_Q1FL = 10,
	I40IW_HMC_IW_TIMER = 11,
	I40IW_HMC_IW_FSIMC = 12,
	I40IW_HMC_IW_FSIAV = 13,
	I40IW_HMC_IW_PBLE = 14,
	I40IW_HMC_IW_MAX = 15,
};

struct i40iw_hmc_create_obj_info {
	struct i40iw_hmc_info *hmc_info;
	struct i40iw_virt_mem add_sd_virt_mem;
	u32 rsrc_type;
	u32 start_idx;
	u32 count;
	u32 add_sd_cnt;
	enum i40iw_sd_entry_type entry_type;
	bool is_pf;
};

struct i40iw_hmc_del_obj_info {
	struct i40iw_hmc_info *hmc_info;
	struct i40iw_virt_mem del_sd_virt_mem;
	u32 rsrc_type;
	u32 start_idx;
	u32 count;
	u32 del_sd_cnt;
	bool is_pf;
};

enum i40iw_status_code i40iw_copy_dma_mem(struct i40iw_hw *hw, void *dest_buf,
					  struct i40iw_dma_mem *src_mem, u64 src_offset, u64 size);
enum i40iw_status_code i40iw_sc_create_hmc_obj(struct i40iw_sc_dev *dev,
					       struct i40iw_hmc_create_obj_info *info);
enum i40iw_status_code i40iw_sc_del_hmc_obj(struct i40iw_sc_dev *dev,
					    struct i40iw_hmc_del_obj_info *info,
					    bool reset);
enum i40iw_status_code i40iw_hmc_sd_one(struct i40iw_sc_dev *dev, u8 hmc_fn_id,
					u64 pa, u32 sd_idx, enum i40iw_sd_entry_type type,
					bool setsd);
enum i40iw_status_code i40iw_update_sds_noccq(struct i40iw_sc_dev *dev,
					      struct i40iw_update_sds_info *info);
struct i40iw_vfdev *i40iw_vfdev_from_fpm(struct i40iw_sc_dev *dev, u8 hmc_fn_id);
struct i40iw_hmc_info *i40iw_vf_hmcinfo_from_fpm(struct i40iw_sc_dev *dev,
						 u8 hmc_fn_id);
enum i40iw_status_code i40iw_add_sd_table_entry(struct i40iw_hw *hw,
						struct i40iw_hmc_info *hmc_info, u32 sd_index,
						enum i40iw_sd_entry_type type, u64 direct_mode_sz);
enum i40iw_status_code i40iw_add_pd_table_entry(struct i40iw_hw *hw,
						struct i40iw_hmc_info *hmc_info, u32 pd_index,
						struct i40iw_dma_mem *rsrc_pg);
enum i40iw_status_code i40iw_remove_pd_bp(struct i40iw_hw *hw,
					  struct i40iw_hmc_info *hmc_info, u32 idx, bool is_pf);
enum i40iw_status_code i40iw_prep_remove_sd_bp(struct i40iw_hmc_info *hmc_info, u32 idx);
enum i40iw_status_code i40iw_prep_remove_pd_page(struct i40iw_hmc_info *hmc_info, u32 idx);

#define     ENTER_SHARED_FUNCTION()
#define     EXIT_SHARED_FUNCTION()

#endif				/* I40IW_HMC_H */
