/* linux/drivers/media/video/exynos/gsc-vb2.c
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
#include "gsc-core.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void *gsc_cma_init(struct gsc_dev *gsc)
{
	return vb2_cma_phys_init(&gsc->pdev->dev, NULL, 0, false);
}

int gsc_cma_resume(void *alloc_ctx)
{
	return 1;
}
void gsc_cma_suspend(void *alloc_ctx) {}
void gsc_cma_set_cacheable(void *alloc_ctx, bool cacheable) {}

const struct gsc_vb2 gsc_vb2_cma = {
	.ops		= &vb2_cma_phys_memops,
	.init		= gsc_cma_init,
	.cleanup	= vb2_cma_phys_cleanup,
	.plane_addr	= vb2_cma_phys_plane_paddr,
	.resume		= gsc_cma_resume,
	.suspend	= gsc_cma_suspend,
	.set_cacheable	= gsc_cma_set_cacheable,
};
#elif defined(CONFIG_VIDEOBUF2_ION)
void *gsc_ion_init(struct gsc_dev *gsc)
{
	return vb2_ion_create_context(&gsc->pdev->dev, SZ_4K,
		VB2ION_CTX_VMCONTIG | VB2ION_CTX_IOMMU | VB2ION_CTX_UNCACHED);
}

static unsigned long gsc_vb2_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dva = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dva) != 0);

	return dva;
}
const struct gsc_vb2 gsc_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= gsc_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= gsc_vb2_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.set_cacheable	= vb2_ion_set_cached,
	.set_protected	= vb2_ion_set_protected,
};
#endif
