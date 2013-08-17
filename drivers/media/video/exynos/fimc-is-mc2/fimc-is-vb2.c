/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is misc functions(mipi, fimc-lite control)
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <media/videobuf2-core.h>
#include <linux/platform_device.h>
#include "fimc-is-core.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void *fimc_is_cma_init(struct fimc_is_core *isp)
{
	return vb2_cma_phys_init(&isp->pdev->dev, NULL, 0, false);
}

int fimc_is_cma_resume(void *alloc_ctx)
{
	return 1;
}
void fimc_is_cma_suspend(void *alloc_ctx) {}
void fimc_is_cma_set_cacheable(void *alloc_ctx, bool cacheable) {}

int fimc_is_cma_cache_flush(struct vb2_buffer *vb, u32 plane_no)
{
	return 0;
}

const struct fimc_is_vb2 fimc_is_vb2_cma = {
	.ops		= &vb2_cma_phys_memops,
	.init		= fimc_is_cma_init,
	.cleanup	= vb2_cma_phys_cleanup,
	.plane_addr	= vb2_cma_phys_plane_paddr,
	.resume		= fimc_is_cma_resume,
	.suspend	= fimc_is_cma_suspend,
	.cache_flush	= fimc_is_cma_cache_flush,
	.set_cacheable	= fimc_is_cma_set_cacheable,
};
#elif defined(CONFIG_VIDEOBUF2_ION)
static void *fimc_is_ion_init(struct platform_device *pdev)
{
	return vb2_ion_create_context(&pdev->dev, SZ_4K,
					VB2ION_CTX_IOMMU | VB2ION_CTX_VMCONTIG);
}

static unsigned long plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dva = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dva) != 0);

	return dva;
}

const struct fimc_is_vb2 fimc_is_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= fimc_is_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.cache_flush	= vb2_ion_cache_flush,
	.set_cacheable	= vb2_ion_set_cached,
	.plane_kvaddr = vb2_plane_vaddr,
};
#endif
