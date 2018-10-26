/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/**
 * DOC: vkms (Virtual Kernel Modesetting)
 *
 * vkms is a software-only model of a kms driver that is useful for testing,
 * or for running X (or similar) on headless machines and be able to still
 * use the GPU. vkms aims to enable a virtual display without the need for
 * a hardware display capability.
 */

#include <linux/module.h>
#include <drm/drm_gem.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_helper.h>
#include "vkms_drv.h"

#define DRIVER_NAME	"vkms"
#define DRIVER_DESC	"Virtual Kernel Mode Setting"
#define DRIVER_DATE	"20180514"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct vkms_device *vkms_device;

bool enable_cursor;
module_param_named(enable_cursor, enable_cursor, bool, 0444);
MODULE_PARM_DESC(enable_cursor, "Enable/Disable cursor support");

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

static const struct vm_operations_struct vkms_gem_vm_ops = {
	.fault = vkms_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static void vkms_release(struct drm_device *dev)
{
	struct vkms_device *vkms = container_of(dev, struct vkms_device, drm);

	platform_device_unregister(vkms->platform);
	drm_atomic_helper_shutdown(&vkms->drm);
	drm_mode_config_cleanup(&vkms->drm);
	drm_dev_fini(&vkms->drm);
	destroy_workqueue(vkms->output.crc_workq);
}

static struct drm_driver vkms_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.release		= vkms_release,
	.fops			= &vkms_driver_fops,
	.dumb_create		= vkms_dumb_create,
	.dumb_map_offset	= vkms_dumb_map,
	.gem_vm_ops		= &vkms_gem_vm_ops,
	.gem_free_object_unlocked = vkms_gem_free_object,
	.get_vblank_timestamp	= vkms_get_vblank_timestamp,

	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
};

static const struct drm_mode_config_funcs vkms_mode_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int vkms_modeset_init(struct vkms_device *vkmsdev)
{
	struct drm_device *dev = &vkmsdev->drm;

	drm_mode_config_init(dev);
	dev->mode_config.funcs = &vkms_mode_funcs;
	dev->mode_config.min_width = XRES_MIN;
	dev->mode_config.min_height = YRES_MIN;
	dev->mode_config.max_width = XRES_MAX;
	dev->mode_config.max_height = YRES_MAX;

	return vkms_output_init(vkmsdev);
}

static int __init vkms_init(void)
{
	int ret;

	vkms_device = kzalloc(sizeof(*vkms_device), GFP_KERNEL);
	if (!vkms_device)
		return -ENOMEM;

	vkms_device->platform =
		platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(vkms_device->platform)) {
		ret = PTR_ERR(vkms_device->platform);
		goto out_free;
	}

	ret = drm_dev_init(&vkms_device->drm, &vkms_driver,
			   &vkms_device->platform->dev);
	if (ret)
		goto out_unregister;

	vkms_device->drm.irq_enabled = true;

	ret = drm_vblank_init(&vkms_device->drm, 1);
	if (ret) {
		DRM_ERROR("Failed to vblank\n");
		goto out_fini;
	}

	ret = vkms_modeset_init(vkms_device);
	if (ret)
		goto out_fini;

	ret = drm_dev_register(&vkms_device->drm, 0);
	if (ret)
		goto out_fini;

	return 0;

out_fini:
	drm_dev_fini(&vkms_device->drm);

out_unregister:
	platform_device_unregister(vkms_device->platform);

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

MODULE_AUTHOR("Haneen Mohammed <hamohammed.sa@gmail.com>");
MODULE_AUTHOR("Rodrigo Siqueira <rodrigosiqueiramelo@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
