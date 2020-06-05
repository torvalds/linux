/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/pci.h>

#include "mgag200_drv.h"

int mgag200_mm_init(struct mga_device *mdev)
{
	struct drm_device *dev = mdev->dev;
	resource_size_t start, len;
	int ret;

	/* BAR 0 is VRAM */
	start = pci_resource_start(dev->pdev, 0);
	len = pci_resource_len(dev->pdev, 0);

	arch_io_reserve_memtype_wc(start, len);

	mdev->fb_mtrr = arch_phys_wc_add(start, len);

	mdev->vram = ioremap(start, len);
	if (!mdev->vram) {
		ret = -ENOMEM;
		goto err_arch_phys_wc_del;
	}

	mdev->vram_fb_available = mdev->mc.vram_size;

	return 0;

err_arch_phys_wc_del:
	arch_phys_wc_del(mdev->fb_mtrr);
	arch_io_free_memtype_wc(start, len);
	return ret;
}

void mgag200_mm_fini(struct mga_device *mdev)
{
	struct drm_device *dev = mdev->dev;

	mdev->vram_fb_available = 0;
	iounmap(mdev->vram);
	arch_io_free_memtype_wc(pci_resource_start(dev->pdev, 0),
				pci_resource_len(dev->pdev, 0));
	arch_phys_wc_del(mdev->fb_mtrr);
	mdev->fb_mtrr = 0;
}
