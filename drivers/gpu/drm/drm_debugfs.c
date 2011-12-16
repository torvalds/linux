/**
 * \file drm_debugfs.c
 * debugfs support for DRM
 *
 * \author Ben Gamari <bgamari@gmail.com>
 */

/*
 * Created: Sun Dec 21 13:08:50 2008 by bgamari@gmail.com
 *
 * Copyright 2008 Ben Gamari <bgamari@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/export.h>
#include "drmP.h"

#if defined(CONFIG_DEBUG_FS)

/***************************************************
 * Initialization, etc.
 **************************************************/

static struct drm_info_list drm_debugfs_list[] = {
	{"name", drm_name_info, 0},
	{"vm", drm_vm_info, 0},
	{"clients", drm_clients_info, 0},
	{"queues", drm_queues_info, 0},
	{"bufs", drm_bufs_info, 0},
	{"gem_names", drm_gem_name_info, DRIVER_GEM},
#if DRM_DEBUG_CODE
	{"vma", drm_vma_info, 0},
#endif
};
#define DRM_DEBUGFS_ENTRIES ARRAY_SIZE(drm_debugfs_list)


static int drm_debugfs_open(struct inode *inode, struct file *file)
{
	struct drm_info_node *node = inode->i_private;

	return single_open(file, node->info_ent->show, node);
}


static const struct file_operations drm_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = drm_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


/**
 * Initialize a given set of debugfs files for a device
 *
 * \param files The array of files to create
 * \param count The number of files given
 * \param root DRI debugfs dir entry.
 * \param minor device minor number
 * \return Zero on success, non-zero on failure
 *
 * Create a given set of debugfs files represented by an array of
 * gdm_debugfs_lists in the given root directory.
 */
int drm_debugfs_create_files(struct drm_info_list *files, int count,
			     struct dentry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct dentry *ent;
	struct drm_info_node *tmp;
	int i, ret;

	for (i = 0; i < count; i++) {
		u32 features = files[i].driver_features;

		if (features != 0 &&
		    (dev->driver->driver_features & features) != features)
			continue;

		tmp = kmalloc(sizeof(struct drm_info_node), GFP_KERNEL);
		if (tmp == NULL) {
			ret = -1;
			goto fail;
		}
		ent = debugfs_create_file(files[i].name, S_IFREG | S_IRUGO,
					  root, tmp, &drm_debugfs_fops);
		if (!ent) {
			DRM_ERROR("Cannot create /sys/kernel/debug/dri/%s/%s\n",
				  root->d_name.name, files[i].name);
			kfree(tmp);
			ret = -1;
			goto fail;
		}

		tmp->minor = minor;
		tmp->dent = ent;
		tmp->info_ent = &files[i];

		mutex_lock(&minor->debugfs_lock);
		list_add(&tmp->list, &minor->debugfs_list);
		mutex_unlock(&minor->debugfs_lock);
	}
	return 0;

fail:
	drm_debugfs_remove_files(files, count, minor);
	return ret;
}
EXPORT_SYMBOL(drm_debugfs_create_files);

/**
 * Initialize the DRI debugfs filesystem for a device
 *
 * \param dev DRM device
 * \param minor device minor number
 * \param root DRI debugfs dir entry.
 *
 * Create the DRI debugfs root entry "/sys/kernel/debug/dri", the device debugfs root entry
 * "/sys/kernel/debug/dri/%minor%/", and each entry in debugfs_list as
 * "/sys/kernel/debug/dri/%minor%/%name%".
 */
int drm_debugfs_init(struct drm_minor *minor, int minor_id,
		     struct dentry *root)
{
	struct drm_device *dev = minor->dev;
	char name[64];
	int ret;

	INIT_LIST_HEAD(&minor->debugfs_list);
	mutex_init(&minor->debugfs_lock);
	sprintf(name, "%d", minor_id);
	minor->debugfs_root = debugfs_create_dir(name, root);
	if (!minor->debugfs_root) {
		DRM_ERROR("Cannot create /sys/kernel/debug/dri/%s\n", name);
		return -1;
	}

	ret = drm_debugfs_create_files(drm_debugfs_list, DRM_DEBUGFS_ENTRIES,
				       minor->debugfs_root, minor);
	if (ret) {
		debugfs_remove(minor->debugfs_root);
		minor->debugfs_root = NULL;
		DRM_ERROR("Failed to create core drm debugfs files\n");
		return ret;
	}

	if (dev->driver->debugfs_init) {
		ret = dev->driver->debugfs_init(minor);
		if (ret) {
			DRM_ERROR("DRM: Driver failed to initialize "
				  "/sys/kernel/debug/dri.\n");
			return ret;
		}
	}
	return 0;
}


/**
 * Remove a list of debugfs files
 *
 * \param files The list of files
 * \param count The number of files
 * \param minor The minor of which we should remove the files
 * \return always zero.
 *
 * Remove all debugfs entries created by debugfs_init().
 */
int drm_debugfs_remove_files(struct drm_info_list *files, int count,
			     struct drm_minor *minor)
{
	struct list_head *pos, *q;
	struct drm_info_node *tmp;
	int i;

	mutex_lock(&minor->debugfs_lock);
	for (i = 0; i < count; i++) {
		list_for_each_safe(pos, q, &minor->debugfs_list) {
			tmp = list_entry(pos, struct drm_info_node, list);
			if (tmp->info_ent == &files[i]) {
				debugfs_remove(tmp->dent);
				list_del(pos);
				kfree(tmp);
			}
		}
	}
	mutex_unlock(&minor->debugfs_lock);
	return 0;
}
EXPORT_SYMBOL(drm_debugfs_remove_files);

/**
 * Cleanup the debugfs filesystem resources.
 *
 * \param minor device minor number.
 * \return always zero.
 *
 * Remove all debugfs entries created by debugfs_init().
 */
int drm_debugfs_cleanup(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;

	if (!minor->debugfs_root)
		return 0;

	if (dev->driver->debugfs_cleanup)
		dev->driver->debugfs_cleanup(minor);

	drm_debugfs_remove_files(drm_debugfs_list, DRM_DEBUGFS_ENTRIES, minor);

	debugfs_remove(minor->debugfs_root);
	minor->debugfs_root = NULL;

	return 0;
}

#endif /* CONFIG_DEBUG_FS */

