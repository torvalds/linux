// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "osdep.h"
#include "status.h"
#include "hmc.h"
#include "defs.h"
#include "type.h"
#include "protos.h"
#include "pble.h"

static enum irdma_status_code
add_pble_prm(struct irdma_hmc_pble_rsrc *pble_rsrc);

/**
 * irdma_destroy_pble_prm - destroy prm during module unload
 * @pble_rsrc: pble resources
 */
void irdma_destroy_pble_prm(struct irdma_hmc_pble_rsrc *pble_rsrc)
{
	struct irdma_chunk *chunk;
	struct irdma_pble_prm *pinfo = &pble_rsrc->pinfo;

	while (!list_empty(&pinfo->clist)) {
		chunk = (struct irdma_chunk *) pinfo->clist.next;
		list_del(&chunk->list);
		if (chunk->type == PBLE_SD_PAGED)
			irdma_pble_free_paged_mem(chunk);
		bitmap_free(chunk->bitmapbuf);
		kfree(chunk->chunkmem.va);
	}
}

/**
 * irdma_hmc_init_pble - Initialize pble resources during module load
 * @dev: irdma_sc_dev struct
 * @pble_rsrc: pble resources
 */
enum irdma_status_code
irdma_hmc_init_pble(struct irdma_sc_dev *dev,
		    struct irdma_hmc_pble_rsrc *pble_rsrc)
{
	struct irdma_hmc_info *hmc_info;
	u32 fpm_idx = 0;
	enum irdma_status_code status = 0;

	hmc_info = dev->hmc_info;
	pble_rsrc->dev = dev;
	pble_rsrc->fpm_base_addr = hmc_info->hmc_obj[IRDMA_HMC_IW_PBLE].base;
	/* Start pble' on 4k boundary */
	if (pble_rsrc->fpm_base_addr & 0xfff)
		fpm_idx = (4096 - (pble_rsrc->fpm_base_addr & 0xfff)) >> 3;
	pble_rsrc->unallocated_pble =
		hmc_info->hmc_obj[IRDMA_HMC_IW_PBLE].cnt - fpm_idx;
	pble_rsrc->next_fpm_addr = pble_rsrc->fpm_base_addr + (fpm_idx << 3);
	pble_rsrc->pinfo.pble_shift = PBLE_SHIFT;

	mutex_init(&pble_rsrc->pble_mutex_lock);

	spin_lock_init(&pble_rsrc->pinfo.prm_lock);
	INIT_LIST_HEAD(&pble_rsrc->pinfo.clist);
	if (add_pble_prm(pble_rsrc)) {
		irdma_destroy_pble_prm(pble_rsrc);
		status = IRDMA_ERR_NO_MEMORY;
	}

	return status;
}

/**
 * get_sd_pd_idx -  Returns sd index, pd index and rel_pd_idx from fpm address
 * @pble_rsrc: structure containing fpm address
 * @idx: where to return indexes
 */
static void get_sd_pd_idx(struct irdma_hmc_pble_rsrc *pble_rsrc,
			  struct sd_pd_idx *idx)
{
	idx->sd_idx = (u32)pble_rsrc->next_fpm_addr / IRDMA_HMC_DIRECT_BP_SIZE;
	idx->pd_idx = (u32)(pble_rsrc->next_fpm_addr / IRDMA_HMC_PAGED_BP_SIZE);
	idx->rel_pd_idx = (idx->pd_idx % IRDMA_HMC_PD_CNT_IN_SD);
}

/**
 * add_sd_direct - add sd direct for pble
 * @pble_rsrc: pble resource ptr
 * @info: page info for sd
 */
static enum irdma_status_code
add_sd_direct(struct irdma_hmc_pble_rsrc *pble_rsrc,
	      struct irdma_add_page_info *info)
{
	struct irdma_sc_dev *dev = pble_rsrc->dev;
	enum irdma_status_code ret_code = 0;
	struct sd_pd_idx *idx = &info->idx;
	struct irdma_chunk *chunk = info->chunk;
	struct irdma_hmc_info *hmc_info = info->hmc_info;
	struct irdma_hmc_sd_entry *sd_entry = info->sd_entry;
	u32 offset = 0;

	if (!sd_entry->valid) {
		ret_code = irdma_add_sd_table_entry(dev->hw, hmc_info,
						    info->idx.sd_idx,
						    IRDMA_SD_TYPE_DIRECT,
						    IRDMA_HMC_DIRECT_BP_SIZE);
		if (ret_code)
			return ret_code;

		chunk->type = PBLE_SD_CONTIGOUS;
	}

	offset = idx->rel_pd_idx << HMC_PAGED_BP_SHIFT;
	chunk->size = info->pages << HMC_PAGED_BP_SHIFT;
	chunk->vaddr = sd_entry->u.bp.addr.va + offset;
	chunk->fpm_addr = pble_rsrc->next_fpm_addr;
	ibdev_dbg(to_ibdev(dev),
		  "PBLE: chunk_size[%lld] = 0x%llx vaddr=0x%pK fpm_addr = %llx\n",
		  chunk->size, chunk->size, chunk->vaddr, chunk->fpm_addr);

	return 0;
}

/**
 * fpm_to_idx - given fpm address, get pble index
 * @pble_rsrc: pble resource management
 * @addr: fpm address for index
 */
static u32 fpm_to_idx(struct irdma_hmc_pble_rsrc *pble_rsrc, u64 addr)
{
	u64 idx;

	idx = (addr - (pble_rsrc->fpm_base_addr)) >> 3;

	return (u32)idx;
}

/**
 * add_bp_pages - add backing pages for sd
 * @pble_rsrc: pble resource management
 * @info: page info for sd
 */
static enum irdma_status_code
add_bp_pages(struct irdma_hmc_pble_rsrc *pble_rsrc,
	     struct irdma_add_page_info *info)
{
	struct irdma_sc_dev *dev = pble_rsrc->dev;
	u8 *addr;
	struct irdma_dma_mem mem;
	struct irdma_hmc_pd_entry *pd_entry;
	struct irdma_hmc_sd_entry *sd_entry = info->sd_entry;
	struct irdma_hmc_info *hmc_info = info->hmc_info;
	struct irdma_chunk *chunk = info->chunk;
	enum irdma_status_code status = 0;
	u32 rel_pd_idx = info->idx.rel_pd_idx;
	u32 pd_idx = info->idx.pd_idx;
	u32 i;

	if (irdma_pble_get_paged_mem(chunk, info->pages))
		return IRDMA_ERR_NO_MEMORY;

	status = irdma_add_sd_table_entry(dev->hw, hmc_info, info->idx.sd_idx,
					  IRDMA_SD_TYPE_PAGED,
					  IRDMA_HMC_DIRECT_BP_SIZE);
	if (status)
		goto error;

	addr = chunk->vaddr;
	for (i = 0; i < info->pages; i++) {
		mem.pa = (u64)chunk->dmainfo.dmaaddrs[i];
		mem.size = 4096;
		mem.va = addr;
		pd_entry = &sd_entry->u.pd_table.pd_entry[rel_pd_idx++];
		if (!pd_entry->valid) {
			status = irdma_add_pd_table_entry(dev, hmc_info,
							  pd_idx++, &mem);
			if (status)
				goto error;

			addr += 4096;
		}
	}

	chunk->fpm_addr = pble_rsrc->next_fpm_addr;
	return 0;

error:
	irdma_pble_free_paged_mem(chunk);

	return status;
}

/**
 * irdma_get_type - add a sd entry type for sd
 * @dev: irdma_sc_dev struct
 * @idx: index of sd
 * @pages: pages in the sd
 */
static enum irdma_sd_entry_type irdma_get_type(struct irdma_sc_dev *dev,
					       struct sd_pd_idx *idx, u32 pages)
{
	enum irdma_sd_entry_type sd_entry_type;

	sd_entry_type = !idx->rel_pd_idx && pages == IRDMA_HMC_PD_CNT_IN_SD ?
			IRDMA_SD_TYPE_DIRECT : IRDMA_SD_TYPE_PAGED;
	return sd_entry_type;
}

/**
 * add_pble_prm - add a sd entry for pble resoure
 * @pble_rsrc: pble resource management
 */
static enum irdma_status_code
add_pble_prm(struct irdma_hmc_pble_rsrc *pble_rsrc)
{
	struct irdma_sc_dev *dev = pble_rsrc->dev;
	struct irdma_hmc_sd_entry *sd_entry;
	struct irdma_hmc_info *hmc_info;
	struct irdma_chunk *chunk;
	struct irdma_add_page_info info;
	struct sd_pd_idx *idx = &info.idx;
	enum irdma_status_code ret_code = 0;
	enum irdma_sd_entry_type sd_entry_type;
	u64 sd_reg_val = 0;
	struct irdma_virt_mem chunkmem;
	u32 pages;

	if (pble_rsrc->unallocated_pble < PBLE_PER_PAGE)
		return IRDMA_ERR_NO_MEMORY;

	if (pble_rsrc->next_fpm_addr & 0xfff)
		return IRDMA_ERR_INVALID_PAGE_DESC_INDEX;

	chunkmem.size = sizeof(*chunk);
	chunkmem.va = kzalloc(chunkmem.size, GFP_KERNEL);
	if (!chunkmem.va)
		return IRDMA_ERR_NO_MEMORY;

	chunk = chunkmem.va;
	chunk->chunkmem = chunkmem;
	hmc_info = dev->hmc_info;
	chunk->dev = dev;
	chunk->fpm_addr = pble_rsrc->next_fpm_addr;
	get_sd_pd_idx(pble_rsrc, idx);
	sd_entry = &hmc_info->sd_table.sd_entry[idx->sd_idx];
	pages = (idx->rel_pd_idx) ? (IRDMA_HMC_PD_CNT_IN_SD - idx->rel_pd_idx) :
				    IRDMA_HMC_PD_CNT_IN_SD;
	pages = min(pages, pble_rsrc->unallocated_pble >> PBLE_512_SHIFT);
	info.chunk = chunk;
	info.hmc_info = hmc_info;
	info.pages = pages;
	info.sd_entry = sd_entry;
	if (!sd_entry->valid)
		sd_entry_type = irdma_get_type(dev, idx, pages);
	else
		sd_entry_type = sd_entry->entry_type;

	ibdev_dbg(to_ibdev(dev),
		  "PBLE: pages = %d, unallocated_pble[%d] current_fpm_addr = %llx\n",
		  pages, pble_rsrc->unallocated_pble,
		  pble_rsrc->next_fpm_addr);
	ibdev_dbg(to_ibdev(dev), "PBLE: sd_entry_type = %d\n", sd_entry_type);
	if (sd_entry_type == IRDMA_SD_TYPE_DIRECT)
		ret_code = add_sd_direct(pble_rsrc, &info);

	if (ret_code)
		sd_entry_type = IRDMA_SD_TYPE_PAGED;
	else
		pble_rsrc->stats_direct_sds++;

	if (sd_entry_type == IRDMA_SD_TYPE_PAGED) {
		ret_code = add_bp_pages(pble_rsrc, &info);
		if (ret_code)
			goto error;
		else
			pble_rsrc->stats_paged_sds++;
	}

	ret_code = irdma_prm_add_pble_mem(&pble_rsrc->pinfo, chunk);
	if (ret_code)
		goto error;

	pble_rsrc->next_fpm_addr += chunk->size;
	ibdev_dbg(to_ibdev(dev),
		  "PBLE: next_fpm_addr = %llx chunk_size[%llu] = 0x%llx\n",
		  pble_rsrc->next_fpm_addr, chunk->size, chunk->size);
	pble_rsrc->unallocated_pble -= (u32)(chunk->size >> 3);
	sd_reg_val = (sd_entry_type == IRDMA_SD_TYPE_PAGED) ?
			     sd_entry->u.pd_table.pd_page_addr.pa :
			     sd_entry->u.bp.addr.pa;

	if (!sd_entry->valid) {
		ret_code = irdma_hmc_sd_one(dev, hmc_info->hmc_fn_id, sd_reg_val,
					    idx->sd_idx, sd_entry->entry_type, true);
		if (ret_code)
			goto error;
	}

	list_add(&chunk->list, &pble_rsrc->pinfo.clist);
	sd_entry->valid = true;
	return 0;

error:
	bitmap_free(chunk->bitmapbuf);
	kfree(chunk->chunkmem.va);

	return ret_code;
}

/**
 * free_lvl2 - fee level 2 pble
 * @pble_rsrc: pble resource management
 * @palloc: level 2 pble allocation
 */
static void free_lvl2(struct irdma_hmc_pble_rsrc *pble_rsrc,
		      struct irdma_pble_alloc *palloc)
{
	u32 i;
	struct irdma_pble_level2 *lvl2 = &palloc->level2;
	struct irdma_pble_info *root = &lvl2->root;
	struct irdma_pble_info *leaf = lvl2->leaf;

	for (i = 0; i < lvl2->leaf_cnt; i++, leaf++) {
		if (leaf->addr)
			irdma_prm_return_pbles(&pble_rsrc->pinfo,
					       &leaf->chunkinfo);
		else
			break;
	}

	if (root->addr)
		irdma_prm_return_pbles(&pble_rsrc->pinfo, &root->chunkinfo);

	kfree(lvl2->leafmem.va);
	lvl2->leaf = NULL;
}

/**
 * get_lvl2_pble - get level 2 pble resource
 * @pble_rsrc: pble resource management
 * @palloc: level 2 pble allocation
 */
static enum irdma_status_code
get_lvl2_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
	      struct irdma_pble_alloc *palloc)
{
	u32 lf4k, lflast, total, i;
	u32 pblcnt = PBLE_PER_PAGE;
	u64 *addr;
	struct irdma_pble_level2 *lvl2 = &palloc->level2;
	struct irdma_pble_info *root = &lvl2->root;
	struct irdma_pble_info *leaf;
	enum irdma_status_code ret_code;
	u64 fpm_addr;

	/* number of full 512 (4K) leafs) */
	lf4k = palloc->total_cnt >> 9;
	lflast = palloc->total_cnt % PBLE_PER_PAGE;
	total = (lflast == 0) ? lf4k : lf4k + 1;
	lvl2->leaf_cnt = total;

	lvl2->leafmem.size = (sizeof(*leaf) * total);
	lvl2->leafmem.va = kzalloc(lvl2->leafmem.size, GFP_KERNEL);
	if (!lvl2->leafmem.va)
		return IRDMA_ERR_NO_MEMORY;

	lvl2->leaf = lvl2->leafmem.va;
	leaf = lvl2->leaf;
	ret_code = irdma_prm_get_pbles(&pble_rsrc->pinfo, &root->chunkinfo,
				       total << 3, &root->addr, &fpm_addr);
	if (ret_code) {
		kfree(lvl2->leafmem.va);
		lvl2->leaf = NULL;
		return IRDMA_ERR_NO_MEMORY;
	}

	root->idx = fpm_to_idx(pble_rsrc, fpm_addr);
	root->cnt = total;
	addr = root->addr;
	for (i = 0; i < total; i++, leaf++) {
		pblcnt = (lflast && ((i + 1) == total)) ?
				lflast : PBLE_PER_PAGE;
		ret_code = irdma_prm_get_pbles(&pble_rsrc->pinfo,
					       &leaf->chunkinfo, pblcnt << 3,
					       &leaf->addr, &fpm_addr);
		if (ret_code)
			goto error;

		leaf->idx = fpm_to_idx(pble_rsrc, fpm_addr);

		leaf->cnt = pblcnt;
		*addr = (u64)leaf->idx;
		addr++;
	}

	palloc->level = PBLE_LEVEL_2;
	pble_rsrc->stats_lvl2++;
	return 0;

error:
	free_lvl2(pble_rsrc, palloc);

	return IRDMA_ERR_NO_MEMORY;
}

/**
 * get_lvl1_pble - get level 1 pble resource
 * @pble_rsrc: pble resource management
 * @palloc: level 1 pble allocation
 */
static enum irdma_status_code
get_lvl1_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
	      struct irdma_pble_alloc *palloc)
{
	enum irdma_status_code ret_code;
	u64 fpm_addr;
	struct irdma_pble_info *lvl1 = &palloc->level1;

	ret_code = irdma_prm_get_pbles(&pble_rsrc->pinfo, &lvl1->chunkinfo,
				       palloc->total_cnt << 3, &lvl1->addr,
				       &fpm_addr);
	if (ret_code)
		return IRDMA_ERR_NO_MEMORY;

	palloc->level = PBLE_LEVEL_1;
	lvl1->idx = fpm_to_idx(pble_rsrc, fpm_addr);
	lvl1->cnt = palloc->total_cnt;
	pble_rsrc->stats_lvl1++;

	return 0;
}

/**
 * get_lvl1_lvl2_pble - calls get_lvl1 and get_lvl2 pble routine
 * @pble_rsrc: pble resources
 * @palloc: contains all inforamtion regarding pble (idx + pble addr)
 * @level1_only: flag for a level 1 PBLE
 */
static enum irdma_status_code
get_lvl1_lvl2_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
		   struct irdma_pble_alloc *palloc, bool level1_only)
{
	enum irdma_status_code status = 0;

	status = get_lvl1_pble(pble_rsrc, palloc);
	if (!status || level1_only || palloc->total_cnt <= PBLE_PER_PAGE)
		return status;

	status = get_lvl2_pble(pble_rsrc, palloc);

	return status;
}

/**
 * irdma_get_pble - allocate pbles from the prm
 * @pble_rsrc: pble resources
 * @palloc: contains all inforamtion regarding pble (idx + pble addr)
 * @pble_cnt: #of pbles requested
 * @level1_only: true if only pble level 1 to acquire
 */
enum irdma_status_code irdma_get_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
				      struct irdma_pble_alloc *palloc,
				      u32 pble_cnt, bool level1_only)
{
	enum irdma_status_code status = 0;
	int max_sds = 0;
	int i;

	palloc->total_cnt = pble_cnt;
	palloc->level = PBLE_LEVEL_0;

	mutex_lock(&pble_rsrc->pble_mutex_lock);

	/*check first to see if we can get pble's without acquiring
	 * additional sd's
	 */
	status = get_lvl1_lvl2_pble(pble_rsrc, palloc, level1_only);
	if (!status)
		goto exit;

	max_sds = (palloc->total_cnt >> 18) + 1;
	for (i = 0; i < max_sds; i++) {
		status = add_pble_prm(pble_rsrc);
		if (status)
			break;

		status = get_lvl1_lvl2_pble(pble_rsrc, palloc, level1_only);
		/* if level1_only, only go through it once */
		if (!status || level1_only)
			break;
	}

exit:
	if (!status) {
		pble_rsrc->allocdpbles += pble_cnt;
		pble_rsrc->stats_alloc_ok++;
	} else {
		pble_rsrc->stats_alloc_fail++;
	}
	mutex_unlock(&pble_rsrc->pble_mutex_lock);

	return status;
}

/**
 * irdma_free_pble - put pbles back into prm
 * @pble_rsrc: pble resources
 * @palloc: contains all information regarding pble resource being freed
 */
void irdma_free_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
		     struct irdma_pble_alloc *palloc)
{
	pble_rsrc->freedpbles += palloc->total_cnt;

	if (palloc->level == PBLE_LEVEL_2)
		free_lvl2(pble_rsrc, palloc);
	else
		irdma_prm_return_pbles(&pble_rsrc->pinfo,
				       &palloc->level1.chunkinfo);
	pble_rsrc->stats_alloc_freed++;
}
