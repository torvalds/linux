// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright © 2014 Broadcom
 */

#include <drm/drm_drv.h>

#include <linux/seq_file.h>
#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>

#include "vc4_drv.h"
#include "vc4_regs.h"

/*
 * Called at drm_dev_register() time on each of the minors registered
 * by the DRM device, to attach the debugfs files.
 */
void
vc4_debugfs_init(struct drm_minor *minor)
{
	struct vc4_dev *vc4 = to_vc4_dev(minor->dev);
	struct drm_device *drm = &vc4->base;

	drm_WARN_ON(drm, vc4_hvs_debugfs_init(minor));

	if (vc4->v3d) {
		drm_WARN_ON(drm, vc4_bo_debugfs_init(minor));
		drm_WARN_ON(drm, vc4_v3d_debugfs_init(minor));
	}
}

static int vc4_debugfs_regset32(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *drm = node->minor->dev;
	struct debugfs_regset32 *regset = node->info_ent->data;
	struct drm_printer p = drm_seq_file_printer(m);
	int idx;

	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;

	drm_print_regset32(&p, regset);

	drm_dev_exit(idx);

	return 0;
}

int vc4_debugfs_add_file(struct drm_minor *minor,
			 const char *name,
			 int (*show)(struct seq_file*, void*),
			 void *data)
{
	struct drm_device *dev = minor->dev;
	struct dentry *root = minor->debugfs_root;
	struct drm_info_list *file;

	file = drmm_kzalloc(dev, sizeof(*file), GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	file->name = name;
	file->show = show;
	file->data = data;

	drm_debugfs_create_files(file, 1, root, minor);

	return 0;
}

int vc4_debugfs_add_regset32(struct drm_minor *minor,
			     const char *name,
			     struct debugfs_regset32 *regset)
{
	return vc4_debugfs_add_file(minor, name, vc4_debugfs_regset32, regset);
}
