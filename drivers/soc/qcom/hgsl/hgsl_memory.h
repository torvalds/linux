/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef HGSL_MEMORY_INCLUDED
#define HGSL_MEMORY_INCLUDED

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include "hgsl_types.h"
#include "hgsl_utils.h"

#define HGSL_MEM_META_MAX_SIZE         128
enum gsl_user_mem_type_t {
	GSL_USER_MEM_TYPE_PMEM         = 0x00000000,
	GSL_USER_MEM_TYPE_ASHMEM       = 0x00000001,
	GSL_USER_MEM_TYPE_ADDR         = 0x00000002,
	GSL_USER_MEM_TYPE_ION          = 0x00000003
};

struct hgsl_mem_node {
	struct list_head           node;
	struct gsl_memdesc_t       memdesc;
	int32_t                    fd;
	uint32_t                   export_id;
	enum gsl_user_mem_type_t   memtype;
	struct dma_buf            *dma_buf;
	struct page               **pages;
	uint32_t                  page_count;
	struct dma_buf_attachment *attach;
	struct sg_table           *sgt;
	struct sg_table           *sgt_ext;
	uint32_t                  sgt_refcount;
	uint32_t                  sg_nents;
	void                      *vmapping;
	uint32_t                  vmap_count;
	uint32_t                  flags;
	char                      metainfo[HGSL_MEM_META_MAX_SIZE];
};

int hgsl_sharedmem_alloc(struct device *dev, uint32_t sizebytes,
	uint32_t flags, struct hgsl_mem_node *mem_node);

void hgsl_sharedmem_free(struct hgsl_mem_node *mem_node);

int hgsl_mem_cache_op(struct device *dev, struct hgsl_mem_node *mem_node,
	bool internal, uint64_t offsetbytes, uint64_t sizebytes, uint32_t op);

void hgsl_put_sgt(struct hgsl_mem_node *mem_node, bool internal);

struct hgsl_mem_node *hgsl_mem_find_base_locked(struct list_head *head,
	uint64_t gpuaddr, uint64_t size);

#endif
