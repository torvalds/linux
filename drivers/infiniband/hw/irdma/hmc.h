/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2015 - 2020 Intel Corporation */
#ifndef IRDMA_HMC_H
#define IRDMA_HMC_H

#include "defs.h"

#define IRDMA_HMC_MAX_BP_COUNT			512
#define IRDMA_MAX_SD_ENTRIES			11
#define IRDMA_HW_DBG_HMC_INVALID_BP_MARK	0xca
#define IRDMA_HMC_INFO_SIGNATURE		0x484d5347
#define IRDMA_HMC_PD_CNT_IN_SD			512
#define IRDMA_HMC_DIRECT_BP_SIZE		0x200000
#define IRDMA_HMC_MAX_SD_COUNT			8192
#define IRDMA_HMC_PAGED_BP_SIZE			4096
#define IRDMA_HMC_PD_BP_BUF_ALIGNMENT		4096
#define IRDMA_FIRST_VF_FPM_ID			8
#define FPM_MULTIPLIER				1024
#define IRDMA_OBJ_LOC_MEM_BIT			0x4
#define IRDMA_XF_MULTIPLIER			16
#define IRDMA_RRF_MULTIPLIER			8
#define IRDMA_MIN_PBLE_PAGES			3
#define IRDMA_HMC_PAGE_SIZE			2097152
#define IRDMA_MIN_MR_PER_QP			4
#define IRDMA_MIN_QP_CNT			64
#define IRDMA_FSIAV_CNT_MAX			1048576
#define IRDMA_MIN_IRD				8
#define IRDMA_HMC_MIN_RRF			16

enum irdma_hmc_rsrc_type {
	IRDMA_HMC_IW_QP		 = 0,
	IRDMA_HMC_IW_CQ		 = 1,
	IRDMA_HMC_IW_SRQ	 = 2,
	IRDMA_HMC_IW_HTE	 = 3,
	IRDMA_HMC_IW_ARP	 = 4,
	IRDMA_HMC_IW_APBVT_ENTRY = 5,
	IRDMA_HMC_IW_MR		 = 6,
	IRDMA_HMC_IW_XF		 = 7,
	IRDMA_HMC_IW_XFFL	 = 8,
	IRDMA_HMC_IW_Q1		 = 9,
	IRDMA_HMC_IW_Q1FL	 = 10,
	IRDMA_HMC_IW_TIMER       = 11,
	IRDMA_HMC_IW_FSIMC       = 12,
	IRDMA_HMC_IW_FSIAV       = 13,
	IRDMA_HMC_IW_PBLE	 = 14,
	IRDMA_HMC_IW_RRF	 = 15,
	IRDMA_HMC_IW_RRFFL       = 16,
	IRDMA_HMC_IW_HDR	 = 17,
	IRDMA_HMC_IW_MD		 = 18,
	IRDMA_HMC_IW_OOISC       = 19,
	IRDMA_HMC_IW_OOISCFFL    = 20,
	IRDMA_HMC_IW_MAX, /* Must be last entry */
};

enum irdma_sd_entry_type {
	IRDMA_SD_TYPE_INVALID = 0,
	IRDMA_SD_TYPE_PAGED   = 1,
	IRDMA_SD_TYPE_DIRECT  = 2,
};

enum irdma_hmc_obj_mem {
	IRDMA_HOST_MEM = 0,
	IRDMA_LOC_MEM  = 1,
};

struct irdma_hmc_obj_info {
	u64 base;
	u32 max_cnt;
	u32 cnt;
	u64 size;
	enum irdma_hmc_obj_mem mem_loc;
};

struct irdma_hmc_bp {
	enum irdma_sd_entry_type entry_type;
	struct irdma_dma_mem addr;
	u32 sd_pd_index;
	u32 use_cnt;
};

struct irdma_hmc_pd_entry {
	struct irdma_hmc_bp bp;
	u32 sd_index;
	bool rsrc_pg:1;
	bool valid:1;
};

struct irdma_hmc_pd_table {
	struct irdma_dma_mem pd_page_addr;
	struct irdma_hmc_pd_entry *pd_entry;
	struct irdma_virt_mem pd_entry_virt_mem;
	u32 use_cnt;
	u32 sd_index;
};

struct irdma_hmc_sd_entry {
	enum irdma_sd_entry_type entry_type;
	bool valid;
	union {
		struct irdma_hmc_pd_table pd_table;
		struct irdma_hmc_bp bp;
	} u;
};

struct irdma_hmc_sd_table {
	struct irdma_virt_mem addr;
	u32 sd_cnt;
	u32 use_cnt;
	struct irdma_hmc_sd_entry *sd_entry;
};

struct irdma_hmc_info {
	u32 signature;
	u8 hmc_fn_id;
	u16 first_sd_index;
	struct irdma_hmc_obj_info *hmc_obj;
	struct irdma_virt_mem hmc_obj_virt_mem;
	struct irdma_hmc_sd_table sd_table;
	u16 sd_indexes[IRDMA_HMC_MAX_SD_COUNT];
};

struct irdma_update_sd_entry {
	u64 cmd;
	u64 data;
};

struct irdma_update_sds_info {
	u32 cnt;
	u8 hmc_fn_id;
	struct irdma_update_sd_entry entry[IRDMA_MAX_SD_ENTRIES];
};

struct irdma_ccq_cqe_info;
struct irdma_hmc_fcn_info {
	u32 vf_id;
	u8 protocol_used;
	u8 free_fcn;
};

struct irdma_hmc_create_obj_info {
	struct irdma_hmc_info *hmc_info;
	struct irdma_virt_mem add_sd_virt_mem;
	u32 rsrc_type;
	u32 start_idx;
	u32 count;
	u32 add_sd_cnt;
	enum irdma_sd_entry_type entry_type;
	bool privileged;
};

struct irdma_hmc_del_obj_info {
	struct irdma_hmc_info *hmc_info;
	struct irdma_virt_mem del_sd_virt_mem;
	u32 rsrc_type;
	u32 start_idx;
	u32 count;
	u32 del_sd_cnt;
	bool privileged;
};

int irdma_copy_dma_mem(struct irdma_hw *hw, void *dest_buf,
		       struct irdma_dma_mem *src_mem, u64 src_offset, u64 size);
int irdma_sc_create_hmc_obj(struct irdma_sc_dev *dev,
			    struct irdma_hmc_create_obj_info *info);
int irdma_sc_del_hmc_obj(struct irdma_sc_dev *dev,
			 struct irdma_hmc_del_obj_info *info, bool reset);
int irdma_hmc_sd_one(struct irdma_sc_dev *dev, u8 hmc_fn_id, u64 pa, u32 sd_idx,
		     enum irdma_sd_entry_type type,
		     bool setsd);
int irdma_update_sds_noccq(struct irdma_sc_dev *dev,
			   struct irdma_update_sds_info *info);
struct irdma_vfdev *irdma_vfdev_from_fpm(struct irdma_sc_dev *dev,
					 u8 hmc_fn_id);
struct irdma_hmc_info *irdma_vf_hmcinfo_from_fpm(struct irdma_sc_dev *dev,
						 u8 hmc_fn_id);
int irdma_add_sd_table_entry(struct irdma_hw *hw,
			     struct irdma_hmc_info *hmc_info, u32 sd_index,
			     enum irdma_sd_entry_type type, u64 direct_mode_sz);
int irdma_add_pd_table_entry(struct irdma_sc_dev *dev,
			     struct irdma_hmc_info *hmc_info, u32 pd_index,
			     struct irdma_dma_mem *rsrc_pg);
int irdma_remove_pd_bp(struct irdma_sc_dev *dev,
		       struct irdma_hmc_info *hmc_info, u32 idx);
int irdma_prep_remove_sd_bp(struct irdma_hmc_info *hmc_info, u32 idx);
int irdma_prep_remove_pd_page(struct irdma_hmc_info *hmc_info, u32 idx);
#endif /* IRDMA_HMC_H */
