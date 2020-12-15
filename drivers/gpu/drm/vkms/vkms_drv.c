// SPDX-License-Identifier: GPL-2.0+

/**
 * DOC: vkms (Virtual Kernel Modesetting)
 *
 * VKMS is a software-only model of a KMS driver that is useful for testing
 * and for running X (or similar) on headless machines. VKMS aims to enable
 * a virtual display with no need of a hardware display capability, releasing
 * the GPU in DRM API tests.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <drm/drm_gem.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_vblank.h>

#include "vkms_drv.h"

#define DRIVER_NAME	"vkms"
#define DRIVER_DESC	"Virtual Kernel Mode Setting"
#define DRIVER_DATE	"20180514"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct vkms_device *vkms_device;

bool enable_cursor = true;
module_param_named(enable_cursor, enable_cursor, bool, 0444);
MODULE_PARM_DESC(enable_cursor, "Enable/Disable cursor support");

DEFINE_DRM_GEM_FOPS(vkms_driver_fops);

static void vkms_release(struct drm_device *dev)
{
	struct vkms_device *vkms = container_of(dev, struct vkms_device, drm);

	destroy_workqueue(vkms->output.composer_workq);
}

static void vkms_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_fake_vblank(old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_wait_for_flip_done(dev, old_state);

	for_each_old_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		struct vkms_crtc_state *vkms_state =
			to_vkms_crtc_state(old_crtc_state);

		flush_work(&vkms_state->composer_work);
	}

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static const struct drm_driver vkms_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.release		= vkms_release,
	.fops			= &vkms_driver_fops,
	.gem_create_object = drm_gem_shmem_create_object_cached,
	DRM_GEM_SHMEM_DRIVER_OPS,

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

static const struct drm_mode_config_helper_funcs vkms_mode_config_helpers = {
	.atomic_commit_tail = vkms_atomic_commit_tail,
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
	dev->mode_config.cursor_width = 512;
	dev->mode_config.cursor_height = 512;
	dev->mode_config.preferred_depth = 32;
	dev->mode_config.helper_private = &vkms_mode_config_helpers;

	return vkms_output_init(vkmsdev, 0);
}

static int __init vkms_init(void)
{
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	vkms_device = devm_drm_dev_alloc(&pdev->dev, &vkms_driver,
					 struct vkms_device, drm);
	if (IS_ERR(vkms_device)) {
		ret = PTR_ERR(vkms_device);
		goto out_devres;
	}
	vkms_device->platform = pdev;

	ret = dma_coerce_mask_and_coherent(vkms_device->drm.dev,
					   DMA_BIT_MASK(64));

	if (ret) {
		DRM_ERROR("Could not initialize DMA support\n");
		goto out_devres;
	}

	vkms_device->drm.irq_enabled = true;

	ret = drm_vblank_init(&vkms_device->drm, 1);
	if (ret) {
		DRM_ERROR("Failed to vblank\n");
		goto out_devres;
	}

	ret = vkms_modeset_init(vkms_device);
	if (ret)
		goto out_devres;

	ret = drm_dev_register(&vkms_device->drm, 0);
	if (ret)
		goto out_devres;

	drm_fbdev_generic_setup(&vkms_device->drm, 0);

	return 0;

out_devres:
	devres_release_group(&pdev->dev, NULL);
out_unregister:
	platform_device_unregister(pdev);
	return ret;
}

static void __exit vkms_exit(void)
{
	struct platform_device *pdev;

	if (!vkms_device) {
		DRM_INFO("vkms_device is NULL.\n");
		return;
	}

	pdev = vkms_device->platform;

	drm_dev_unregister(&vkms_device->drm);
	drm_atomic_helper_shutdown(&vkms_device->drm);
	devres_release_group(&pdev->dev, NULL);
	platform_device_unregister(pdev);
}

module_init(vkms_init);
module_exit(vkms_exit);

MODULE_AUTHOR("Haneen Mohammed <hamohammed.sa@gmail.com>");
MODULE_AUTHOR("Rodrigo Siqueira <rodrigosiqueiramelo@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
