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

#include "virtgpu_drv.h"

int drm_virtio_set_busid(struct drm_device *dev, struct drm_master *master)
{
	struct pci_dev *pdev = dev->pdev;

	if (pdev) {
		return drm_pci_set_busid(dev, master);
	}
	return 0;
}

int drm_virtio_init(struct drm_driver *driver, struct virtio_device *vdev)
{
	struct drm_device *dev;
	int ret;

	dev = drm_dev_alloc(driver, &vdev->dev);
	if (!dev)
		return -ENOMEM;
	dev->virtdev = vdev;
	vdev->priv = dev;

	if (strcmp(vdev->dev.parent->bus->name, "pci") == 0) {
		struct pci_dev *pdev = to_pci_dev(vdev->dev.parent);
		bool vga = (pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA;

		if (vga) {
			/*
			 * Need to make sure we don't have two drivers
			 * for the same hardware here.  Some day we
			 * will simply kick out the firmware
			 * (vesa/efi) framebuffer.
			 *
			 * Virtual hardware specs for virtio-vga are
			 * not finalized yet, therefore we can't add
			 * code for that yet.
			 *
			 * So ignore the device for the time being,
			 * and suggest to the user use the device
			 * variant without vga compatibility mode.
			 */
			DRM_ERROR("virtio-vga not (yet) supported\n");
			DRM_ERROR("please use virtio-gpu-pci instead\n");
			ret = -ENODEV;
			goto err_free;
		}
		dev->pdev = pdev;
	}

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free;

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n", driver->name,
		 driver->major, driver->minor, driver->patchlevel,
		 driver->date, dev->primary->index);

	return 0;

err_free:
	drm_dev_unref(dev);
	return ret;
}
