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

#ifndef I40IW_PBLE_H
#define I40IW_PBLE_H

#define POOL_SHIFT      6
#define PBLE_PER_PAGE   512
#define I40IW_HMC_PAGED_BP_SHIFT 12
#define PBLE_512_SHIFT  9

enum i40iw_pble_level {
	I40IW_LEVEL_0 = 0,
	I40IW_LEVEL_1 = 1,
	I40IW_LEVEL_2 = 2
};

enum i40iw_alloc_type {
	I40IW_NO_ALLOC = 0,
	I40IW_DMA_COHERENT = 1,
	I40IW_VMALLOC = 2
};

struct i40iw_pble_info {
	unsigned long addr;
	u32 idx;
	u32 cnt;
};

struct i40iw_pble_level2 {
	struct i40iw_pble_info root;
	struct i40iw_pble_info *leaf;
	u32 leaf_cnt;
};

struct i40iw_pble_alloc {
	u32 total_cnt;
	enum i40iw_pble_level level;
	union {
		struct i40iw_pble_info level1;
		struct i40iw_pble_level2 level2;
	};
};

struct sd_pd_idx {
	u32 sd_idx;
	u32 pd_idx;
	u32 rel_pd_idx;
};

struct i40iw_add_page_info {
	struct i40iw_chunk *chunk;
	struct i40iw_hmc_sd_entry *sd_entry;
	struct i40iw_hmc_info *hmc_info;
	struct sd_pd_idx idx;
	u32 pages;
};

struct i40iw_chunk {
	struct list_head list;
	u32 size;
	void *vaddr;
	u64 fpm_addr;
	u32 pg_cnt;
	dma_addr_t *dmaaddrs;
	enum i40iw_alloc_type type;
};

struct i40iw_pble_pool {
	struct gen_pool *pool;
	struct list_head clist;
	u32 total_pble_alloc;
	u32 free_pble_cnt;
	u32 pool_shift;
};

struct i40iw_hmc_pble_rsrc {
	u32 unallocated_pble;
	u64 fpm_base_addr;
	u64 next_fpm_addr;
	struct i40iw_pble_pool pinfo;

	u32 stats_direct_sds;
	u32 stats_paged_sds;
	u64 stats_alloc_ok;
	u64 stats_alloc_fail;
	u64 stats_alloc_freed;
	u64 stats_lvl1;
	u64 stats_lvl2;
};

void i40iw_destroy_pble_pool(struct i40iw_sc_dev *dev, struct i40iw_hmc_pble_rsrc *pble_rsrc);
enum i40iw_status_code i40iw_hmc_init_pble(struct i40iw_sc_dev *dev,
					   struct i40iw_hmc_pble_rsrc *pble_rsrc);
void i40iw_free_pble(struct i40iw_hmc_pble_rsrc *pble_rsrc, struct i40iw_pble_alloc *palloc);
enum i40iw_status_code i40iw_get_pble(struct i40iw_sc_dev *dev,
				      struct i40iw_hmc_pble_rsrc *pble_rsrc,
				      struct i40iw_pble_alloc *palloc,
				      u32 pble_cnt);
#endif
