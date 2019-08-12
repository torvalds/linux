// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright © 2014 Broadcom
 */

#include <linux/seq_file.h>
#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <drm/drmP.h>

#include "vc4_drv.h"
#include "vc4_regs.h"

struct vc4_debugfs_info_entry {
	struct list_head link;
	struct drm_info_list info;
};

/**
 * Called at drm_dev_register() time on each of the minors registered
 * by the DRM device, to attach the debugfs files.
 */
int
vc4_debugfs_init(struct drm_minor *minor)
{
	struct vc4_dev *vc4 = to_vc4_dev(minor->dev);
	struct vc4_debugfs_info_entry *entry;

	debugfs_create_bool("hvs_load_tracker", S_IRUGO | S_IWUSR,
			    minor->debugfs_root, &vc4->load_tracker_enabled);

	list_for_each_entry(entry, &vc4->debugfs_list, link) {
		int ret = drm_debugfs_create_files(&entry->info, 1,
						   minor->debugfs_root, minor);

		if (ret)
			return ret;
	}

	return 0;
}

static int vc4_debugfs_regset32(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct debugfs_regset32 *regset = node->info_ent->data;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_print_regset32(&p, regset);

	return 0;
}

/**
 * Registers a debugfs file with a callback function for a vc4 component.
 *
 * This is like drm_debugfs_create_files(), but that can only be
 * called a given DRM minor, while the various VC4 components want to
 * register their debugfs files during the component bind process.  We
 * track the request and delay it to be called on each minor during
 * vc4_debugfs_init().
 */
void vc4_debugfs_add_file(struct drm_device *dev,
			  const char *name,
			  int (*show)(struct seq_file*, void*),
			  void *data)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	struct vc4_debugfs_info_entry *entry =
		devm_kzalloc(dev->dev, sizeof(*entry), GFP_KERNEL);

	if (!entry)
		return;

	entry->info.name = name;
	entry->info.show = show;
	entry->info.data = data;

	list_add(&entry->link, &vc4->debugfs_list);
}

void vc4_debugfs_add_regset32(struct drm_device *drm,
			      const char *name,
			      struct debugfs_regset32 *regset)
{
	vc4_debugfs_add_file(drm, name, vc4_debugfs_regset32, regset);
}
