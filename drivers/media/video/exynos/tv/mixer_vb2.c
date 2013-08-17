/* linux/drivers/media/video/exynos/tv/mixer_vb2.c
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
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif

#include "mixer.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void *mxr_cma_init(struct mxr_device *mdev)
{
	return vb2_cma_phys_init(mdev->dev, NULL, 0, false);
}

int mxr_cma_resume(void *alloc_ctx) {}
void mxr_cma_suspend(void *alloc_ctx) {}
void mxr_cma_set_cacheable(void *alloc_ctx, bool cacheable) {}

int mxr_cma_cache_flush(struct vb2_buffer *vb, u32 plane_no)
{
	return 0;
}

const struct mxr_vb2 mxr_vb2_cma = {
	.ops		= &vb2_cma_phys_memops,
	.init		= mxr_cma_init,
	.cleanup	= vb2_cma_phys_cleanup,
	.plane_addr	= vb2_cma_phys_plane_paddr,
	.resume		= mxr_cma_resume,
	.suspend	= mxr_cma_suspend,
	.set_cacheable	= mxr_cma_set_cacheable,
};

int mxr_buf_sync_prepare(struct vb2_buffer *vb)
{
	return mxr_cma_cache_flush(vb, vb->num_planes);
}

int mxr_buf_sync_finish(struct vb2_buffer *vb)
{
	return 0;
}

#elif defined(CONFIG_VIDEOBUF2_ION)
void *mxr_ion_init(struct mxr_device *mdev)
{
	return vb2_ion_create_context(mdev->dev, SZ_4K,
		VB2ION_CTX_VMCONTIG | VB2ION_CTX_UNCACHED | VB2ION_CTX_IOMMU);
}

unsigned long mxr_ion_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dva = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dva) != 0);

	return dva;
}

const struct mxr_vb2 mxr_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= mxr_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= mxr_ion_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.set_cacheable	= vb2_ion_set_cached,
};
#endif
