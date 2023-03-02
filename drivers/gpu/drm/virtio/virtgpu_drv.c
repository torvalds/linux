/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *    Gerd Hoffmann <kraxel@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/wait.h>

#include <drm/drm.h>
#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_file.h>

#include "virtgpu_drv.h"

static const struct drm_driver driver;

static int virtio_gpu_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, virtio_gpu_modeset, int, 0400);

static int virtio_gpu_pci_quirk(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	const char *pname = dev_name(&pdev->dev);
	bool vga = (pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA;
	int ret;

	DRM_INFO("pci: %s detected at %s\n",
		 vga ? "virtio-vga" : "virtio-gpu-pci",
		 pname);
	if (vga) {
		ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &driver);
		if (ret)
			return ret;
	}

	return 0;
}

static int virtio_gpu_probe(struct virtio_device *vdev)
{
	struct drm_device *dev;
	int ret;

	if (drm_firmware_drivers_only() && virtio_gpu_modeset == -1)
		return -EINVAL;

	if (virtio_gpu_modeset == 0)
		return -EINVAL;

	/*
	 * The virtio-gpu device is a virtual device that doesn't have DMA
	 * ops assigned to it, nor DMA mask set and etc. Its parent device
	 * is actual GPU device we want to use it for the DRM's device in
	 * order to benefit from using generic DRM APIs.
	 */
	dev = drm_dev_alloc(&driver, vdev->dev.parent);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	vdev->priv = dev;

	if (dev_is_pci(vdev->dev.parent)) {
		ret = virtio_gpu_pci_quirk(dev);
		if (ret)
			goto err_free;
	}

	ret = virtio_gpu_init(vdev, dev);
	if (ret)
		goto err_free;

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_deinit;

	drm_fbdev_generic_setup(vdev->priv, 32);
	return 0;

err_deinit:
	virtio_gpu_deinit(dev);
err_free:
	drm_dev_put(dev);
	return ret;
}

static void virtio_gpu_remove(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
	virtio_gpu_deinit(dev);
	drm_dev_put(dev);
}

static void virtio_gpu_config_changed(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->config_changed_work);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
#ifdef __LITTLE_ENDIAN
	/*
	 * Gallium command stream send by virgl is native endian.
	 * Because of that we only support little endian guests on
	 * little endian hosts.
	 */
	VIRTIO_GPU_F_VIRGL,
#endif
	VIRTIO_GPU_F_EDID,
	VIRTIO_GPU_F_RESOURCE_UUID,
	VIRTIO_GPU_F_RESOURCE_BLOB,
	VIRTIO_GPU_F_CONTEXT_INIT,
};
static struct virtio_driver virtio_gpu_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_gpu_probe,
	.remove = virtio_gpu_remove,
	.config_changed = virtio_gpu_config_changed
};

module_virtio_driver(virtio_gpu_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio GPU driver");
MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Dave Airlie <airlied@redhat.com>");
MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
MODULE_AUTHOR("Alon Levy");

DEFINE_DRM_GEM_FOPS(virtio_gpu_driver_fops);

static const struct drm_driver driver = {
	/*
	 * If KMS is disabled DRIVER_MODESET and DRIVER_ATOMIC are masked
	 * out via drm_device::driver_features:
	 */
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_RENDER | DRIVER_ATOMIC,
	.open = virtio_gpu_driver_open,
	.postclose = virtio_gpu_driver_postclose,

	.dumb_create = virtio_gpu_mode_dumb_create,
	.dumb_map_offset = virtio_gpu_mode_dumb_mmap,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = virtio_gpu_debugfs_init,
#endif
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_mmap = drm_gem_prime_mmap,
	.gem_prime_import = virtgpu_gem_prime_import,
	.gem_prime_import_sg_table = virtgpu_gem_prime_import_sg_table,

	.gem_create_object = virtio_gpu_create_object,
	.fops = &virtio_gpu_driver_fops,

	.ioctls = virtio_gpu_ioctls,
	.num_ioctls = DRM_VIRTIO_NUM_IOCTLS,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	.release = virtio_gpu_release,
};
