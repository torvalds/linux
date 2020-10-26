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

#include "i40iw_status.h"
#include "i40iw_osdep.h"
#include "i40iw_register.h"
#include "i40iw_hmc.h"

#include "i40iw_d.h"
#include "i40iw_type.h"
#include "i40iw_p.h"

#include <linux/pci.h>
#include <linux/genalloc.h>
#include <linux/vmalloc.h>
#include "i40iw_pble.h"
#include "i40iw.h"

struct i40iw_device;
static enum i40iw_status_code add_pble_pool(struct i40iw_sc_dev *dev,
					    struct i40iw_hmc_pble_rsrc *pble_rsrc);
static void i40iw_free_vmalloc_mem(struct i40iw_hw *hw, struct i40iw_chunk *chunk);

/**
 * i40iw_destroy_pble_pool - destroy pool during module unload
 * @pble_rsrc:	pble resources
 */
void i40iw_destroy_pble_pool(struct i40iw_sc_dev *dev, struct i40iw_hmc_pble_rsrc *pble_rsrc)
{
	struct list_head *clist;
	struct list_head *tlist;
	struct i40iw_chunk *chunk;
	struct i40iw_pble_pool *pinfo = &pble_rsrc->pinfo;

	if (pinfo->pool) {
		list_for_each_safe(clist, tlist, &pinfo->clist) {
			chunk = list_entry(clist, struct i40iw_chunk, list);
			if (chunk->type == I40IW_VMALLOC)
				i40iw_free_vmalloc_mem(dev->hw, chunk);
			kfree(chunk);
		}
		gen_pool_destroy(pinfo->pool);
	}
}

/**
 * i40iw_hmc_init_pble - Initialize pble resources during module load
 * @dev: i40iw_sc_dev struct
 * @pble_rsrc:	pble resources
 */
enum i40iw_status_code i40iw_hmc_init_pble(struct i40iw_sc_dev *dev,
					   struct i40iw_hmc_pble_rsrc *pble_rsrc)
{
	struct i40iw_hmc_info *hmc_info;
	u32 fpm_idx = 0;

	hmc_info = dev->hmc_info;
	pble_rsrc->fpm_base_addr = hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].base;
	/* Now start the pble' on 4k boundary */
	if (pble_rsrc->fpm_base_addr & 0xfff)
		fpm_idx = (PAGE_SIZE - (pble_rsrc->fpm_base_addr & 0xfff)) >> 3;

	pble_rsrc->unallocated_pble =
	    hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt - fpm_idx;
	pble_rsrc->next_fpm_addr = pble_rsrc->fpm_base_addr + (fpm_idx << 3);

	pble_rsrc->pinfo.pool_shift = POOL_SHIFT;
	pble_rsrc->pinfo.pool = gen_pool_create(pble_rsrc->pinfo.pool_shift, -1);
	INIT_LIST_HEAD(&pble_rsrc->pinfo.clist);
	if (!pble_rsrc->pinfo.pool)
		goto error;

	if (add_pble_pool(dev, pble_rsrc))
		goto error;

	return 0;

 error:i40iw_destroy_pble_pool(dev, pble_rsrc);
	return I40IW_ERR_NO_MEMORY;
}

/**
 * get_sd_pd_idx -  Returns sd index, pd index and rel_pd_idx from fpm address
 * @ pble_rsrc:	structure containing fpm address
 * @ idx: where to return indexes
 */
static inline void get_sd_pd_idx(struct i40iw_hmc_pble_rsrc *pble_rsrc,
				 struct sd_pd_idx *idx)
{
	idx->sd_idx = (u32)(pble_rsrc->next_fpm_addr) / I40IW_HMC_DIRECT_BP_SIZE;
	idx->pd_idx = (u32)(pble_rsrc->next_fpm_addr) / I40IW_HMC_PAGED_BP_SIZE;
	idx->rel_pd_idx = (idx->pd_idx % I40IW_HMC_PD_CNT_IN_SD);
}

/**
 * add_sd_direct - add sd direct for pble
 * @dev: hardware control device structure
 * @pble_rsrc: pble resource ptr
 * @info: page info for sd
 */
static enum i40iw_status_code add_sd_direct(struct i40iw_sc_dev *dev,
					    struct i40iw_hmc_pble_rsrc *pble_rsrc,
					    struct i40iw_add_page_info *info)
{
	enum i40iw_status_code ret_code = 0;
	struct sd_pd_idx *idx = &info->idx;
	struct i40iw_chunk *chunk = info->chunk;
	struct i40iw_hmc_info *hmc_info = info->hmc_info;
	struct i40iw_hmc_sd_entry *sd_entry = info->sd_entry;
	u32 offset = 0;

	if (!sd_entry->valid) {
		if (dev->is_pf) {
			ret_code = i40iw_add_sd_table_entry(dev->hw, hmc_info,
							    info->idx.sd_idx,
							    I40IW_SD_TYPE_DIRECT,
							    I40IW_HMC_DIRECT_BP_SIZE);
			if (ret_code)
				return ret_code;
			chunk->type = I40IW_DMA_COHERENT;
		}
	}
	offset = idx->rel_pd_idx << I40IW_HMC_PAGED_BP_SHIFT;
	chunk->size = info->pages << I40IW_HMC_PAGED_BP_SHIFT;
	chunk->vaddr = ((u8 *)sd_entry->u.bp.addr.va + offset);
	chunk->fpm_addr = pble_rsrc->next_fpm_addr;
	i40iw_debug(dev, I40IW_DEBUG_PBLE, "chunk_size[%d] = 0x%x vaddr=%p fpm_addr = %llx\n",
		    chunk->size, chunk->size, chunk->vaddr, chunk->fpm_addr);
	return 0;
}

/**
 * i40iw_free_vmalloc_mem - free vmalloc during close
 * @hw: hw struct
 * @chunk: chunk information for vmalloc
 */
static void i40iw_free_vmalloc_mem(struct i40iw_hw *hw, struct i40iw_chunk *chunk)
{
	struct pci_dev *pcidev = hw->pcidev;
	int i;

	if (!chunk->pg_cnt)
		goto done;
	for (i = 0; i < chunk->pg_cnt; i++)
		dma_unmap_page(&pcidev->dev, chunk->dmaaddrs[i], PAGE_SIZE, DMA_BIDIRECTIONAL);

 done:
	kfree(chunk->dmaaddrs);
	chunk->dmaaddrs = NULL;
	vfree(chunk->vaddr);
	chunk->vaddr = NULL;
	chunk->type = 0;
}

/**
 * i40iw_get_vmalloc_mem - get 2M page for sd
 * @hw: hardware address
 * @chunk: chunk to adf
 * @pg_cnt: #of 4 K pages
 */
static enum i40iw_status_code i40iw_get_vmalloc_mem(struct i40iw_hw *hw,
						    struct i40iw_chunk *chunk,
						    int pg_cnt)
{
	struct pci_dev *pcidev = hw->pcidev;
	struct page *page;
	u8 *addr;
	u32 size;
	int i;

	chunk->dmaaddrs = kzalloc(pg_cnt << 3, GFP_KERNEL);
	if (!chunk->dmaaddrs)
		return I40IW_ERR_NO_MEMORY;
	size = PAGE_SIZE * pg_cnt;
	chunk->vaddr = vmalloc(size);
	if (!chunk->vaddr) {
		kfree(chunk->dmaaddrs);
		chunk->dmaaddrs = NULL;
		return I40IW_ERR_NO_MEMORY;
	}
	chunk->size = size;
	addr = (u8 *)chunk->vaddr;
	for (i = 0; i < pg_cnt; i++) {
		page = vmalloc_to_page((void *)addr);
		if (!page)
			break;
		chunk->dmaaddrs[i] = dma_map_page(&pcidev->dev, page, 0,
						  PAGE_SIZE, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(&pcidev->dev, chunk->dmaaddrs[i]))
			break;
		addr += PAGE_SIZE;
	}

	chunk->pg_cnt = i;
	chunk->type = I40IW_VMALLOC;
	if (i == pg_cnt)
		return 0;

	i40iw_free_vmalloc_mem(hw, chunk);
	return I40IW_ERR_NO_MEMORY;
}

/**
 * fpm_to_idx - given fpm address, get pble index
 * @pble_rsrc: pble resource management
 * @addr: fpm address for index
 */
static inline u32 fpm_to_idx(struct i40iw_hmc_pble_rsrc *pble_rsrc, u64 addr)
{
	return (addr - (pble_rsrc->fpm_base_addr)) >> 3;
}

/**
 * add_bp_pages - add backing pages for sd
 * @dev: hardware control device structure
 * @pble_rsrc: pble resource management
 * @info: page info for sd
 */
static enum i40iw_status_code add_bp_pages(struct i40iw_sc_dev *dev,
					   struct i40iw_hmc_pble_rsrc *pble_rsrc,
					   struct i40iw_add_page_info *info)
{
	u8 *addr;
	struct i40iw_dma_mem mem;
	struct i40iw_hmc_pd_entry *pd_entry;
	struct i40iw_hmc_sd_entry *sd_entry = info->sd_entry;
	struct i40iw_hmc_info *hmc_info = info->hmc_info;
	struct i40iw_chunk *chunk = info->chunk;
	struct i40iw_manage_vf_pble_info vf_pble_info;
	enum i40iw_status_code status = 0;
	u32 rel_pd_idx = info->idx.rel_pd_idx;
	u32 pd_idx = info->idx.pd_idx;
	u32 i;

	status = i40iw_get_vmalloc_mem(dev->hw, chunk, info->pages);
	if (status)
		return I40IW_ERR_NO_MEMORY;
	status = i40iw_add_sd_table_entry(dev->hw, hmc_info,
					  info->idx.sd_idx, I40IW_SD_TYPE_PAGED,
					  I40IW_HMC_DIRECT_BP_SIZE);
	if (status)
		goto error;
	if (!dev->is_pf) {
		status = i40iw_vchnl_vf_add_hmc_objs(dev, I40IW_HMC_IW_PBLE,
						     fpm_to_idx(pble_rsrc,
								pble_rsrc->next_fpm_addr),
						     (info->pages << PBLE_512_SHIFT));
		if (status) {
			i40iw_pr_err("allocate PBLEs in the PF.  Error %i\n", status);
			goto error;
		}
	}
	addr = chunk->vaddr;
	for (i = 0; i < info->pages; i++) {
		mem.pa = chunk->dmaaddrs[i];
		mem.size = PAGE_SIZE;
		mem.va = (void *)(addr);
		pd_entry = &sd_entry->u.pd_table.pd_entry[rel_pd_idx++];
		if (!pd_entry->valid) {
			status = i40iw_add_pd_table_entry(dev->hw, hmc_info, pd_idx++, &mem);
			if (status)
				goto error;
			addr += PAGE_SIZE;
		} else {
			i40iw_pr_err("pd entry is valid expecting to be invalid\n");
		}
	}
	if (!dev->is_pf) {
		vf_pble_info.first_pd_index = info->idx.rel_pd_idx;
		vf_pble_info.inv_pd_ent = false;
		vf_pble_info.pd_entry_cnt = PBLE_PER_PAGE;
		vf_pble_info.pd_pl_pba = sd_entry->u.pd_table.pd_page_addr.pa;
		vf_pble_info.sd_index = info->idx.sd_idx;
		status = i40iw_hw_manage_vf_pble_bp(dev->back_dev,
						    &vf_pble_info, true);
		if (status) {
			i40iw_pr_err("CQP manage VF PBLE BP failed.  %i\n", status);
			goto error;
		}
	}
	chunk->fpm_addr = pble_rsrc->next_fpm_addr;
	return 0;
error:
	i40iw_free_vmalloc_mem(dev->hw, chunk);
	return status;
}

/**
 * add_pble_pool - add a sd entry for pble resoure
 * @dev: hardware control device structure
 * @pble_rsrc: pble resource management
 */
static enum i40iw_status_code add_pble_pool(struct i40iw_sc_dev *dev,
					    struct i40iw_hmc_pble_rsrc *pble_rsrc)
{
	struct i40iw_hmc_sd_entry *sd_entry;
	struct i40iw_hmc_info *hmc_info;
	struct i40iw_chunk *chunk;
	struct i40iw_add_page_info info;
	struct sd_pd_idx *idx = &info.idx;
	enum i40iw_status_code ret_code = 0;
	enum i40iw_sd_entry_type sd_entry_type;
	u64 sd_reg_val = 0;
	u32 pages;

	if (pble_rsrc->unallocated_pble < PBLE_PER_PAGE)
		return I40IW_ERR_NO_MEMORY;
	if (pble_rsrc->next_fpm_addr & 0xfff) {
		i40iw_pr_err("next fpm_addr %llx\n", pble_rsrc->next_fpm_addr);
		return I40IW_ERR_INVALID_PAGE_DESC_INDEX;
	}
	chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
	if (!chunk)
		return I40IW_ERR_NO_MEMORY;
	hmc_info = dev->hmc_info;
	chunk->fpm_addr = pble_rsrc->next_fpm_addr;
	get_sd_pd_idx(pble_rsrc, idx);
	sd_entry = &hmc_info->sd_table.sd_entry[idx->sd_idx];
	pages = (idx->rel_pd_idx) ? (I40IW_HMC_PD_CNT_IN_SD -
			idx->rel_pd_idx) : I40IW_HMC_PD_CNT_IN_SD;
	pages = min(pages, pble_rsrc->unallocated_pble >> PBLE_512_SHIFT);
	info.chunk = chunk;
	info.hmc_info = hmc_info;
	info.pages = pages;
	info.sd_entry = sd_entry;
	if (!sd_entry->valid) {
		sd_entry_type = (!idx->rel_pd_idx &&
				 (pages == I40IW_HMC_PD_CNT_IN_SD) &&
				 dev->is_pf) ? I40IW_SD_TYPE_DIRECT : I40IW_SD_TYPE_PAGED;
	} else {
		sd_entry_type = sd_entry->entry_type;
	}
	i40iw_debug(dev, I40IW_DEBUG_PBLE,
		    "pages = %d, unallocated_pble[%u] current_fpm_addr = %llx\n",
		    pages, pble_rsrc->unallocated_pble, pble_rsrc->next_fpm_addr);
	i40iw_debug(dev, I40IW_DEBUG_PBLE, "sd_entry_type = %d sd_entry valid = %d\n",
		    sd_entry_type, sd_entry->valid);

	if (sd_entry_type == I40IW_SD_TYPE_DIRECT)
		ret_code = add_sd_direct(dev, pble_rsrc, &info);
	if (ret_code)
		sd_entry_type = I40IW_SD_TYPE_PAGED;
	else
		pble_rsrc->stats_direct_sds++;

	if (sd_entry_type == I40IW_SD_TYPE_PAGED) {
		ret_code = add_bp_pages(dev, pble_rsrc, &info);
		if (ret_code)
			goto error;
		else
			pble_rsrc->stats_paged_sds++;
	}

	if (gen_pool_add_virt(pble_rsrc->pinfo.pool, (unsigned long)chunk->vaddr,
			      (phys_addr_t)chunk->fpm_addr, chunk->size, -1)) {
		i40iw_pr_err("could not allocate memory by gen_pool_addr_virt()\n");
		ret_code = I40IW_ERR_NO_MEMORY;
		goto error;
	}
	pble_rsrc->next_fpm_addr += chunk->size;
	i40iw_debug(dev, I40IW_DEBUG_PBLE, "next_fpm_addr = %llx chunk_size[%u] = 0x%x\n",
		    pble_rsrc->next_fpm_addr, chunk->size, chunk->size);
	pble_rsrc->unallocated_pble -= (chunk->size >> 3);
	list_add(&chunk->list, &pble_rsrc->pinfo.clist);
	sd_reg_val = (sd_entry_type == I40IW_SD_TYPE_PAGED) ?
			sd_entry->u.pd_table.pd_page_addr.pa : sd_entry->u.bp.addr.pa;
	if (sd_entry->valid)
		return 0;
	if (dev->is_pf) {
		ret_code = i40iw_hmc_sd_one(dev, hmc_info->hmc_fn_id,
					    sd_reg_val, idx->sd_idx,
					    sd_entry->entry_type, true);
		if (ret_code) {
			i40iw_pr_err("cqp cmd failed for sd (pbles)\n");
			goto error;
		}
	}

	sd_entry->valid = true;
	return 0;
 error:
	kfree(chunk);
	return ret_code;
}

/**
 * free_lvl2 - fee level 2 pble
 * @pble_rsrc: pble resource management
 * @palloc: level 2 pble allocation
 */
static void free_lvl2(struct i40iw_hmc_pble_rsrc *pble_rsrc,
		      struct i40iw_pble_alloc *palloc)
{
	u32 i;
	struct gen_pool *pool;
	struct i40iw_pble_level2 *lvl2 = &palloc->level2;
	struct i40iw_pble_info *root = &lvl2->root;
	struct i40iw_pble_info *leaf = lvl2->leaf;

	pool = pble_rsrc->pinfo.pool;

	for (i = 0; i < lvl2->leaf_cnt; i++, leaf++) {
		if (leaf->addr)
			gen_pool_free(pool, leaf->addr, (leaf->cnt << 3));
		else
			break;
	}

	if (root->addr)
		gen_pool_free(pool, root->addr, (root->cnt << 3));

	kfree(lvl2->leaf);
	lvl2->leaf = NULL;
}

/**
 * get_lvl2_pble - get level 2 pble resource
 * @pble_rsrc: pble resource management
 * @palloc: level 2 pble allocation
 * @pool: pool pointer
 */
static enum i40iw_status_code get_lvl2_pble(struct i40iw_hmc_pble_rsrc *pble_rsrc,
					    struct i40iw_pble_alloc *palloc,
					    struct gen_pool *pool)
{
	u32 lf4k, lflast, total, i;
	u32 pblcnt = PBLE_PER_PAGE;
	u64 *addr;
	struct i40iw_pble_level2 *lvl2 = &palloc->level2;
	struct i40iw_pble_info *root = &lvl2->root;
	struct i40iw_pble_info *leaf;

	/* number of full 512 (4K) leafs) */
	lf4k = palloc->total_cnt >> 9;
	lflast = palloc->total_cnt % PBLE_PER_PAGE;
	total = (lflast == 0) ? lf4k : lf4k + 1;
	lvl2->leaf_cnt = total;

	leaf = kzalloc((sizeof(*leaf) * total), GFP_ATOMIC);
	if (!leaf)
		return I40IW_ERR_NO_MEMORY;
	lvl2->leaf = leaf;
	/* allocate pbles for the root */
	root->addr = gen_pool_alloc(pool, (total << 3));
	if (!root->addr) {
		kfree(lvl2->leaf);
		lvl2->leaf = NULL;
		return I40IW_ERR_NO_MEMORY;
	}
	root->idx = fpm_to_idx(pble_rsrc,
			       (u64)gen_pool_virt_to_phys(pool, root->addr));
	root->cnt = total;
	addr = (u64 *)root->addr;
	for (i = 0; i < total; i++, leaf++) {
		pblcnt = (lflast && ((i + 1) == total)) ? lflast : PBLE_PER_PAGE;
		leaf->addr = gen_pool_alloc(pool, (pblcnt << 3));
		if (!leaf->addr)
			goto error;
		leaf->idx = fpm_to_idx(pble_rsrc, (u64)gen_pool_virt_to_phys(pool, leaf->addr));

		leaf->cnt = pblcnt;
		*addr = (u64)leaf->idx;
		addr++;
	}
	palloc->level = I40IW_LEVEL_2;
	pble_rsrc->stats_lvl2++;
	return 0;
 error:
	free_lvl2(pble_rsrc, palloc);
	return I40IW_ERR_NO_MEMORY;
}

/**
 * get_lvl1_pble - get level 1 pble resource
 * @dev: hardware control device structure
 * @pble_rsrc: pble resource management
 * @palloc: level 1 pble allocation
 */
static enum i40iw_status_code get_lvl1_pble(struct i40iw_sc_dev *dev,
					    struct i40iw_hmc_pble_rsrc *pble_rsrc,
					    struct i40iw_pble_alloc *palloc)
{
	u64 *addr;
	struct gen_pool *pool;
	struct i40iw_pble_info *lvl1 = &palloc->level1;

	pool = pble_rsrc->pinfo.pool;
	addr = (u64 *)gen_pool_alloc(pool, (palloc->total_cnt << 3));

	if (!addr)
		return I40IW_ERR_NO_MEMORY;

	palloc->level = I40IW_LEVEL_1;
	lvl1->addr = (unsigned long)addr;
	lvl1->idx = fpm_to_idx(pble_rsrc, (u64)gen_pool_virt_to_phys(pool,
			       (unsigned long)addr));
	lvl1->cnt = palloc->total_cnt;
	pble_rsrc->stats_lvl1++;
	return 0;
}

/**
 * get_lvl1_lvl2_pble - calls get_lvl1 and get_lvl2 pble routine
 * @dev: i40iw_sc_dev struct
 * @pble_rsrc:	pble resources
 * @palloc: contains all inforamtion regarding pble (idx + pble addr)
 * @pool: pointer to general purpose special memory pool descriptor
 */
static inline enum i40iw_status_code get_lvl1_lvl2_pble(struct i40iw_sc_dev *dev,
							struct i40iw_hmc_pble_rsrc *pble_rsrc,
							struct i40iw_pble_alloc *palloc,
							struct gen_pool *pool)
{
	enum i40iw_status_code status = 0;

	status = get_lvl1_pble(dev, pble_rsrc, palloc);
	if (status && (palloc->total_cnt > PBLE_PER_PAGE))
		status = get_lvl2_pble(pble_rsrc, palloc, pool);
	return status;
}

/**
 * i40iw_get_pble - allocate pbles from the pool
 * @dev: i40iw_sc_dev struct
 * @pble_rsrc:	pble resources
 * @palloc: contains all inforamtion regarding pble (idx + pble addr)
 * @pble_cnt: #of pbles requested
 */
enum i40iw_status_code i40iw_get_pble(struct i40iw_sc_dev *dev,
				      struct i40iw_hmc_pble_rsrc *pble_rsrc,
				      struct i40iw_pble_alloc *palloc,
				      u32 pble_cnt)
{
	struct gen_pool *pool;
	enum i40iw_status_code status = 0;
	u32 max_sds = 0;
	int i;

	pool = pble_rsrc->pinfo.pool;
	palloc->total_cnt = pble_cnt;
	palloc->level = I40IW_LEVEL_0;
	/*check first to see if we can get pble's without acquiring additional sd's */
	status = get_lvl1_lvl2_pble(dev, pble_rsrc, palloc, pool);
	if (!status)
		goto exit;
	max_sds = (palloc->total_cnt >> 18) + 1;
	for (i = 0; i < max_sds; i++) {
		status = add_pble_pool(dev, pble_rsrc);
		if (status)
			break;
		status = get_lvl1_lvl2_pble(dev, pble_rsrc, palloc, pool);
		if (!status)
			break;
	}
exit:
	if (!status)
		pble_rsrc->stats_alloc_ok++;
	else
		pble_rsrc->stats_alloc_fail++;

	return status;
}

/**
 * i40iw_free_pble - put pbles back into pool
 * @pble_rsrc:	pble resources
 * @palloc: contains all inforamtion regarding pble resource being freed
 */
void i40iw_free_pble(struct i40iw_hmc_pble_rsrc *pble_rsrc,
		     struct i40iw_pble_alloc *palloc)
{
	struct gen_pool *pool;

	pool = pble_rsrc->pinfo.pool;
	if (palloc->level == I40IW_LEVEL_2)
		free_lvl2(pble_rsrc, palloc);
	else
		gen_pool_free(pool, palloc->level1.addr,
			      (palloc->level1.cnt << 3));
	pble_rsrc->stats_alloc_freed++;
}
