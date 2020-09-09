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

static size_t mgag200_probe_vram(struct mga_device *mdev, void __iomem *mem,
				 size_t size)
{
	int offset;
	int orig;
	int test1, test2;
	int orig1, orig2;
	size_t vram_size;

	/* Probe */
	orig = ioread16(mem);
	iowrite16(0, mem);

	vram_size = size;

	if ((mdev->type == G200_EW3) && (vram_size >= 0x1000000))
		vram_size = vram_size - 0x400000;

	for (offset = 0x100000; offset < vram_size; offset += 0x4000) {
		orig1 = ioread8(mem + offset);
		orig2 = ioread8(mem + offset + 0x100);

		iowrite16(0xaa55, mem + offset);
		iowrite16(0xaa55, mem + offset + 0x100);

		test1 = ioread16(mem + offset);
		test2 = ioread16(mem);

		iowrite16(orig1, mem + offset);
		iowrite16(orig2, mem + offset + 0x100);

		if (test1 != 0xaa55)
			break;

		if (test2)
			break;
	}

	iowrite16(orig, mem);

	return offset - 65536;
}

static void mgag200_mm_release(struct drm_device *dev, void *ptr)
{
	struct mga_device *mdev = to_mga_device(dev);

	mdev->vram_fb_available = 0;
	iounmap(mdev->vram);
	arch_io_free_memtype_wc(pci_resource_start(dev->pdev, 0),
				pci_resource_len(dev->pdev, 0));
	arch_phys_wc_del(mdev->fb_mtrr);
	mdev->fb_mtrr = 0;
}

int mgag200_mm_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	resource_size_t start, len;
	int ret;

	/* BAR 0 is VRAM */
	start = pci_resource_start(dev->pdev, 0);
	len = pci_resource_len(dev->pdev, 0);

	if (!devm_request_mem_region(dev->dev, start, len, "mgadrmfb_vram")) {
		drm_err(dev, "can't reserve VRAM\n");
		return -ENXIO;
	}

	arch_io_reserve_memtype_wc(start, len);

	mdev->fb_mtrr = arch_phys_wc_add(start, len);

	mdev->vram = ioremap(start, len);
	if (!mdev->vram) {
		ret = -ENOMEM;
		goto err_arch_phys_wc_del;
	}

	mdev->mc.vram_size = mgag200_probe_vram(mdev, mdev->vram, len);
	mdev->mc.vram_base = start;
	mdev->mc.vram_window = len;

	mdev->vram_fb_available = mdev->mc.vram_size;

	return drmm_add_action_or_reset(dev, mgag200_mm_release, NULL);

err_arch_phys_wc_del:
	arch_phys_wc_del(mdev->fb_mtrr);
	arch_io_free_memtype_wc(start, len);
	return ret;
}
