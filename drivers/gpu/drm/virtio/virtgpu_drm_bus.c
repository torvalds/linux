/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/pci.h>
#include <drm/drm_fb_helper.h>

#include "virtgpu_drv.h"

static void virtio_pci_kick_out_firmware_fb(struct pci_dev *pci_dev)
{
	struct apertures_struct *ap;
	bool primary;

	ap = alloc_apertures(1);
	if (!ap)
		return;

	ap->ranges[0].base = pci_resource_start(pci_dev, 0);
	ap->ranges[0].size = pci_resource_len(pci_dev, 0);

	primary = pci_dev->resource[PCI_ROM_RESOURCE].flags
		& IORESOURCE_ROM_SHADOW;

	drm_fb_helper_remove_conflicting_framebuffers(ap, "virtiodrmfb", primary);

	kfree(ap);
}

int drm_virtio_init(struct drm_driver *driver, struct virtio_device *vdev)
{
	struct drm_device *dev;
	int ret;

	dev = drm_dev_alloc(driver, &vdev->dev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	vdev->priv = dev;

	if (strcmp(vdev->dev.parent->bus->name, "pci") == 0) {
		struct pci_dev *pdev = to_pci_dev(vdev->dev.parent);
		const char *pname = dev_name(&pdev->dev);
		bool vga = (pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA;
		char unique[20];

		DRM_INFO("pci: %s detected at %s\n",
			 vga ? "virtio-vga" : "virtio-gpu-pci",
			 pname);
		dev->pdev = pdev;
		if (vga)
			virtio_pci_kick_out_firmware_fb(pdev);

		snprintf(unique, sizeof(unique), "pci:%s", pname);
		ret = drm_dev_set_unique(dev, unique);
		if (ret)
			goto err_free;

	}

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free;

	return 0;

err_free:
	drm_dev_unref(dev);
	return ret;
}
