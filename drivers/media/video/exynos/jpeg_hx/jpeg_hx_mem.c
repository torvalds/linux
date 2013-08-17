/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_mem.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Managent memory of the jpeg driver for encoder/docoder.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/page.h>

#include <linux/cma.h>

#include "jpeg_hx_mem.h"
#include "jpeg_hx_core.h"

#if defined(CONFIG_VIDEOBUF2_ION)
static void *jpeg_ion_init(struct jpeg_dev *dev)
{
	return vb2_ion_create_context(&dev->plat_dev->dev, SZ_8K,
					VB2ION_CTX_VMCONTIG | VB2ION_CTX_IOMMU);
}

static unsigned long jpeg_vb2_plane_addr(struct vb2_buffer *vb, u32 plane_no)
{
	void *cookie = vb2_plane_cookie(vb, plane_no);
	dma_addr_t dva = 0;

	WARN_ON(vb2_ion_dma_address(cookie, &dva) != 0);

	return dva;
}

const struct jpeg_vb2 jpeg_hx_vb2_ion = {
	.ops		= &vb2_ion_memops,
	.init		= jpeg_ion_init,
	.cleanup	= vb2_ion_destroy_context,
	.plane_addr	= jpeg_vb2_plane_addr,
	.resume		= vb2_ion_attach_iommu,
	.suspend	= vb2_ion_detach_iommu,
	.buf_prepare	= vb2_ion_buf_prepare,
	.buf_finish	= vb2_ion_buf_finish,
};
#endif
