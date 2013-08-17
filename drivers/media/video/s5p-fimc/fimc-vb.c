/*
 * linux/drivers/media/video/s5p-fimc/fimc-vb.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Videobuf2 allocator operations file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include "fimc-core.h"

void *fimc_ion_init(struct fimc_dev *fimc)
{
	return vb2_ion_create_context(&fimc->pdev->dev, SZ_4K,
		VB2ION_CTX_VMCONTIG | VB2ION_CTX_IOMMU | VB2ION_CTX_UNCACHED);
}

static unsigned long fimc_vb2_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dva = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dva) != 0);

	return dva;
}
const struct fimc_vb2 fimc_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= fimc_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= fimc_vb2_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.set_cacheable	= vb2_ion_set_cached,
};
