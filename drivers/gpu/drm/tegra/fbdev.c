// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2013 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Based on the KMS/FB DMA helpers
 *   Copyright (C) 2012 Analog Devices Inc.
 */

#include <linux/console.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>

#include <drm/drm_drv.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>

#include "drm.h"
#include "gem.h"

static int tegra_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct tegra_bo *bo;
	int err;

	bo = tegra_fb_get_plane(helper->fb, 0);

	err = drm_gem_mmap_obj(&bo->gem, bo->gem.size, vma);
	if (err < 0)
		return err;

	return __tegra_gem_mmap(&bo->gem, vma);
}

static void tegra_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_framebuffer *fb = helper->fb;
	struct tegra_bo *bo = tegra_fb_get_plane(fb, 0);

	drm_fb_helper_fini(helper);

	/* Undo the special mapping we made in fbdev probe. */
	if (bo->pages) {
		vunmap(bo->vaddr);
		bo->vaddr = NULL;
	}
	drm_framebuffer_remove(fb);

	drm_client_release(&helper->client);
	drm_fb_helper_unprepare(helper);
	kfree(helper);
}

static const struct fb_ops tegra_fb_ops = {
	.owner = THIS_MODULE,
	__FB_DEFAULT_DMAMEM_OPS_RDWR,
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DMAMEM_OPS_DRAW,
	.fb_mmap = tegra_fb_mmap,
	.fb_destroy = tegra_fbdev_fb_destroy,
};

static int tegra_fbdev_probe(struct drm_fb_helper *helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct tegra_drm *tegra = helper->dev->dev_private;
	struct drm_device *drm = helper->dev;
	struct drm_mode_fb_cmd2 cmd = { 0 };
	unsigned int bytes_per_pixel;
	struct drm_framebuffer *fb;
	unsigned long offset;
	struct fb_info *info;
	struct tegra_bo *bo;
	size_t size;
	int err;

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	cmd.width = sizes->surface_width;
	cmd.height = sizes->surface_height;
	cmd.pitches[0] = round_up(sizes->surface_width * bytes_per_pixel,
				  tegra->pitch_align);

	cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
						     sizes->surface_depth);

	size = cmd.pitches[0] * cmd.height;

	bo = tegra_bo_create(drm, size, 0);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	info = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(info)) {
		dev_err(drm->dev, "failed to allocate framebuffer info\n");
		drm_gem_object_put(&bo->gem);
		return PTR_ERR(info);
	}

	fb = tegra_fb_alloc(drm, &cmd, &bo, 1);
	if (IS_ERR(fb)) {
		err = PTR_ERR(fb);
		dev_err(drm->dev, "failed to allocate DRM framebuffer: %d\n",
			err);
		drm_gem_object_put(&bo->gem);
		return PTR_ERR(fb);
	}

	helper->fb = fb;
	helper->info = info;

	info->fbops = &tegra_fb_ops;

	drm_fb_helper_fill_info(info, helper, sizes);

	offset = info->var.xoffset * bytes_per_pixel +
		 info->var.yoffset * fb->pitches[0];

	if (bo->pages) {
		bo->vaddr = vmap(bo->pages, bo->num_pages, VM_MAP,
				 pgprot_writecombine(PAGE_KERNEL));
		if (!bo->vaddr) {
			dev_err(drm->dev, "failed to vmap() framebuffer\n");
			err = -ENOMEM;
			goto destroy;
		}
	}

	info->flags |= FBINFO_VIRTFB;
	info->screen_buffer = bo->vaddr + offset;
	info->screen_size = size;
	info->fix.smem_start = (unsigned long)(bo->iova + offset);
	info->fix.smem_len = size;

	return 0;

destroy:
	drm_framebuffer_remove(fb);
	return err;
}

static const struct drm_fb_helper_funcs tegra_fb_helper_funcs = {
	.fb_probe = tegra_fbdev_probe,
};

/*
 * struct drm_client
 */

static void tegra_fbdev_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);

	if (fb_helper->info) {
		drm_fb_helper_unregister_info(fb_helper);
	} else {
		drm_client_release(&fb_helper->client);
		drm_fb_helper_unprepare(fb_helper);
		kfree(fb_helper);
	}
}

static int tegra_fbdev_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int tegra_fbdev_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	int ret;

	if (dev->fb_helper)
		return drm_fb_helper_hotplug_event(dev->fb_helper);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto err_drm_err;

	if (!drm_drv_uses_atomic_modeset(dev))
		drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto err_drm_fb_helper_fini;

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_drm_err:
	drm_err(dev, "Failed to setup fbdev emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs tegra_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= tegra_fbdev_client_unregister,
	.restore	= tegra_fbdev_client_restore,
	.hotplug	= tegra_fbdev_client_hotplug,
};

void tegra_fbdev_setup(struct drm_device *dev)
{
	struct drm_fb_helper *helper;
	int ret;

	drm_WARN(dev, !dev->registered, "Device has not been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	helper = kzalloc(sizeof(*helper), GFP_KERNEL);
	if (!helper)
		return;
	drm_fb_helper_prepare(dev, helper, 32, &tegra_fb_helper_funcs);

	ret = drm_client_init(dev, &helper->client, "fbdev", &tegra_fbdev_client_funcs);
	if (ret)
		goto err_drm_client_init;

	drm_client_register(&helper->client);

	return;

err_drm_client_init:
	drm_fb_helper_unprepare(helper);
	kfree(helper);
}
