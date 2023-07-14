// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 *  Written from the i915 driver.
 */

#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>

#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"

static void armada_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fbh = info->par;

	drm_fb_helper_fini(fbh);

	fbh->fb->funcs->destroy(fbh->fb);

	drm_client_release(&fbh->client);
	drm_fb_helper_unprepare(fbh);
	kfree(fbh);
}

static const struct fb_ops armada_fb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IO_OPS,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_destroy	= armada_fbdev_fb_destroy,
};

static int armada_fbdev_create(struct drm_fb_helper *fbh,
	struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fbh->dev;
	struct drm_mode_fb_cmd2 mode;
	struct armada_framebuffer *dfb;
	struct armada_gem_object *obj;
	struct fb_info *info;
	int size, ret;
	void *ptr;

	memset(&mode, 0, sizeof(mode));
	mode.width = sizes->surface_width;
	mode.height = sizes->surface_height;
	mode.pitches[0] = armada_pitch(mode.width, sizes->surface_bpp);
	mode.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
					sizes->surface_depth);

	size = mode.pitches[0] * mode.height;
	obj = armada_gem_alloc_private_object(dev, size);
	if (!obj) {
		DRM_ERROR("failed to allocate fb memory\n");
		return -ENOMEM;
	}

	ret = armada_gem_linear_back(dev, obj);
	if (ret) {
		drm_gem_object_put(&obj->obj);
		return ret;
	}

	ptr = armada_gem_map_object(dev, obj);
	if (!ptr) {
		drm_gem_object_put(&obj->obj);
		return -ENOMEM;
	}

	dfb = armada_framebuffer_create(dev, &mode, obj);

	/*
	 * A reference is now held by the framebuffer object if
	 * successful, otherwise this drops the ref for the error path.
	 */
	drm_gem_object_put(&obj->obj);

	if (IS_ERR(dfb))
		return PTR_ERR(dfb);

	info = drm_fb_helper_alloc_info(fbh);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_fballoc;
	}

	info->fbops = &armada_fb_ops;
	info->fix.smem_start = obj->phys_addr;
	info->fix.smem_len = obj->obj.size;
	info->screen_size = obj->obj.size;
	info->screen_base = ptr;
	fbh->fb = &dfb->fb;

	drm_fb_helper_fill_info(info, fbh, sizes);

	DRM_DEBUG_KMS("allocated %dx%d %dbpp fb: 0x%08llx\n",
		dfb->fb.width, dfb->fb.height, dfb->fb.format->cpp[0] * 8,
		(unsigned long long)obj->phys_addr);

	return 0;

 err_fballoc:
	dfb->fb.funcs->destroy(&dfb->fb);
	return ret;
}

static int armada_fb_probe(struct drm_fb_helper *fbh,
	struct drm_fb_helper_surface_size *sizes)
{
	int ret = 0;

	if (!fbh->fb) {
		ret = armada_fbdev_create(fbh, sizes);
		if (ret == 0)
			ret = 1;
	}
	return ret;
}

static const struct drm_fb_helper_funcs armada_fb_helper_funcs = {
	.fb_probe	= armada_fb_probe,
};

/*
 * Fbdev client and struct drm_client_funcs
 */

static void armada_fbdev_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fbh = drm_fb_helper_from_client(client);

	if (fbh->info) {
		drm_fb_helper_unregister_info(fbh);
	} else {
		drm_client_release(&fbh->client);
		drm_fb_helper_unprepare(fbh);
		kfree(fbh);
	}
}

static int armada_fbdev_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int armada_fbdev_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fbh = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	int ret;

	if (dev->fb_helper)
		return drm_fb_helper_hotplug_event(dev->fb_helper);

	ret = drm_fb_helper_init(dev, fbh);
	if (ret)
		goto err_drm_err;

	if (!drm_drv_uses_atomic_modeset(dev))
		drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fbh);
	if (ret)
		goto err_drm_fb_helper_fini;

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fbh);
err_drm_err:
	drm_err(dev, "armada: Failed to setup fbdev emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs armada_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= armada_fbdev_client_unregister,
	.restore	= armada_fbdev_client_restore,
	.hotplug	= armada_fbdev_client_hotplug,
};

void armada_fbdev_setup(struct drm_device *dev)
{
	struct drm_fb_helper *fbh;
	int ret;

	drm_WARN(dev, !dev->registered, "Device has not been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	fbh = kzalloc(sizeof(*fbh), GFP_KERNEL);
	if (!fbh)
		return;
	drm_fb_helper_prepare(dev, fbh, 32, &armada_fb_helper_funcs);

	ret = drm_client_init(dev, &fbh->client, "fbdev", &armada_fbdev_client_funcs);
	if (ret) {
		drm_err(dev, "Failed to register client: %d\n", ret);
		goto err_drm_client_init;
	}

	drm_client_register(&fbh->client);

	return;

err_drm_client_init:
	drm_fb_helper_unprepare(fbh);
	kfree(fbh);
	return;
}
