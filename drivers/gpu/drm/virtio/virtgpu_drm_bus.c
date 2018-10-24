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
			drm_fb_helper_remove_conflicting_pci_framebuffers(pdev,
									  0,
									  "virtiodrmfb");

		/*
		 * Normally the drm_dev_set_unique() call is done by core DRM.
		 * The following comment covers, why virtio cannot rely on it.
		 *
		 * Unlike the other virtual GPU drivers, virtio abstracts the
		 * underlying bus type by using struct virtio_device.
		 *
		 * Hence the dev_is_pci() check, used in core DRM, will fail
		 * and the unique returned will be the virtio_device "virtio0",
		 * while a "pci:..." one is required.
		 *
		 * A few other ideas were considered:
		 * - Extend the dev_is_pci() check [in drm_set_busid] to
		 *   consider virtio.
		 *   Seems like a bigger hack than what we have already.
		 *
		 * - Point drm_device::dev to the parent of the virtio_device
		 *   Semantic changes:
		 *   * Using the wrong device for i2c, framebuffer_alloc and
		 *     prime import.
		 *   Visual changes:
		 *   * Helpers such as DRM_DEV_ERROR, dev_info, drm_printer,
		 *     will print the wrong information.
		 *
		 * We could address the latter issues, by introducing
		 * drm_device::bus_dev, ... which would be used solely for this.
		 *
		 * So for the moment keep things as-is, with a bulky comment
		 * for the next person who feels like removing this
		 * drm_dev_set_unique() quirk.
		 */
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
	drm_dev_put(dev);
	return ret;
}
