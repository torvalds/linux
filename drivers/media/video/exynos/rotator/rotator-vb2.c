/* linux/drivers/media/video/exynos/rotator/rotator-vb2.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Videobuf2 bridge driver file for Exynos Rotator driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include "rotator.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void *rot_cma_init(struct rot_dev *rot)
{
	return vb2_cma_phys_init(&rot->pdev->dev, NULL, 0, false);
}

void rot_cma_resume(void *alloc_ctx) {}
void rot_cma_suspend(void *alloc_ctx) {}
void rot_cma_set_cacheable(void *alloc_ctx, bool cacheable) {}
int rot_cma_cache_flush(struct vb2_buffer *vb, u32 plane_no) { return 0; }

const struct rot_vb2 rot_vb2_cma = {
	.ops		= &vb2_cma_phys_memops,
	.init		= rot_cma_init,
	.cleanup	= vb2_cma_phys_cleanup,
	.plane_addr	= vb2_cma_phys_plane_paddr,
	.resume		= rot_cma_resume,
	.suspend	= rot_cma_suspend,
	.cache_flush	= rot_cma_cache_flush,
	.set_cacheable	= rot_cma_set_cacheable,
};

#elif defined(CONFIG_VIDEOBUF2_ION)
void *rot_ion_init(struct rot_dev *rot)
{
	struct vb2_ion vb2_ion;
	struct vb2_drv vb2_drv = {0, };

	vb2_ion.dev = &rot->pdev->dev;
	vb2_ion.name = "Rotator";
	vb2_ion.contig = false;
	vb2_ion.cacheable = false;
	vb2_ion.align = SZ_4K;

	vb2_drv.use_mmu = true;

	return vb2_ion_init(&vb2_ion, &vb2_drv);
}

const struct rot_vb2 rot_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= rot_ion_init,
	.cleanup	= vb2_ion_cleanup,
	.plane_addr	= vb2_ion_plane_dvaddr,
	.resume		= vb2_ion_resume,
	.suspend	= vb2_ion_suspend,
	.cache_flush	= vb2_ion_cache_flush,
	.set_cacheable	= vb2_ion_set_cacheable,
	.set_sharable	= vb2_ion_set_sharable,
};
#endif
