// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob.clark@linaro.org>
 */

#include <linux/seq_file.h>

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>

#include "omap_drv.h"
#include "omap_dmm_tiler.h"

#ifdef CONFIG_DEBUG_FS

static int gem_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct omap_drm_private *priv = dev->dev_private;

	seq_printf(m, "All Objects:\n");
	mutex_lock(&priv->list_lock);
	omap_gem_describe_objects(&priv->obj_list, m);
	mutex_unlock(&priv->list_lock);

	return 0;
}

static int mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_mm_print(&dev->vma_offset_manager->vm_addr_space_mm, &p);

	return 0;
}

#ifdef CONFIG_DRM_FBDEV_EMULATION
static int fb_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_framebuffer *fb;

	seq_printf(m, "fbcon ");
	omap_framebuffer_describe(priv->fbdev->fb, m);

	mutex_lock(&dev->mode_config.fb_lock);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		if (fb == priv->fbdev->fb)
			continue;

		seq_printf(m, "user ");
		omap_framebuffer_describe(fb, m);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}
#endif

/* list of debufs files that are applicable to all devices */
static struct drm_info_list omap_debugfs_list[] = {
	{"gem", gem_show, 0},
	{"mm", mm_show, 0},
#ifdef CONFIG_DRM_FBDEV_EMULATION
	{"fb", fb_show, 0},
#endif
};

/* list of debugfs files that are specific to devices with dmm/tiler */
static struct drm_info_list omap_dmm_debugfs_list[] = {
	{"tiler_map", tiler_map_show, 0},
};

int omap_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(omap_debugfs_list,
			ARRAY_SIZE(omap_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install omap_debugfs_list\n");
		return ret;
	}

	if (dmm_is_available())
		ret = drm_debugfs_create_files(omap_dmm_debugfs_list,
				ARRAY_SIZE(omap_dmm_debugfs_list),
				minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install omap_dmm_debugfs_list\n");
		return ret;
	}

	return ret;
}

#endif
