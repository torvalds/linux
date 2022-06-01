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

#include <drm/drm_managed.h>

#include "mgag200_drv.h"

int mgag200_mm_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u8 misc;
	resource_size_t start, len;

	WREG_ECRT(0x04, 0x00);

	misc = RREG8(MGA_MISC_IN);
	misc |= MGAREG_MISC_RAMMAPEN |
		MGAREG_MISC_HIGH_PG_SEL;
	WREG8(MGA_MISC_OUT, misc);

	/* BAR 0 is VRAM */
	start = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);

	if (!devm_request_mem_region(dev->dev, start, len, "mgadrmfb_vram")) {
		drm_err(dev, "can't reserve VRAM\n");
		return -ENXIO;
	}

	/* Don't fail on errors, but performance might be reduced. */
	devm_arch_io_reserve_memtype_wc(dev->dev, start, len);
	devm_arch_phys_wc_add(dev->dev, start, len);

	mdev->vram = devm_ioremap(dev->dev, start, len);
	if (!mdev->vram)
		return -ENOMEM;

	mdev->mc.vram_size = len;
	mdev->mc.vram_base = start;
	mdev->mc.vram_window = len;

	return 0;
}
