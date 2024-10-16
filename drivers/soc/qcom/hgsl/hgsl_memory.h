/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef HGSL_MEMORY_INCLUDED
#define HGSL_MEMORY_INCLUDED

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/rbtree.h>
#include "hgsl_types.h"
#include "hgsl_utils.h"

#define GSL_MEMTYPE_MASK		0x0000FF00
#define GSL_MEMTYPE_SHIFT			8
#define GET_MEMTYPE(x) \
	((x & GSL_MEMTYPE_MASK) >> GSL_MEMTYPE_SHIFT)
#define GSL_MEMTYPE_OBJECTANY			0x0
#define GSL_MEMTYPE_FRAMEBUFFER			0x1
#define GSL_MEMTYPE_RENDERBUFFER		0x2
#define GSL_MEMTYPE_ARRAYBUFFER			0x3
#define GSL_MEMTYPE_ELEMENTARRAYBUFFER		0x4
#define GSL_MEMTYPE_VERTEXARRAYBUFFER		0x5
#define GSL_MEMTYPE_TEXTURE			0x6
#define GSL_MEMTYPE_SURFACE			0x7
#define GSL_MEMTYPE_EGL_SURFACE			0x8
#define GSL_MEMTYPE_GL				0x9
#define GSL_MEMTYPE_CL				0xa
#define GSL_MEMTYPE_CL_BUFFER_MAP		0xb
#define GSL_MEMTYPE_CL_BUFFER_NOMAP		0xc
#define GSL_MEMTYPE_CL_IMAGE_MAP		0xd
#define GSL_MEMTYPE_CL_IMAGE_NOMAP		0xe
#define GSL_MEMTYPE_CL_KERNEL_STACK		0xf
#define GSL_MEMTYPE_COMMAND			0x10
#define GSL_MEMTYPE_2D				0x11
#define GSL_MEMTYPE_EGL_IMAGE			0x12
#define GSL_MEMTYPE_EGL_SHADOW			0x13
#define GSL_MEMTYPE_MULTISAMPLE			0x14
#define GSL_MEMTYPE_VISIBLE_MAS			0x15
#define GSL_MEMTYPE_KERNEL			0xff
#define HGSL_MEM_META_MAX_SIZE         128
enum gsl_user_mem_type_t {
	GSL_USER_MEM_TYPE_PMEM         = 0x00000000,
	GSL_USER_MEM_TYPE_ASHMEM       = 0x00000001,
	GSL_USER_MEM_TYPE_ADDR         = 0x00000002,
	GSL_USER_MEM_TYPE_ION          = 0x00000003
};

struct hgsl_mem_node {
	struct rb_node             mem_rb_node;
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
	bool                      default_iocoherency;
	char                      metainfo[HGSL_MEM_META_MAX_SIZE];
};

int hgsl_sharedmem_alloc(struct device *dev, uint32_t sizebytes,
	uint32_t flags, struct hgsl_mem_node *mem_node);

void hgsl_sharedmem_free(struct hgsl_mem_node *mem_node);

int hgsl_mem_cache_op(struct device *dev, struct hgsl_mem_node *mem_node,
	bool internal, uint64_t offsetbytes, uint64_t sizebytes, uint32_t op);

void hgsl_put_sgt(struct hgsl_mem_node *mem_node, bool internal);

void *hgsl_mem_node_zalloc(bool iocoherency);

int hgsl_mem_add_node(struct rb_root *rb_root,
		struct hgsl_mem_node *mem_node);
struct hgsl_mem_node *hgsl_mem_find_node_locked(
		struct rb_root *rb_root, uint64_t gpuaddr,
		uint64_t size, bool accurate);

static inline bool hgsl_mem_range_inspect(uint64_t da1, uint64_t da2,
			uint64_t size1, uint64_t size2, bool accurate)
{
	if (accurate)
		return ((da1 == da2) && (size1 == size2));
	else
		return ((da1 <= da2) && (da1 + size1) >= (da2 + size2));
}

#endif
