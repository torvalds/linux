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

#include "i40iw_osdep.h"
#include "i40iw_register.h"
#include "i40iw_status.h"
#include "i40iw_hmc.h"
#include "i40iw_d.h"
#include "i40iw_type.h"
#include "i40iw_p.h"
#include "i40iw_vf.h"
#include "i40iw_virtchnl.h"

/**
 * i40iw_find_sd_index_limit - finds segment descriptor index limit
 * @hmc_info: pointer to the HMC configuration information structure
 * @type: type of HMC resources we're searching
 * @idx: starting index for the object
 * @cnt: number of objects we're trying to create
 * @sd_idx: pointer to return index of the segment descriptor in question
 * @sd_limit: pointer to return the maximum number of segment descriptors
 *
 * This function calculates the segment descriptor index and index limit
 * for the resource defined by i40iw_hmc_rsrc_type.
 */

static inline void i40iw_find_sd_index_limit(struct i40iw_hmc_info *hmc_info,
					     u32 type,
					     u32 idx,
					     u32 cnt,
					     u32 *sd_idx,
					     u32 *sd_limit)
{
	u64 fpm_addr, fpm_limit;

	fpm_addr = hmc_info->hmc_obj[(type)].base +
			hmc_info->hmc_obj[type].size * idx;
	fpm_limit = fpm_addr + hmc_info->hmc_obj[type].size * cnt;
	*sd_idx = (u32)(fpm_addr / I40IW_HMC_DIRECT_BP_SIZE);
	*sd_limit = (u32)((fpm_limit - 1) / I40IW_HMC_DIRECT_BP_SIZE);
	*sd_limit += 1;
}

/**
 * i40iw_find_pd_index_limit - finds page descriptor index limit
 * @hmc_info: pointer to the HMC configuration information struct
 * @type: HMC resource type we're examining
 * @idx: starting index for the object
 * @cnt: number of objects we're trying to create
 * @pd_idx: pointer to return page descriptor index
 * @pd_limit: pointer to return page descriptor index limit
 *
 * Calculates the page descriptor index and index limit for the resource
 * defined by i40iw_hmc_rsrc_type.
 */

static inline void i40iw_find_pd_index_limit(struct i40iw_hmc_info *hmc_info,
					     u32 type,
					     u32 idx,
					     u32 cnt,
					     u32 *pd_idx,
					     u32 *pd_limit)
{
	u64 fpm_adr, fpm_limit;

	fpm_adr = hmc_info->hmc_obj[type].base +
			hmc_info->hmc_obj[type].size * idx;
	fpm_limit = fpm_adr + (hmc_info)->hmc_obj[(type)].size * (cnt);
	*(pd_idx) = (u32)(fpm_adr / I40IW_HMC_PAGED_BP_SIZE);
	*(pd_limit) = (u32)((fpm_limit - 1) / I40IW_HMC_PAGED_BP_SIZE);
	*(pd_limit) += 1;
}

/**
 * i40iw_set_sd_entry - setup entry for sd programming
 * @pa: physical addr
 * @idx: sd index
 * @type: paged or direct sd
 * @entry: sd entry ptr
 */
static inline void i40iw_set_sd_entry(u64 pa,
				      u32 idx,
				      enum i40iw_sd_entry_type type,
				      struct update_sd_entry *entry)
{
	entry->data = pa | (I40IW_HMC_MAX_BP_COUNT << I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT) |
			(((type == I40IW_SD_TYPE_PAGED) ? 0 : 1) <<
				I40E_PFHMC_SDDATALOW_PMSDTYPE_SHIFT) |
			(1 << I40E_PFHMC_SDDATALOW_PMSDVALID_SHIFT);
	entry->cmd = (idx | (1 << I40E_PFHMC_SDCMD_PMSDWR_SHIFT) | (1 << 15));
}

/**
 * i40iw_clr_sd_entry - setup entry for sd clear
 * @idx: sd index
 * @type: paged or direct sd
 * @entry: sd entry ptr
 */
static inline void i40iw_clr_sd_entry(u32 idx, enum i40iw_sd_entry_type type,
				      struct update_sd_entry *entry)
{
	entry->data = (I40IW_HMC_MAX_BP_COUNT <<
			I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT) |
			(((type == I40IW_SD_TYPE_PAGED) ? 0 : 1) <<
				I40E_PFHMC_SDDATALOW_PMSDTYPE_SHIFT);
	entry->cmd = (idx | (1 << I40E_PFHMC_SDCMD_PMSDWR_SHIFT) | (1 << 15));
}

/**
 * i40iw_hmc_sd_one - setup 1 sd entry for cqp
 * @dev: pointer to the device structure
 * @hmc_fn_id: hmc's function id
 * @pa: physical addr
 * @sd_idx: sd index
 * @type: paged or direct sd
 * @setsd: flag to set or clear sd
 */
enum i40iw_status_code i40iw_hmc_sd_one(struct i40iw_sc_dev *dev,
					u8 hmc_fn_id,
					u64 pa, u32 sd_idx,
					enum i40iw_sd_entry_type type,
					bool setsd)
{
	struct i40iw_update_sds_info sdinfo;

	sdinfo.cnt = 1;
	sdinfo.hmc_fn_id = hmc_fn_id;
	if (setsd)
		i40iw_set_sd_entry(pa, sd_idx, type, sdinfo.entry);
	else
		i40iw_clr_sd_entry(sd_idx, type, sdinfo.entry);

	return dev->cqp->process_cqp_sds(dev, &sdinfo);
}

/**
 * i40iw_hmc_sd_grp - setup group od sd entries for cqp
 * @dev: pointer to the device structure
 * @hmc_info: pointer to the HMC configuration information struct
 * @sd_index: sd index
 * @sd_cnt: number of sd entries
 * @setsd: flag to set or clear sd
 */
static enum i40iw_status_code i40iw_hmc_sd_grp(struct i40iw_sc_dev *dev,
					       struct i40iw_hmc_info *hmc_info,
					       u32 sd_index,
					       u32 sd_cnt,
					       bool setsd)
{
	struct i40iw_hmc_sd_entry *sd_entry;
	struct i40iw_update_sds_info sdinfo;
	u64 pa;
	u32 i;
	enum i40iw_status_code ret_code = 0;

	memset(&sdinfo, 0, sizeof(sdinfo));
	sdinfo.hmc_fn_id = hmc_info->hmc_fn_id;
	for (i = sd_index; i < sd_index + sd_cnt; i++) {
		sd_entry = &hmc_info->sd_table.sd_entry[i];
		if (!sd_entry ||
		    (!sd_entry->valid && setsd) ||
		    (sd_entry->valid && !setsd))
			continue;
		if (setsd) {
			pa = (sd_entry->entry_type == I40IW_SD_TYPE_PAGED) ?
			    sd_entry->u.pd_table.pd_page_addr.pa :
			    sd_entry->u.bp.addr.pa;
			i40iw_set_sd_entry(pa, i, sd_entry->entry_type,
					   &sdinfo.entry[sdinfo.cnt]);
		} else {
			i40iw_clr_sd_entry(i, sd_entry->entry_type,
					   &sdinfo.entry[sdinfo.cnt]);
		}
		sdinfo.cnt++;
		if (sdinfo.cnt == I40IW_MAX_SD_ENTRIES) {
			ret_code = dev->cqp->process_cqp_sds(dev, &sdinfo);
			if (ret_code) {
				i40iw_debug(dev, I40IW_DEBUG_HMC,
					    "i40iw_hmc_sd_grp: sd_programming failed err=%d\n",
					    ret_code);
				return ret_code;
			}
			sdinfo.cnt = 0;
		}
	}
	if (sdinfo.cnt)
		ret_code = dev->cqp->process_cqp_sds(dev, &sdinfo);

	return ret_code;
}

/**
 * i40iw_vfdev_from_fpm - return vf dev ptr for hmc function id
 * @dev: pointer to the device structure
 * @hmc_fn_id: hmc's function id
 */
struct i40iw_vfdev *i40iw_vfdev_from_fpm(struct i40iw_sc_dev *dev, u8 hmc_fn_id)
{
	struct i40iw_vfdev *vf_dev = NULL;
	u16 idx;

	for (idx = 0; idx < I40IW_MAX_PE_ENABLED_VF_COUNT; idx++) {
		if (dev->vf_dev[idx] &&
		    ((u8)dev->vf_dev[idx]->pmf_index == hmc_fn_id)) {
			vf_dev = dev->vf_dev[idx];
			break;
		}
	}
	return vf_dev;
}

/**
 * i40iw_vf_hmcinfo_from_fpm - get ptr to hmc for func_id
 * @dev: pointer to the device structure
 * @hmc_fn_id: hmc's function id
 */
struct i40iw_hmc_info *i40iw_vf_hmcinfo_from_fpm(struct i40iw_sc_dev *dev,
						 u8 hmc_fn_id)
{
	struct i40iw_hmc_info *hmc_info = NULL;
	u16 idx;

	for (idx = 0; idx < I40IW_MAX_PE_ENABLED_VF_COUNT; idx++) {
		if (dev->vf_dev[idx] &&
		    ((u8)dev->vf_dev[idx]->pmf_index == hmc_fn_id)) {
			hmc_info = &dev->vf_dev[idx]->hmc_info;
			break;
		}
	}
	return hmc_info;
}

/**
 * i40iw_hmc_finish_add_sd_reg - program sd entries for objects
 * @dev: pointer to the device structure
 * @info: create obj info
 */
static enum i40iw_status_code i40iw_hmc_finish_add_sd_reg(struct i40iw_sc_dev *dev,
							  struct i40iw_hmc_create_obj_info *info)
{
	if (info->start_idx >= info->hmc_info->hmc_obj[info->rsrc_type].cnt)
		return I40IW_ERR_INVALID_HMC_OBJ_INDEX;

	if ((info->start_idx + info->count) >
			info->hmc_info->hmc_obj[info->rsrc_type].cnt)
		return I40IW_ERR_INVALID_HMC_OBJ_COUNT;

	if (!info->add_sd_cnt)
		return 0;

	return i40iw_hmc_sd_grp(dev, info->hmc_info,
				info->hmc_info->sd_indexes[0],
				info->add_sd_cnt, true);
}

/**
 * i40iw_sc_create_hmc_obj - allocate backing store for hmc objects
 * @dev: pointer to the device structure
 * @info: pointer to i40iw_hmc_iw_create_obj_info struct
 *
 * This will allocate memory for PDs and backing pages and populate
 * the sd and pd entries.
 */
enum i40iw_status_code i40iw_sc_create_hmc_obj(struct i40iw_sc_dev *dev,
					       struct i40iw_hmc_create_obj_info *info)
{
	struct i40iw_hmc_sd_entry *sd_entry;
	u32 sd_idx, sd_lmt;
	u32 pd_idx = 0, pd_lmt = 0;
	u32 pd_idx1 = 0, pd_lmt1 = 0;
	u32 i, j;
	bool pd_error = false;
	enum i40iw_status_code ret_code = 0;

	if (info->start_idx >= info->hmc_info->hmc_obj[info->rsrc_type].cnt)
		return I40IW_ERR_INVALID_HMC_OBJ_INDEX;

	if ((info->start_idx + info->count) >
	    info->hmc_info->hmc_obj[info->rsrc_type].cnt) {
		i40iw_debug(dev, I40IW_DEBUG_HMC,
			    "%s: error type %u, start = %u, req cnt %u, cnt = %u\n",
			    __func__, info->rsrc_type, info->start_idx, info->count,
			    info->hmc_info->hmc_obj[info->rsrc_type].cnt);
		return I40IW_ERR_INVALID_HMC_OBJ_COUNT;
	}

	if (!dev->is_pf)
		return i40iw_vchnl_vf_add_hmc_objs(dev, info->rsrc_type, 0, info->count);

	i40iw_find_sd_index_limit(info->hmc_info, info->rsrc_type,
				  info->start_idx, info->count,
				  &sd_idx, &sd_lmt);
	if (sd_idx >= info->hmc_info->sd_table.sd_cnt ||
	    sd_lmt > info->hmc_info->sd_table.sd_cnt) {
		return I40IW_ERR_INVALID_SD_INDEX;
	}
	i40iw_find_pd_index_limit(info->hmc_info, info->rsrc_type,
				  info->start_idx, info->count, &pd_idx, &pd_lmt);

	for (j = sd_idx; j < sd_lmt; j++) {
		ret_code = i40iw_add_sd_table_entry(dev->hw, info->hmc_info,
						    j,
						    info->entry_type,
						    I40IW_HMC_DIRECT_BP_SIZE);
		if (ret_code)
			goto exit_sd_error;
		sd_entry = &info->hmc_info->sd_table.sd_entry[j];

		if ((sd_entry->entry_type == I40IW_SD_TYPE_PAGED) &&
		    ((dev->hmc_info == info->hmc_info) &&
		     (info->rsrc_type != I40IW_HMC_IW_PBLE))) {
			pd_idx1 = max(pd_idx, (j * I40IW_HMC_MAX_BP_COUNT));
			pd_lmt1 = min(pd_lmt,
				      (j + 1) * I40IW_HMC_MAX_BP_COUNT);
			for (i = pd_idx1; i < pd_lmt1; i++) {
				/* update the pd table entry */
				ret_code = i40iw_add_pd_table_entry(dev->hw, info->hmc_info,
								    i, NULL);
				if (ret_code) {
					pd_error = true;
					break;
				}
			}
			if (pd_error) {
				while (i && (i > pd_idx1)) {
					i40iw_remove_pd_bp(dev->hw, info->hmc_info, (i - 1),
							   info->is_pf);
					i--;
				}
			}
		}
		if (sd_entry->valid)
			continue;

		info->hmc_info->sd_indexes[info->add_sd_cnt] = (u16)j;
		info->add_sd_cnt++;
		sd_entry->valid = true;
	}
	return i40iw_hmc_finish_add_sd_reg(dev, info);

exit_sd_error:
	while (j && (j > sd_idx)) {
		sd_entry = &info->hmc_info->sd_table.sd_entry[j - 1];
		switch (sd_entry->entry_type) {
		case I40IW_SD_TYPE_PAGED:
			pd_idx1 = max(pd_idx,
				      (j - 1) * I40IW_HMC_MAX_BP_COUNT);
			pd_lmt1 = min(pd_lmt, (j * I40IW_HMC_MAX_BP_COUNT));
			for (i = pd_idx1; i < pd_lmt1; i++)
				i40iw_prep_remove_pd_page(info->hmc_info, i);
			break;
		case I40IW_SD_TYPE_DIRECT:
			i40iw_prep_remove_pd_page(info->hmc_info, (j - 1));
			break;
		default:
			ret_code = I40IW_ERR_INVALID_SD_TYPE;
			break;
		}
		j--;
	}

	return ret_code;
}

/**
 * i40iw_finish_del_sd_reg - delete sd entries for objects
 * @dev: pointer to the device structure
 * @info: dele obj info
 * @reset: true if called before reset
 */
static enum i40iw_status_code i40iw_finish_del_sd_reg(struct i40iw_sc_dev *dev,
						      struct i40iw_hmc_del_obj_info *info,
						      bool reset)
{
	struct i40iw_hmc_sd_entry *sd_entry;
	enum i40iw_status_code ret_code = 0;
	u32 i, sd_idx;
	struct i40iw_dma_mem *mem;

	if (dev->is_pf && !reset)
		ret_code = i40iw_hmc_sd_grp(dev, info->hmc_info,
					    info->hmc_info->sd_indexes[0],
					    info->del_sd_cnt, false);

	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_HMC, "%s: error cqp sd sd_grp\n", __func__);

	for (i = 0; i < info->del_sd_cnt; i++) {
		sd_idx = info->hmc_info->sd_indexes[i];
		sd_entry = &info->hmc_info->sd_table.sd_entry[sd_idx];
		if (!sd_entry)
			continue;
		mem = (sd_entry->entry_type == I40IW_SD_TYPE_PAGED) ?
			&sd_entry->u.pd_table.pd_page_addr :
			&sd_entry->u.bp.addr;

		if (!mem || !mem->va)
			i40iw_debug(dev, I40IW_DEBUG_HMC, "%s: error cqp sd mem\n", __func__);
		else
			i40iw_free_dma_mem(dev->hw, mem);
	}
	return ret_code;
}

/**
 * i40iw_sc_del_hmc_obj - remove pe hmc objects
 * @dev: pointer to the device structure
 * @info: pointer to i40iw_hmc_del_obj_info struct
 * @reset: true if called before reset
 *
 * This will de-populate the SDs and PDs.  It frees
 * the memory for PDS and backing storage.  After this function is returned,
 * caller should deallocate memory allocated previously for
 * book-keeping information about PDs and backing storage.
 */
enum i40iw_status_code i40iw_sc_del_hmc_obj(struct i40iw_sc_dev *dev,
					    struct i40iw_hmc_del_obj_info *info,
					    bool reset)
{
	struct i40iw_hmc_pd_table *pd_table;
	u32 sd_idx, sd_lmt;
	u32 pd_idx, pd_lmt, rel_pd_idx;
	u32 i, j;
	enum i40iw_status_code ret_code = 0;

	if (info->start_idx >= info->hmc_info->hmc_obj[info->rsrc_type].cnt) {
		i40iw_debug(dev, I40IW_DEBUG_HMC,
			    "%s: error start_idx[%04d]  >= [type %04d].cnt[%04d]\n",
			    __func__, info->start_idx, info->rsrc_type,
			    info->hmc_info->hmc_obj[info->rsrc_type].cnt);
		return I40IW_ERR_INVALID_HMC_OBJ_INDEX;
	}

	if ((info->start_idx + info->count) >
	    info->hmc_info->hmc_obj[info->rsrc_type].cnt) {
		i40iw_debug(dev, I40IW_DEBUG_HMC,
			    "%s: error start_idx[%04d] + count %04d  >= [type %04d].cnt[%04d]\n",
			    __func__, info->start_idx, info->count,
			    info->rsrc_type,
			    info->hmc_info->hmc_obj[info->rsrc_type].cnt);
		return I40IW_ERR_INVALID_HMC_OBJ_COUNT;
	}
	if (!dev->is_pf) {
		ret_code = i40iw_vchnl_vf_del_hmc_obj(dev, info->rsrc_type, 0,
						      info->count);
		if (info->rsrc_type != I40IW_HMC_IW_PBLE)
			return ret_code;
	}

	i40iw_find_pd_index_limit(info->hmc_info, info->rsrc_type,
				  info->start_idx, info->count, &pd_idx, &pd_lmt);

	for (j = pd_idx; j < pd_lmt; j++) {
		sd_idx = j / I40IW_HMC_PD_CNT_IN_SD;

		if (info->hmc_info->sd_table.sd_entry[sd_idx].entry_type !=
		    I40IW_SD_TYPE_PAGED)
			continue;

		rel_pd_idx = j % I40IW_HMC_PD_CNT_IN_SD;
		pd_table = &info->hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
		if (pd_table->pd_entry[rel_pd_idx].valid) {
			ret_code = i40iw_remove_pd_bp(dev->hw, info->hmc_info, j,
						      info->is_pf);
			if (ret_code) {
				i40iw_debug(dev, I40IW_DEBUG_HMC, "%s: error\n", __func__);
				return ret_code;
			}
		}
	}

	i40iw_find_sd_index_limit(info->hmc_info, info->rsrc_type,
				  info->start_idx, info->count, &sd_idx, &sd_lmt);
	if (sd_idx >= info->hmc_info->sd_table.sd_cnt ||
	    sd_lmt > info->hmc_info->sd_table.sd_cnt) {
		i40iw_debug(dev, I40IW_DEBUG_HMC, "%s: error invalid sd_idx\n", __func__);
		return I40IW_ERR_INVALID_SD_INDEX;
	}

	for (i = sd_idx; i < sd_lmt; i++) {
		if (!info->hmc_info->sd_table.sd_entry[i].valid)
			continue;
		switch (info->hmc_info->sd_table.sd_entry[i].entry_type) {
		case I40IW_SD_TYPE_DIRECT:
			ret_code = i40iw_prep_remove_sd_bp(info->hmc_info, i);
			if (!ret_code) {
				info->hmc_info->sd_indexes[info->del_sd_cnt] = (u16)i;
				info->del_sd_cnt++;
			}
			break;
		case I40IW_SD_TYPE_PAGED:
			ret_code = i40iw_prep_remove_pd_page(info->hmc_info, i);
			if (!ret_code) {
				info->hmc_info->sd_indexes[info->del_sd_cnt] = (u16)i;
				info->del_sd_cnt++;
			}
			break;
		default:
			break;
		}
	}
	return i40iw_finish_del_sd_reg(dev, info, reset);
}

/**
 * i40iw_add_sd_table_entry - Adds a segment descriptor to the table
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information struct
 * @sd_index: segment descriptor index to manipulate
 * @type: what type of segment descriptor we're manipulating
 * @direct_mode_sz: size to alloc in direct mode
 */
enum i40iw_status_code i40iw_add_sd_table_entry(struct i40iw_hw *hw,
						struct i40iw_hmc_info *hmc_info,
						u32 sd_index,
						enum i40iw_sd_entry_type type,
						u64 direct_mode_sz)
{
	enum i40iw_status_code ret_code = 0;
	struct i40iw_hmc_sd_entry *sd_entry;
	bool dma_mem_alloc_done = false;
	struct i40iw_dma_mem mem;
	u64 alloc_len;

	sd_entry = &hmc_info->sd_table.sd_entry[sd_index];
	if (!sd_entry->valid) {
		if (type == I40IW_SD_TYPE_PAGED)
			alloc_len = I40IW_HMC_PAGED_BP_SIZE;
		else
			alloc_len = direct_mode_sz;

		/* allocate a 4K pd page or 2M backing page */
		ret_code = i40iw_allocate_dma_mem(hw, &mem, alloc_len,
						  I40IW_HMC_PD_BP_BUF_ALIGNMENT);
		if (ret_code)
			goto exit;
		dma_mem_alloc_done = true;
		if (type == I40IW_SD_TYPE_PAGED) {
			ret_code = i40iw_allocate_virt_mem(hw,
							   &sd_entry->u.pd_table.pd_entry_virt_mem,
							   sizeof(struct i40iw_hmc_pd_entry) * 512);
			if (ret_code)
				goto exit;
			sd_entry->u.pd_table.pd_entry = (struct i40iw_hmc_pd_entry *)
							 sd_entry->u.pd_table.pd_entry_virt_mem.va;

			memcpy(&sd_entry->u.pd_table.pd_page_addr, &mem, sizeof(struct i40iw_dma_mem));
		} else {
			memcpy(&sd_entry->u.bp.addr, &mem, sizeof(struct i40iw_dma_mem));
			sd_entry->u.bp.sd_pd_index = sd_index;
		}

		hmc_info->sd_table.sd_entry[sd_index].entry_type = type;

		I40IW_INC_SD_REFCNT(&hmc_info->sd_table);
	}
	if (sd_entry->entry_type == I40IW_SD_TYPE_DIRECT)
		I40IW_INC_BP_REFCNT(&sd_entry->u.bp);
exit:
	if (ret_code)
		if (dma_mem_alloc_done)
			i40iw_free_dma_mem(hw, &mem);

	return ret_code;
}

/**
 * i40iw_add_pd_table_entry - Adds page descriptor to the specified table
 * @hw: pointer to our HW structure
 * @hmc_info: pointer to the HMC configuration information structure
 * @pd_index: which page descriptor index to manipulate
 * @rsrc_pg: if not NULL, use preallocated page instead of allocating new one.
 *
 * This function:
 *	1. Initializes the pd entry
 *	2. Adds pd_entry in the pd_table
 *	3. Mark the entry valid in i40iw_hmc_pd_entry structure
 *	4. Initializes the pd_entry's ref count to 1
 * assumptions:
 *	1. The memory for pd should be pinned down, physically contiguous and
 *	   aligned on 4K boundary and zeroed memory.
 *	2. It should be 4K in size.
 */
enum i40iw_status_code i40iw_add_pd_table_entry(struct i40iw_hw *hw,
						struct i40iw_hmc_info *hmc_info,
						u32 pd_index,
						struct i40iw_dma_mem *rsrc_pg)
{
	enum i40iw_status_code ret_code = 0;
	struct i40iw_hmc_pd_table *pd_table;
	struct i40iw_hmc_pd_entry *pd_entry;
	struct i40iw_dma_mem mem;
	struct i40iw_dma_mem *page = &mem;
	u32 sd_idx, rel_pd_idx;
	u64 *pd_addr;
	u64 page_desc;

	if (pd_index / I40IW_HMC_PD_CNT_IN_SD >= hmc_info->sd_table.sd_cnt)
		return I40IW_ERR_INVALID_PAGE_DESC_INDEX;

	sd_idx = (pd_index / I40IW_HMC_PD_CNT_IN_SD);
	if (hmc_info->sd_table.sd_entry[sd_idx].entry_type != I40IW_SD_TYPE_PAGED)
		return 0;

	rel_pd_idx = (pd_index % I40IW_HMC_PD_CNT_IN_SD);
	pd_table = &hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
	pd_entry = &pd_table->pd_entry[rel_pd_idx];
	if (!pd_entry->valid) {
		if (rsrc_pg) {
			pd_entry->rsrc_pg = true;
			page = rsrc_pg;
		} else {
			ret_code = i40iw_allocate_dma_mem(hw, page,
							  I40IW_HMC_PAGED_BP_SIZE,
							  I40IW_HMC_PD_BP_BUF_ALIGNMENT);
			if (ret_code)
				return ret_code;
			pd_entry->rsrc_pg = false;
		}

		memcpy(&pd_entry->bp.addr, page, sizeof(struct i40iw_dma_mem));
		pd_entry->bp.sd_pd_index = pd_index;
		pd_entry->bp.entry_type = I40IW_SD_TYPE_PAGED;
		page_desc = page->pa | 0x1;

		pd_addr = (u64 *)pd_table->pd_page_addr.va;
		pd_addr += rel_pd_idx;

		memcpy(pd_addr, &page_desc, sizeof(*pd_addr));

		pd_entry->sd_index = sd_idx;
		pd_entry->valid = true;
		I40IW_INC_PD_REFCNT(pd_table);
		if (hmc_info->hmc_fn_id < I40IW_FIRST_VF_FPM_ID)
			I40IW_INVALIDATE_PF_HMC_PD(hw, sd_idx, rel_pd_idx);
		else if (hw->hmc.hmc_fn_id != hmc_info->hmc_fn_id)
			I40IW_INVALIDATE_VF_HMC_PD(hw, sd_idx, rel_pd_idx,
						   hmc_info->hmc_fn_id);
	}
	I40IW_INC_BP_REFCNT(&pd_entry->bp);

	return 0;
}

/**
 * i40iw_remove_pd_bp - remove a backing page from a page descriptor
 * @hw: pointer to our HW structure
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 * @is_pf: distinguishes a VF from a PF
 *
 * This function:
 *	1. Marks the entry in pd table (for paged address mode) or in sd table
 *	   (for direct address mode) invalid.
 *	2. Write to register PMPDINV to invalidate the backing page in FV cache
 *	3. Decrement the ref count for the pd _entry
 * assumptions:
 *	1. Caller can deallocate the memory used by backing storage after this
 *	   function returns.
 */
enum i40iw_status_code i40iw_remove_pd_bp(struct i40iw_hw *hw,
					  struct i40iw_hmc_info *hmc_info,
					  u32 idx,
					  bool is_pf)
{
	struct i40iw_hmc_pd_entry *pd_entry;
	struct i40iw_hmc_pd_table *pd_table;
	struct i40iw_hmc_sd_entry *sd_entry;
	u32 sd_idx, rel_pd_idx;
	struct i40iw_dma_mem *mem;
	u64 *pd_addr;

	sd_idx = idx / I40IW_HMC_PD_CNT_IN_SD;
	rel_pd_idx = idx % I40IW_HMC_PD_CNT_IN_SD;
	if (sd_idx >= hmc_info->sd_table.sd_cnt)
		return I40IW_ERR_INVALID_PAGE_DESC_INDEX;

	sd_entry = &hmc_info->sd_table.sd_entry[sd_idx];
	if (sd_entry->entry_type != I40IW_SD_TYPE_PAGED)
		return I40IW_ERR_INVALID_SD_TYPE;

	pd_table = &hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
	pd_entry = &pd_table->pd_entry[rel_pd_idx];
	I40IW_DEC_BP_REFCNT(&pd_entry->bp);
	if (pd_entry->bp.ref_cnt)
		return 0;

	pd_entry->valid = false;
	I40IW_DEC_PD_REFCNT(pd_table);
	pd_addr = (u64 *)pd_table->pd_page_addr.va;
	pd_addr += rel_pd_idx;
	memset(pd_addr, 0, sizeof(u64));
	if (is_pf)
		I40IW_INVALIDATE_PF_HMC_PD(hw, sd_idx, idx);
	else
		I40IW_INVALIDATE_VF_HMC_PD(hw, sd_idx, idx,
					   hmc_info->hmc_fn_id);

	if (!pd_entry->rsrc_pg) {
		mem = &pd_entry->bp.addr;
		if (!mem || !mem->va)
			return I40IW_ERR_PARAM;
		i40iw_free_dma_mem(hw, mem);
	}
	if (!pd_table->ref_cnt)
		i40iw_free_virt_mem(hw, &pd_table->pd_entry_virt_mem);

	return 0;
}

/**
 * i40iw_prep_remove_sd_bp - Prepares to remove a backing page from a sd entry
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 */
enum i40iw_status_code i40iw_prep_remove_sd_bp(struct i40iw_hmc_info *hmc_info, u32 idx)
{
	struct i40iw_hmc_sd_entry *sd_entry;

	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40IW_DEC_BP_REFCNT(&sd_entry->u.bp);
	if (sd_entry->u.bp.ref_cnt)
		return I40IW_ERR_NOT_READY;

	I40IW_DEC_SD_REFCNT(&hmc_info->sd_table);
	sd_entry->valid = false;

	return 0;
}

/**
 * i40iw_prep_remove_pd_page - Prepares to remove a PD page from sd entry.
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: segment descriptor index to find the relevant page descriptor
 */
enum i40iw_status_code i40iw_prep_remove_pd_page(struct i40iw_hmc_info *hmc_info,
						 u32 idx)
{
	struct i40iw_hmc_sd_entry *sd_entry;

	sd_entry = &hmc_info->sd_table.sd_entry[idx];

	if (sd_entry->u.pd_table.ref_cnt)
		return I40IW_ERR_NOT_READY;

	sd_entry->valid = false;
	I40IW_DEC_SD_REFCNT(&hmc_info->sd_table);

	return 0;
}

/**
 * i40iw_pf_init_vfhmc -
 * @vf_cnt_array: array of cnt values of iwarp hmc objects
 * @vf_hmc_fn_id: hmc function id ofr vf driver
 * @dev: pointer to i40iw_dev struct
 *
 * Called by pf driver to initialize hmc_info for vf driver instance.
 */
enum i40iw_status_code i40iw_pf_init_vfhmc(struct i40iw_sc_dev *dev,
					   u8 vf_hmc_fn_id,
					   u32 *vf_cnt_array)
{
	struct i40iw_hmc_info *hmc_info;
	enum i40iw_status_code ret_code = 0;
	u32 i;

	if ((vf_hmc_fn_id < I40IW_FIRST_VF_FPM_ID) ||
	    (vf_hmc_fn_id >= I40IW_FIRST_VF_FPM_ID +
	     I40IW_MAX_PE_ENABLED_VF_COUNT)) {
		i40iw_debug(dev, I40IW_DEBUG_HMC, "%s: invalid vf_hmc_fn_id  0x%x\n",
			    __func__, vf_hmc_fn_id);
		return I40IW_ERR_INVALID_HMCFN_ID;
	}

	ret_code = i40iw_sc_init_iw_hmc(dev, vf_hmc_fn_id);
	if (ret_code)
		return ret_code;

	hmc_info = i40iw_vf_hmcinfo_from_fpm(dev, vf_hmc_fn_id);

	for (i = I40IW_HMC_IW_QP; i < I40IW_HMC_IW_MAX; i++)
		if (vf_cnt_array)
			hmc_info->hmc_obj[i].cnt =
			    vf_cnt_array[i - I40IW_HMC_IW_QP];
		else
			hmc_info->hmc_obj[i].cnt = hmc_info->hmc_obj[i].max_cnt;

	return 0;
}
