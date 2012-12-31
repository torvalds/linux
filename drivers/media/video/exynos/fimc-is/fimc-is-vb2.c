/*
 * Samsung Exynos4 SoC series FIMC-IS video buffer2 interface
 *
 * main platform driver interface
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Younghwan Joo, <yhwan.joo@samsung.com>
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
void *fimc_is_cma_init(struct fimc_is_dev *isp)
{
	return vb2_cma_phys_init(&isp->pdev->dev,
				FIMC_IS_MEM_ISP_BUF, 0, false);
}

void fimc_is_cma_resume(void *alloc_ctx) {}
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
void *fimc_is_ion_init(struct fimc_is_dev *isp)
{
	struct vb2_ion vb2_ion;
	struct vb2_drv vb2_drv = {0, };
	char ion_name[16] = {0,};

	vb2_ion.dev = &isp->pdev->dev;
	sprintf(ion_name, "exynos5-fimc-is");
	vb2_ion.name = ion_name;
	vb2_ion.contig = true;
	vb2_ion.cacheable = true;
	vb2_ion.align = SZ_4K;

	vb2_drv.use_mmu = true;

	return vb2_ion_init(&vb2_ion, &vb2_drv);
}

const struct fimc_is_vb2 fimc_is_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= fimc_is_ion_init,
	.cleanup	= vb2_ion_cleanup,
	.plane_addr	= vb2_ion_plane_dvaddr,
	.resume		= vb2_ion_resume,
	.suspend	= vb2_ion_suspend,
	.cache_flush	= vb2_ion_cache_flush,
	.set_cacheable	= vb2_ion_set_cacheable,
	.set_sharable	= vb2_ion_set_sharable,
	.get_kvaddr	= vb2_ion_plane_kvaddr,
};
#endif

