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
#include <linux/device/faux.h>
#include <linux/dma-mapping.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_vblank.h>

#include "vkms_config.h"
#include "vkms_drv.h"

#define DRIVER_NAME	"vkms"
#define DRIVER_DESC	"Virtual Kernel Mode Setting"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct vkms_config *default_config;

static bool enable_cursor = true;
module_param_named(enable_cursor, enable_cursor, bool, 0444);
MODULE_PARM_DESC(enable_cursor, "Enable/Disable cursor support");

static bool enable_writeback = true;
module_param_named(enable_writeback, enable_writeback, bool, 0444);
MODULE_PARM_DESC(enable_writeback, "Enable/Disable writeback connector support");

static bool enable_overlay;
module_param_named(enable_overlay, enable_overlay, bool, 0444);
MODULE_PARM_DESC(enable_overlay, "Enable/Disable overlay support");

DEFINE_DRM_GEM_FOPS(vkms_driver_fops);

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
		struct vkms_crtc_state *vkms_state = to_vkms_crtc_state(old_crtc_state);

		flush_work(&vkms_state->composer_work);
	}

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static const struct drm_driver vkms_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.fops			= &vkms_driver_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,

	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
};

static int vkms_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->gamma_lut || !new_crtc_state->color_mgmt_changed)
			continue;

		if (new_crtc_state->gamma_lut->length / sizeof(struct drm_color_lut *)
		    > VKMS_LUT_SIZE)
			return -EINVAL;
	}

	return drm_atomic_helper_check(dev, state);
}

static const struct drm_mode_config_funcs vkms_mode_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = vkms_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs vkms_mode_config_helpers = {
	.atomic_commit_tail = vkms_atomic_commit_tail,
};

static int vkms_modeset_init(struct vkms_device *vkmsdev)
{
	struct drm_device *dev = &vkmsdev->drm;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.funcs = &vkms_mode_funcs;
	dev->mode_config.min_width = XRES_MIN;
	dev->mode_config.min_height = YRES_MIN;
	dev->mode_config.max_width = XRES_MAX;
	dev->mode_config.max_height = YRES_MAX;
	dev->mode_config.cursor_width = 512;
	dev->mode_config.cursor_height = 512;
	/*
	 * FIXME: There's a confusion between bpp and depth between this and
	 * fbdev helpers. We have to go with 0, meaning "pick the default",
	 * which is XRGB8888 in all cases.
	 */
	dev->mode_config.preferred_depth = 0;
	dev->mode_config.helper_private = &vkms_mode_config_helpers;

	return vkms_output_init(vkmsdev);
}

static int vkms_create(struct vkms_config *config)
{
	int ret;
	struct faux_device *fdev;
	struct vkms_device *vkms_device;
	const char *dev_name;

	dev_name = vkms_config_get_device_name(config);
	fdev = faux_device_create(dev_name, NULL, NULL);
	if (!fdev)
		return -ENODEV;

	if (!devres_open_group(&fdev->dev, NULL, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	vkms_device = devm_drm_dev_alloc(&fdev->dev, &vkms_driver,
					 struct vkms_device, drm);
	if (IS_ERR(vkms_device)) {
		ret = PTR_ERR(vkms_device);
		goto out_devres;
	}
	vkms_device->faux_dev = fdev;
	vkms_device->config = config;
	config->dev = vkms_device;

	ret = dma_coerce_mask_and_coherent(vkms_device->drm.dev,
					   DMA_BIT_MASK(64));

	if (ret) {
		DRM_ERROR("Could not initialize DMA support\n");
		goto out_devres;
	}

	ret = drm_vblank_init(&vkms_device->drm,
			      vkms_config_get_num_crtcs(config));
	if (ret) {
		DRM_ERROR("Failed to vblank\n");
		goto out_devres;
	}

	ret = vkms_modeset_init(vkms_device);
	if (ret)
		goto out_devres;

	vkms_config_register_debugfs(vkms_device);

	ret = drm_dev_register(&vkms_device->drm, 0);
	if (ret)
		goto out_devres;

	drm_client_setup(&vkms_device->drm, NULL);

	return 0;

out_devres:
	devres_release_group(&fdev->dev, NULL);
out_unregister:
	faux_device_destroy(fdev);
	return ret;
}

static int __init vkms_init(void)
{
	int ret;
	struct vkms_config *config;

	config = vkms_config_default_create(enable_cursor, enable_writeback, enable_overlay);
	if (IS_ERR(config))
		return PTR_ERR(config);

	ret = vkms_create(config);
	if (ret) {
		vkms_config_destroy(config);
		return ret;
	}

	default_config = config;

	return 0;
}

static void vkms_destroy(struct vkms_config *config)
{
	struct faux_device *fdev;

	if (!config->dev) {
		DRM_INFO("vkms_device is NULL.\n");
		return;
	}

	fdev = config->dev->faux_dev;

	drm_dev_unregister(&config->dev->drm);
	drm_atomic_helper_shutdown(&config->dev->drm);
	devres_release_group(&fdev->dev, NULL);
	faux_device_destroy(fdev);

	config->dev = NULL;
}

static void __exit vkms_exit(void)
{
	if (!default_config)
		return;

	vkms_destroy(default_config);
	vkms_config_destroy(default_config);
}

module_init(vkms_init);
module_exit(vkms_exit);

MODULE_AUTHOR("Haneen Mohammed <hamohammed.sa@gmail.com>");
MODULE_AUTHOR("Rodrigo Siqueira <rodrigosiqueiramelo@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
