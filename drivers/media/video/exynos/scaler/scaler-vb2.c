/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Videobuf2 bridge driver file for EXYNOS Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include "scaler.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void *sc_cma_init(struct sc_dev *sc)
{
	return vb2_cma_phys_init(sc->dev, NULL, 0, false);
}

int sc_cma_resume(void *alloc_ctx) {}
void sc_cma_suspend(void *alloc_ctx) {}
void sc_cma_set_cacheable(void *alloc_ctx, bool cacheable) {}
int sc_cma_cache_flush(struct vb2_buffer *vb, u32 plane_no) { return 0; }

const struct sc_vb2 sc_vb2_cma = {
	.ops		= &vb2_cma_phys_memops,
	.init		= sc_cma_init,
	.cleanup	= vb2_cma_phys_cleanup,
	.plane_addr	= vb2_cma_phys_plane_paddr,
	.resume		= sc_cma_resume,
	.suspend	= sc_cma_suspend,
	.cache_flush	= sc_cma_cache_flush,
	.set_cacheable	= sc_cma_set_cacheable,
};

#elif defined(CONFIG_VIDEOBUF2_ION)
void *sc_ion_init(struct sc_dev *sc)
{
	return vb2_ion_create_context(sc->dev, SZ_4K,
		VB2ION_CTX_VMCONTIG | VB2ION_CTX_IOMMU | VB2ION_CTX_UNCACHED);
}

static unsigned long sc_vb2_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dma_addr = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dma_addr) != 0);

	return (unsigned long)dma_addr;
}

const struct sc_vb2 sc_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= sc_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= sc_vb2_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.set_cacheable	= vb2_ion_set_cached,
};
#endif
