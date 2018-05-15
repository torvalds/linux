/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/drm_crtc_helper.h>
#include "vkms_drv.h"

#define DRIVER_NAME	"vkms"
#define DRIVER_DESC	"Virtual Kernel Mode Setting"
#define DRIVER_DATE	"20180514"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct vkms_device *vkms_device;

static const struct file_operations vkms_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= drm_gem_mmap,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.release	= drm_release,
};

static void vkms_release(struct drm_device *dev)
{
	struct vkms_device *vkms = container_of(dev, struct vkms_device, drm);

	platform_device_unregister(vkms->platform);
	drm_mode_config_cleanup(&vkms->drm);
	drm_dev_fini(&vkms->drm);
}

static struct drm_driver vkms_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.release		= vkms_release,
	.fops			= &vkms_driver_fops,

	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
};

static const u32 vkms_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static void vkms_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs vkms_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vkms_connector_destroy,
};

static int __init vkms_init(void)
{
	int ret;

	vkms_device = kzalloc(sizeof(*vkms_device), GFP_KERNEL);
	if (!vkms_device)
		return -ENOMEM;

	ret = drm_dev_init(&vkms_device->drm, &vkms_driver, NULL);
	if (ret)
		goto out_free;

	vkms_device->platform =
		platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(vkms_device->platform)) {
		ret = PTR_ERR(vkms_device->platform);
		goto out_fini;
	}

	drm_mode_config_init(&vkms_device->drm);

	ret = drm_connector_init(&vkms_device->drm, &vkms_device->connector,
				 &vkms_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret < 0) {
		DRM_ERROR("Failed to init connector\n");
		goto out_unregister;
	}

	ret = drm_simple_display_pipe_init(&vkms_device->drm,
					   &vkms_device->pipe,
					   NULL,
					   vkms_formats,
					   ARRAY_SIZE(vkms_formats),
					   NULL,
					   &vkms_device->connector);
	if (ret < 0) {
		DRM_ERROR("Cannot setup simple display pipe\n");
		goto out_unregister;
	}

	ret = drm_dev_register(&vkms_device->drm, 0);
	if (ret)
		goto out_unregister;

	drm_connector_register(&vkms_device->connector);

	return 0;

out_unregister:
	platform_device_unregister(vkms_device->platform);
out_fini:
	drm_dev_fini(&vkms_device->drm);
out_free:
	kfree(vkms_device);

	return ret;
}

static void __exit vkms_exit(void)
{
	if (!vkms_device) {
		DRM_INFO("vkms_device is NULL.\n");
		return;
	}

	drm_dev_unregister(&vkms_device->drm);
	drm_dev_put(&vkms_device->drm);

	kfree(vkms_device);
}

module_init(vkms_init);
module_exit(vkms_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
