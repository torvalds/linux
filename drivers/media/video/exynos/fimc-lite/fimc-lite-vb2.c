/* linux/drivers/media/video/exynos/flite-vb2.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Videobuf2 allocator operations file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include "fimc-lite-core.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void *flite_cma_init(struct flite_dev *flite)
{
	return vb2_cma_phys_init(&flite->pdev->dev, NULL, 0, false);
}

int flite_cma_resume(void *alloc_ctx) {}
void flite_cma_suspend(void *alloc_ctx) {}
void flite_cma_set_cacheable(void *alloc_ctx, bool cacheable) {}

int flite_cma_cache_flush(struct vb2_buffer *vb, u32 plane_no)
{
	return 0;
}

const struct flite_vb2 flite_vb2_cma = {
	.ops		= &vb2_cma_phys_memops,
	.init		= flite_cma_init,
	.cleanup	= vb2_cma_phys_cleanup,
	.plane_addr	= vb2_cma_phys_plane_paddr,
	.resume		= flite_cma_resume,
	.suspend	= flite_cma_suspend,
	.cache_flush	= flite_cma_cache_flush,
	.set_cacheable	= flite_cma_set_cacheable,
};
#elif defined(CONFIG_VIDEOBUF2_ION)
void *flite_ion_init(struct flite_dev *flite)
{
	return vb2_ion_create_context(&flite->pdev->dev, SZ_4K,
		VB2ION_CTX_VMCONTIG | VB2ION_CTX_IOMMU | VB2ION_CTX_UNCACHED);
}

static unsigned long flite_vb2_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dva = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dva) != 0);

	return dva;
}

static int flite_vb2_ion_cache_flush(struct vb2_buffer *vb, u32 plane_no)
{
	return vb2_ion_buf_prepare(vb);
}

const struct flite_vb2 flite_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= flite_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= flite_vb2_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.cache_flush	= flite_vb2_ion_cache_flush,
	.set_cacheable	= vb2_ion_set_cached,
};
#endif
