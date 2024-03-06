/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_HMC_H_
#define _I40E_HMC_H_

#include "i40e_alloc.h"
#include "i40e_io.h"
#include "i40e_register.h"

#define I40E_HMC_MAX_BP_COUNT 512

/* forward-declare the HW struct for the compiler */
struct i40e_hw;

#define I40E_HMC_INFO_SIGNATURE		0x484D5347 /* HMSG */
#define I40E_HMC_PD_CNT_IN_SD		512
#define I40E_HMC_DIRECT_BP_SIZE		0x200000 /* 2M */
#define I40E_HMC_PAGED_BP_SIZE		4096
#define I40E_HMC_PD_BP_BUF_ALIGNMENT	4096

struct i40e_hmc_obj_info {
	u64 base;	/* base addr in FPM */
	u32 max_cnt;	/* max count available for this hmc func */
	u32 cnt;	/* count of objects driver actually wants to create */
	u64 size;	/* size in bytes of one object */
};

enum i40e_sd_entry_type {
	I40E_SD_TYPE_INVALID = 0,
	I40E_SD_TYPE_PAGED   = 1,
	I40E_SD_TYPE_DIRECT  = 2
};

struct i40e_hmc_bp {
	enum i40e_sd_entry_type entry_type;
	struct i40e_dma_mem addr; /* populate to be used by hw */
	u32 sd_pd_index;
	u32 ref_cnt;
};

struct i40e_hmc_pd_entry {
	struct i40e_hmc_bp bp;
	u32 sd_index;
	bool rsrc_pg;
	bool valid;
};

struct i40e_hmc_pd_table {
	struct i40e_dma_mem pd_page_addr; /* populate to be used by hw */
	struct i40e_hmc_pd_entry  *pd_entry; /* [512] for sw book keeping */
	struct i40e_virt_mem pd_entry_virt_mem; /* virt mem for pd_entry */

	u32 ref_cnt;
	u32 sd_index;
};

struct i40e_hmc_sd_entry {
	enum i40e_sd_entry_type entry_type;
	bool valid;

	union {
		struct i40e_hmc_pd_table pd_table;
		struct i40e_hmc_bp bp;
	} u;
};

struct i40e_hmc_sd_table {
	struct i40e_virt_mem addr; /* used to track sd_entry allocations */
	u32 sd_cnt;
	u32 ref_cnt;
	struct i40e_hmc_sd_entry *sd_entry; /* (sd_cnt*512) entries max */
};

struct i40e_hmc_info {
	u32 signature;
	/* equals to pci func num for PF and dynamically allocated for VFs */
	u8 hmc_fn_id;
	u16 first_sd_index; /* index of the first available SD */

	/* hmc objects */
	struct i40e_hmc_obj_info *hmc_obj;
	struct i40e_virt_mem hmc_obj_virt_mem;
	struct i40e_hmc_sd_table sd_table;
};

#define I40E_INC_SD_REFCNT(sd_table)	((sd_table)->ref_cnt++)
#define I40E_INC_PD_REFCNT(pd_table)	((pd_table)->ref_cnt++)
#define I40E_INC_BP_REFCNT(bp)		((bp)->ref_cnt++)

#define I40E_DEC_SD_REFCNT(sd_table)	((sd_table)->ref_cnt--)
#define I40E_DEC_PD_REFCNT(pd_table)	((pd_table)->ref_cnt--)
#define I40E_DEC_BP_REFCNT(bp)		((bp)->ref_cnt--)

/**
 * I40E_SET_PF_SD_ENTRY - marks the sd entry as valid in the hardware
 * @hw: pointer to our hw struct
 * @pa: pointer to physical address
 * @sd_index: segment descriptor index
 * @type: if sd entry is direct or paged
 **/
#define I40E_SET_PF_SD_ENTRY(hw, pa, sd_index, type)			\
{									\
	u32 val1, val2, val3;						\
	val1 = (u32)(upper_32_bits(pa));				\
	val2 = (u32)(pa) | (I40E_HMC_MAX_BP_COUNT <<			\
		 I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT) |		\
		((((type) == I40E_SD_TYPE_PAGED) ? 0 : 1) <<		\
		I40E_PFHMC_SDDATALOW_PMSDTYPE_SHIFT) |			\
		BIT(I40E_PFHMC_SDDATALOW_PMSDVALID_SHIFT);		\
	val3 = (sd_index) | BIT_ULL(I40E_PFHMC_SDCMD_PMSDWR_SHIFT);	\
	wr32((hw), I40E_PFHMC_SDDATAHIGH, val1);			\
	wr32((hw), I40E_PFHMC_SDDATALOW, val2);				\
	wr32((hw), I40E_PFHMC_SDCMD, val3);				\
}

/**
 * I40E_CLEAR_PF_SD_ENTRY - marks the sd entry as invalid in the hardware
 * @hw: pointer to our hw struct
 * @sd_index: segment descriptor index
 * @type: if sd entry is direct or paged
 **/
#define I40E_CLEAR_PF_SD_ENTRY(hw, sd_index, type)			\
{									\
	u32 val2, val3;							\
	val2 = (I40E_HMC_MAX_BP_COUNT <<				\
		I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT) |		\
		((((type) == I40E_SD_TYPE_PAGED) ? 0 : 1) <<		\
		I40E_PFHMC_SDDATALOW_PMSDTYPE_SHIFT);			\
	val3 = (sd_index) | BIT_ULL(I40E_PFHMC_SDCMD_PMSDWR_SHIFT);	\
	wr32((hw), I40E_PFHMC_SDDATAHIGH, 0);				\
	wr32((hw), I40E_PFHMC_SDDATALOW, val2);				\
	wr32((hw), I40E_PFHMC_SDCMD, val3);				\
}

/**
 * I40E_INVALIDATE_PF_HMC_PD - Invalidates the pd cache in the hardware
 * @hw: pointer to our hw struct
 * @sd_idx: segment descriptor index
 * @pd_idx: page descriptor index
 **/
#define I40E_INVALIDATE_PF_HMC_PD(hw, sd_idx, pd_idx)			\
	wr32((hw), I40E_PFHMC_PDINV,					\
	    (((sd_idx) << I40E_PFHMC_PDINV_PMSDIDX_SHIFT) |		\
	     ((pd_idx) << I40E_PFHMC_PDINV_PMPDIDX_SHIFT)))

/**
 * I40E_FIND_SD_INDEX_LIMIT - finds segment descriptor index limit
 * @hmc_info: pointer to the HMC configuration information structure
 * @type: type of HMC resources we're searching
 * @index: starting index for the object
 * @cnt: number of objects we're trying to create
 * @sd_idx: pointer to return index of the segment descriptor in question
 * @sd_limit: pointer to return the maximum number of segment descriptors
 *
 * This function calculates the segment descriptor index and index limit
 * for the resource defined by i40e_hmc_rsrc_type.
 **/
#define I40E_FIND_SD_INDEX_LIMIT(hmc_info, type, index, cnt, sd_idx, sd_limit)\
{									\
	u64 fpm_addr, fpm_limit;					\
	fpm_addr = (hmc_info)->hmc_obj[(type)].base +			\
		   (hmc_info)->hmc_obj[(type)].size * (index);		\
	fpm_limit = fpm_addr + (hmc_info)->hmc_obj[(type)].size * (cnt);\
	*(sd_idx) = (u32)(fpm_addr / I40E_HMC_DIRECT_BP_SIZE);		\
	*(sd_limit) = (u32)((fpm_limit - 1) / I40E_HMC_DIRECT_BP_SIZE);	\
	/* add one more to the limit to correct our range */		\
	*(sd_limit) += 1;						\
}

/**
 * I40E_FIND_PD_INDEX_LIMIT - finds page descriptor index limit
 * @hmc_info: pointer to the HMC configuration information struct
 * @type: HMC resource type we're examining
 * @idx: starting index for the object
 * @cnt: number of objects we're trying to create
 * @pd_index: pointer to return page descriptor index
 * @pd_limit: pointer to return page descriptor index limit
 *
 * Calculates the page descriptor index and index limit for the resource
 * defined by i40e_hmc_rsrc_type.
 **/
#define I40E_FIND_PD_INDEX_LIMIT(hmc_info, type, idx, cnt, pd_index, pd_limit)\
{									\
	u64 fpm_adr, fpm_limit;						\
	fpm_adr = (hmc_info)->hmc_obj[(type)].base +			\
		  (hmc_info)->hmc_obj[(type)].size * (idx);		\
	fpm_limit = fpm_adr + (hmc_info)->hmc_obj[(type)].size * (cnt);	\
	*(pd_index) = (u32)(fpm_adr / I40E_HMC_PAGED_BP_SIZE);		\
	*(pd_limit) = (u32)((fpm_limit - 1) / I40E_HMC_PAGED_BP_SIZE);	\
	/* add one more to the limit to correct our range */		\
	*(pd_limit) += 1;						\
}

int i40e_add_sd_table_entry(struct i40e_hw *hw,
			    struct i40e_hmc_info *hmc_info,
			    u32 sd_index,
			    enum i40e_sd_entry_type type,
			    u64 direct_mode_sz);
int i40e_add_pd_table_entry(struct i40e_hw *hw,
			    struct i40e_hmc_info *hmc_info,
			    u32 pd_index,
			    struct i40e_dma_mem *rsrc_pg);
int i40e_remove_pd_bp(struct i40e_hw *hw,
		      struct i40e_hmc_info *hmc_info,
		      u32 idx);
int i40e_prep_remove_sd_bp(struct i40e_hmc_info *hmc_info,
			   u32 idx);
int i40e_remove_sd_bp_new(struct i40e_hw *hw,
			  struct i40e_hmc_info *hmc_info,
			  u32 idx, bool is_pf);
int i40e_prep_remove_pd_page(struct i40e_hmc_info *hmc_info,
			     u32 idx);
int i40e_remove_pd_page_new(struct i40e_hw *hw,
			    struct i40e_hmc_info *hmc_info,
			    u32 idx, bool is_pf);

#endif /* _I40E_HMC_H_ */
