/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Videobuf2 bridge driver file for EXYNOS Image Rotator driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include "rotator.h"

#if defined(CONFIG_VIDEOBUF2_ION)
void *rot_ion_init(struct rot_dev *rot)
{
	return vb2_ion_create_context(rot->dev, SZ_1M,
		VB2ION_CTX_PHCONTIG | VB2ION_CTX_IOMMU | VB2ION_CTX_UNCACHED);
}

static unsigned long rot_vb2_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dma_addr = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dma_addr) != 0);

	return (unsigned long)dma_addr;
}

const struct rot_vb2 rot_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= rot_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= rot_vb2_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.set_cacheable	= vb2_ion_set_cached,
};
#endif
