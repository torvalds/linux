// SPDX-License-Identifier: GPL-2.0
/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "i40e_osdep.h"
#include "i40e_register.h"
#include "i40e_status.h"
#include "i40e_alloc.h"
#include "i40e_hmc.h"
#include "i40e_type.h"

/**
 * i40e_add_sd_table_entry - Adds a segment descriptor to the table
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information struct
 * @sd_index: segment descriptor index to manipulate
 * @type: what type of segment descriptor we're manipulating
 * @direct_mode_sz: size to alloc in direct mode
 **/
i40e_status i40e_add_sd_table_entry(struct i40e_hw *hw,
					      struct i40e_hmc_info *hmc_info,
					      u32 sd_index,
					      enum i40e_sd_entry_type type,
					      u64 direct_mode_sz)
{
	enum i40e_memory_type mem_type __attribute__((unused));
	struct i40e_hmc_sd_entry *sd_entry;
	bool dma_mem_alloc_done = false;
	struct i40e_dma_mem mem;
	i40e_status ret_code = I40E_SUCCESS;
	u64 alloc_len;

	if (NULL == hmc_info->sd_table.sd_entry) {
		ret_code = I40E_ERR_BAD_PTR;
		hw_dbg(hw, "i40e_add_sd_table_entry: bad sd_entry\n");
		goto exit;
	}

	if (sd_index >= hmc_info->sd_table.sd_cnt) {
		ret_code = I40E_ERR_INVALID_SD_INDEX;
		hw_dbg(hw, "i40e_add_sd_table_entry: bad sd_index\n");
		goto exit;
	}

	sd_entry = &hmc_info->sd_table.sd_entry[sd_index];
	if (!sd_entry->valid) {
		if (I40E_SD_TYPE_PAGED == type) {
			mem_type = i40e_mem_pd;
			alloc_len = I40E_HMC_PAGED_BP_SIZE;
		} else {
			mem_type = i40e_mem_bp_jumbo;
			alloc_len = direct_mode_sz;
		}

		/* allocate a 4K pd page or 2M backing page */
		ret_code = i40e_allocate_dma_mem(hw, &mem, mem_type, alloc_len,
						 I40E_HMC_PD_BP_BUF_ALIGNMENT);
		if (ret_code)
			goto exit;
		dma_mem_alloc_done = true;
		if (I40E_SD_TYPE_PAGED == type) {
			ret_code = i40e_allocate_virt_mem(hw,
					&sd_entry->u.pd_table.pd_entry_virt_mem,
					sizeof(struct i40e_hmc_pd_entry) * 512);
			if (ret_code)
				goto exit;
			sd_entry->u.pd_table.pd_entry =
				(struct i40e_hmc_pd_entry *)
				sd_entry->u.pd_table.pd_entry_virt_mem.va;
			sd_entry->u.pd_table.pd_page_addr = mem;
		} else {
			sd_entry->u.bp.addr = mem;
			sd_entry->u.bp.sd_pd_index = sd_index;
		}
		/* initialize the sd entry */
		hmc_info->sd_table.sd_entry[sd_index].entry_type = type;

		/* increment the ref count */
		I40E_INC_SD_REFCNT(&hmc_info->sd_table);
	}
	/* Increment backing page reference count */
	if (I40E_SD_TYPE_DIRECT == sd_entry->entry_type)
		I40E_INC_BP_REFCNT(&sd_entry->u.bp);
exit:
	if (ret_code)
		if (dma_mem_alloc_done)
			i40e_free_dma_mem(hw, &mem);

	return ret_code;
}

/**
 * i40e_add_pd_table_entry - Adds page descriptor to the specified table
 * @hw: pointer to our HW structure
 * @hmc_info: pointer to the HMC configuration information structure
 * @pd_index: which page descriptor index to manipulate
 * @rsrc_pg: if not NULL, use preallocated page instead of allocating new one.
 *
 * This function:
 *	1. Initializes the pd entry
 *	2. Adds pd_entry in the pd_table
 *	3. Mark the entry valid in i40e_hmc_pd_entry structure
 *	4. Initializes the pd_entry's ref count to 1
 * assumptions:
 *	1. The memory for pd should be pinned down, physically contiguous and
 *	   aligned on 4K boundary and zeroed memory.
 *	2. It should be 4K in size.
 **/
i40e_status i40e_add_pd_table_entry(struct i40e_hw *hw,
					      struct i40e_hmc_info *hmc_info,
					      u32 pd_index,
					      struct i40e_dma_mem *rsrc_pg)
{
	i40e_status ret_code = 0;
	struct i40e_hmc_pd_table *pd_table;
	struct i40e_hmc_pd_entry *pd_entry;
	struct i40e_dma_mem mem;
	struct i40e_dma_mem *page = &mem;
	u32 sd_idx, rel_pd_idx;
	u64 *pd_addr;
	u64 page_desc;

	if (pd_index / I40E_HMC_PD_CNT_IN_SD >= hmc_info->sd_table.sd_cnt) {
		ret_code = I40E_ERR_INVALID_PAGE_DESC_INDEX;
		hw_dbg(hw, "i40e_add_pd_table_entry: bad pd_index\n");
		goto exit;
	}

	/* find corresponding sd */
	sd_idx = (pd_index / I40E_HMC_PD_CNT_IN_SD);
	if (I40E_SD_TYPE_PAGED !=
	    hmc_info->sd_table.sd_entry[sd_idx].entry_type)
		goto exit;

	rel_pd_idx = (pd_index % I40E_HMC_PD_CNT_IN_SD);
	pd_table = &hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
	pd_entry = &pd_table->pd_entry[rel_pd_idx];
	if (!pd_entry->valid) {
		if (rsrc_pg) {
			pd_entry->rsrc_pg = true;
			page = rsrc_pg;
		} else {
			/* allocate a 4K backing page */
			ret_code = i40e_allocate_dma_mem(hw, page, i40e_mem_bp,
						I40E_HMC_PAGED_BP_SIZE,
						I40E_HMC_PD_BP_BUF_ALIGNMENT);
			if (ret_code)
				goto exit;
			pd_entry->rsrc_pg = false;
		}

		pd_entry->bp.addr = *page;
		pd_entry->bp.sd_pd_index = pd_index;
		pd_entry->bp.entry_type = I40E_SD_TYPE_PAGED;
		/* Set page address and valid bit */
		page_desc = page->pa | 0x1;

		pd_addr = (u64 *)pd_table->pd_page_addr.va;
		pd_addr += rel_pd_idx;

		/* Add the backing page physical address in the pd entry */
		memcpy(pd_addr, &page_desc, sizeof(u64));

		pd_entry->sd_index = sd_idx;
		pd_entry->valid = true;
		I40E_INC_PD_REFCNT(pd_table);
	}
	I40E_INC_BP_REFCNT(&pd_entry->bp);
exit:
	return ret_code;
}

/**
 * i40e_remove_pd_bp - remove a backing page from a page descriptor
 * @hw: pointer to our HW structure
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 * @is_pf: distinguishes a VF from a PF
 *
 * This function:
 *	1. Marks the entry in pd tabe (for paged address mode) or in sd table
 *	   (for direct address mode) invalid.
 *	2. Write to register PMPDINV to invalidate the backing page in FV cache
 *	3. Decrement the ref count for the pd _entry
 * assumptions:
 *	1. Caller can deallocate the memory used by backing storage after this
 *	   function returns.
 **/
i40e_status i40e_remove_pd_bp(struct i40e_hw *hw,
					struct i40e_hmc_info *hmc_info,
					u32 idx)
{
	i40e_status ret_code = 0;
	struct i40e_hmc_pd_entry *pd_entry;
	struct i40e_hmc_pd_table *pd_table;
	struct i40e_hmc_sd_entry *sd_entry;
	u32 sd_idx, rel_pd_idx;
	u64 *pd_addr;

	/* calculate index */
	sd_idx = idx / I40E_HMC_PD_CNT_IN_SD;
	rel_pd_idx = idx % I40E_HMC_PD_CNT_IN_SD;
	if (sd_idx >= hmc_info->sd_table.sd_cnt) {
		ret_code = I40E_ERR_INVALID_PAGE_DESC_INDEX;
		hw_dbg(hw, "i40e_remove_pd_bp: bad idx\n");
		goto exit;
	}
	sd_entry = &hmc_info->sd_table.sd_entry[sd_idx];
	if (I40E_SD_TYPE_PAGED != sd_entry->entry_type) {
		ret_code = I40E_ERR_INVALID_SD_TYPE;
		hw_dbg(hw, "i40e_remove_pd_bp: wrong sd_entry type\n");
		goto exit;
	}
	/* get the entry and decrease its ref counter */
	pd_table = &hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
	pd_entry = &pd_table->pd_entry[rel_pd_idx];
	I40E_DEC_BP_REFCNT(&pd_entry->bp);
	if (pd_entry->bp.ref_cnt)
		goto exit;

	/* mark the entry invalid */
	pd_entry->valid = false;
	I40E_DEC_PD_REFCNT(pd_table);
	pd_addr = (u64 *)pd_table->pd_page_addr.va;
	pd_addr += rel_pd_idx;
	memset(pd_addr, 0, sizeof(u64));
	I40E_INVALIDATE_PF_HMC_PD(hw, sd_idx, idx);

	/* free memory here */
	if (!pd_entry->rsrc_pg)
		ret_code = i40e_free_dma_mem(hw, &pd_entry->bp.addr);
	if (ret_code)
		goto exit;
	if (!pd_table->ref_cnt)
		i40e_free_virt_mem(hw, &pd_table->pd_entry_virt_mem);
exit:
	return ret_code;
}

/**
 * i40e_prep_remove_sd_bp - Prepares to remove a backing page from a sd entry
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 **/
i40e_status i40e_prep_remove_sd_bp(struct i40e_hmc_info *hmc_info,
					     u32 idx)
{
	i40e_status ret_code = 0;
	struct i40e_hmc_sd_entry *sd_entry;

	/* get the entry and decrease its ref counter */
	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40E_DEC_BP_REFCNT(&sd_entry->u.bp);
	if (sd_entry->u.bp.ref_cnt) {
		ret_code = I40E_ERR_NOT_READY;
		goto exit;
	}
	I40E_DEC_SD_REFCNT(&hmc_info->sd_table);

	/* mark the entry invalid */
	sd_entry->valid = false;
exit:
	return ret_code;
}

/**
 * i40e_remove_sd_bp_new - Removes a backing page from a segment descriptor
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 * @is_pf: used to distinguish between VF and PF
 **/
i40e_status i40e_remove_sd_bp_new(struct i40e_hw *hw,
					    struct i40e_hmc_info *hmc_info,
					    u32 idx, bool is_pf)
{
	struct i40e_hmc_sd_entry *sd_entry;

	if (!is_pf)
		return I40E_NOT_SUPPORTED;

	/* get the entry and decrease its ref counter */
	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40E_CLEAR_PF_SD_ENTRY(hw, idx, I40E_SD_TYPE_DIRECT);

	return i40e_free_dma_mem(hw, &sd_entry->u.bp.addr);
}

/**
 * i40e_prep_remove_pd_page - Prepares to remove a PD page from sd entry.
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: segment descriptor index to find the relevant page descriptor
 **/
i40e_status i40e_prep_remove_pd_page(struct i40e_hmc_info *hmc_info,
					       u32 idx)
{
	i40e_status ret_code = 0;
	struct i40e_hmc_sd_entry *sd_entry;

	sd_entry = &hmc_info->sd_table.sd_entry[idx];

	if (sd_entry->u.pd_table.ref_cnt) {
		ret_code = I40E_ERR_NOT_READY;
		goto exit;
	}

	/* mark the entry invalid */
	sd_entry->valid = false;

	I40E_DEC_SD_REFCNT(&hmc_info->sd_table);
exit:
	return ret_code;
}

/**
 * i40e_remove_pd_page_new - Removes a PD page from sd entry.
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: segment descriptor index to find the relevant page descriptor
 * @is_pf: used to distinguish between VF and PF
 **/
i40e_status i40e_remove_pd_page_new(struct i40e_hw *hw,
					      struct i40e_hmc_info *hmc_info,
					      u32 idx, bool is_pf)
{
	struct i40e_hmc_sd_entry *sd_entry;

	if (!is_pf)
		return I40E_NOT_SUPPORTED;

	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40E_CLEAR_PF_SD_ENTRY(hw, idx, I40E_SD_TYPE_PAGED);

	return  i40e_free_dma_mem(hw, &sd_entry->u.pd_table.pd_page_addr);
}
