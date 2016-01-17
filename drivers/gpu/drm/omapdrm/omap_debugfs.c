/*
 * drivers/gpu/drm/omapdrm/omap_debugfs.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "All Objects:\n");
	omap_gem_describe_objects(&priv->obj_list, m);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	return drm_mm_dump_table(m, &dev->vma_offset_manager->vm_addr_space_mm);
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

void omap_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(omap_debugfs_list,
			ARRAY_SIZE(omap_debugfs_list), minor);
	if (dmm_is_available())
		drm_debugfs_remove_files(omap_dmm_debugfs_list,
				ARRAY_SIZE(omap_dmm_debugfs_list), minor);
}

#endif
