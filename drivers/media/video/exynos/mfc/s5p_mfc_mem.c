/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_mem.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/dma-mapping.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-memops.h>
#include <asm/cacheflush.h>

#include "s5p_mfc_common.h"
#include "s5p_mfc_mem.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_debug.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
static const char * const s5p_mem_types[] = {
	MFC_CMA_FW,
	MFC_CMA_BANK1,
	MFC_CMA_BANK2,
};

static unsigned long s5p_mem_alignments[] = {
	MFC_CMA_FW_ALIGN,
	MFC_CMA_BANK1_ALIGN,
	MFC_CMA_BANK2_ALIGN,
};

struct vb2_mem_ops *s5p_mfc_mem_ops(void)
{
	return (struct vb2_mem_ops *)&vb2_cma_phys_memops;
}

void **s5p_mfc_mem_init_multi(struct device *dev, unsigned int ctx_num)
{
/* TODO Cachable should be set */
	return (void **)vb2_cma_phys_init_multi(dev, ctx_num,
					   s5p_mem_types,
					   s5p_mem_alignments, 0);
}

void s5p_mfc_mem_cleanup_multi(void **alloc_ctxes)
{
	vb2_cma_phys_cleanup_multi(alloc_ctxes);
}
#elif defined(CONFIG_VIDEOBUF2_ION)
struct vb2_mem_ops *s5p_mfc_mem_ops(void)
{
	return (struct vb2_mem_ops *)&vb2_ion_memops;
}

void **s5p_mfc_mem_init_multi(struct device *dev, unsigned int ctx_num)
{

	struct s5p_mfc_dev *m_dev = platform_get_drvdata(to_platform_device(dev));
	void **alloc_ctxes;
	unsigned int i;

	alloc_ctxes = kmalloc(sizeof(*alloc_ctxes) * ctx_num, GFP_KERNEL);
	if (!alloc_ctxes)
		return NULL;

	for (i = 0; i < ctx_num; i++) {
		alloc_ctxes[i] = vb2_ion_create_context(dev,
				IS_MFCV6(m_dev) ? SZ_4K : SZ_128K,
				VB2ION_CTX_VMCONTIG |
				VB2ION_CTX_IOMMU |
				VB2ION_CTX_KVA_STATIC |
				VB2ION_CTX_UNCACHED);
		if (IS_ERR(alloc_ctxes[i]))
			break;
	}

	if (i < ctx_num) {
		while (i-- > 0)
			vb2_ion_destroy_context(alloc_ctxes[i]);

		kfree(alloc_ctxes);
		alloc_ctxes = NULL;
	}

	return alloc_ctxes;
}

void s5p_mfc_mem_cleanup_multi(void **alloc_ctxes, unsigned int ctx_num)
{
	while (ctx_num-- > 0)
		vb2_ion_destroy_context(alloc_ctxes[ctx_num]);

	kfree(alloc_ctxes);
}
#endif

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
struct vb2_cma_phys_conf {
	struct device		*dev;
	const char		*type;
	unsigned long		alignment;
	bool			cacheable;
};

struct vb2_cma_phys_buf {
	struct vb2_cma_phys_conf		*conf;
	dma_addr_t			paddr;
	unsigned long			size;
	struct vm_area_struct		*vma;
	atomic_t			refcount;
	struct vb2_vmarea_handler	handler;
	bool				cacheable;
};
void s5p_mfc_cache_clean(void *alloc_ctx)
{
	struct vb2_cma_phys_buf *buf = (struct vb2_cma_phys_buf *)alloc_ctx;
	void *start_addr;
	unsigned long size;
	unsigned long paddr = (dma_addr_t)buf->paddr;

	start_addr = (dma_addr_t *)phys_to_virt(buf->paddr);
	size = buf->size;

	dmac_map_area(start_addr, size, DMA_TO_DEVICE);
	outer_clean_range(paddr, paddr + size);
}

void s5p_mfc_cache_inv(void *alloc_ctx)
{
	struct vb2_cma_phys_buf *buf = (struct vb2_cma_phys_buf *)alloc_ctx;
	void *start_addr;
	unsigned long size;
	unsigned long paddr = (dma_addr_t)buf->paddr;

	start_addr = (dma_addr_t *)phys_to_virt(buf->paddr);
	size = buf->size;

	outer_inv_range(paddr, paddr + size);
	dmac_unmap_area(start_addr, size, DMA_FROM_DEVICE);
}

void s5p_mfc_mem_suspend(void *alloc_ctx)
{
	/* NOP */
}

void s5p_mfc_mem_resume(void *alloc_ctx)
{
	/* NOP */
}

void s5p_mfc_mem_set_cacheable(void *alloc_ctx, bool cacheable)
{
	vb2_cma_phys_set_cacheable(alloc_ctx, cacheable);
}

void s5p_mfc_mem_get_cacheable(void *alloc_ctx)
{
	/* NOP */
}

int s5p_mfc_mem_cache_flush(struct vb2_buffer *vb, u32 plane_no)
{
	vb2_cma_phys_cache_flush(vb, plane_no);
	return 0;
}
#elif defined(CONFIG_VIDEOBUF2_ION)
void s5p_mfc_mem_set_cacheable(void *alloc_ctx, bool cacheable)
{
	vb2_ion_set_cached(alloc_ctx, cacheable);
}

void s5p_mfc_mem_clean_priv(void *vb_priv, void *start, off_t offset,
							size_t size)
{
	vb2_ion_sync_for_device(vb_priv, offset, size, DMA_TO_DEVICE);
}

void s5p_mfc_mem_inv_priv(void *vb_priv, void *start, off_t offset,
							size_t size)
{
	vb2_ion_sync_for_device(vb_priv, offset, size, DMA_FROM_DEVICE);
}

int s5p_mfc_mem_clean_vb(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_ion_cookie *cookie;
	int i;
	size_t size;

	for (i = 0; i < num_planes; i++) {
		cookie = vb2_plane_cookie(vb, i);
		if (!cookie)
			continue;

		size = vb->v4l2_planes[i].length;
		vb2_ion_sync_for_device(cookie, 0, size, DMA_TO_DEVICE);
	}

	return 0;
}

int s5p_mfc_mem_inv_vb(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_ion_cookie *cookie;
	int i;
	size_t size;

	for (i = 0; i < num_planes; i++) {
		cookie = vb2_plane_cookie(vb, i);
		if (!cookie)
			continue;

		size = vb->v4l2_planes[i].length;
		vb2_ion_sync_for_device(cookie, 0, size, DMA_FROM_DEVICE);
	}

	return 0;
}

int s5p_mfc_mem_flush_vb(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_ion_cookie *cookie;
	int i;
	enum dma_data_direction dir;
	size_t size;

	dir = V4L2_TYPE_IS_OUTPUT(vb->v4l2_buf.type) ?
					DMA_TO_DEVICE : DMA_FROM_DEVICE;

	for (i = 0; i < num_planes; i++) {
		cookie = vb2_plane_cookie(vb, i);
		if (!cookie)
			continue;

		size = vb->v4l2_planes[i].length;
		vb2_ion_sync_for_device(cookie, 0, size, dir);
	}

	return 0;
}

void s5p_mfc_mem_suspend(void *alloc_ctx)
{
	vb2_ion_detach_iommu(alloc_ctx);
}

int s5p_mfc_mem_resume(void *alloc_ctx)
{
	return vb2_ion_attach_iommu(alloc_ctx);
}
#endif
