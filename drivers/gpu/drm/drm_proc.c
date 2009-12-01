/**
 * \file drm_proc.c
 * /proc support for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 *
 * \par Acknowledgements:
 *    Matthew J Sottek <matthew.j.sottek@intel.com> sent in a patch to fix
 *    the problem with the proc files not outputting all their information.
 */

/*
 * Created: Mon Jan 11 09:48:47 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
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

#include <linux/seq_file.h>
#include "drmP.h"

/***************************************************
 * Initialization, etc.
 **************************************************/

/**
 * Proc file list.
 */
static struct drm_info_list drm_proc_list[] = {
	{"name", drm_name_info, 0},
	{"vm", drm_vm_info, 0},
	{"clients", drm_clients_info, 0},
	{"queues", drm_queues_info, 0},
	{"bufs", drm_bufs_info, 0},
	{"gem_names", drm_gem_name_info, DRIVER_GEM},
	{"gem_objects", drm_gem_object_info, DRIVER_GEM},
#if DRM_DEBUG_CODE
	{"vma", drm_vma_info, 0},
#endif
};
#define DRM_PROC_ENTRIES ARRAY_SIZE(drm_proc_list)

static int drm_proc_open(struct inode *inode, struct file *file)
{
	struct drm_info_node* node = PDE(inode)->data;

	return single_open(file, node->info_ent->show, node);
}

static const struct file_operations drm_proc_fops = {
	.owner = THIS_MODULE,
	.open = drm_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


/**
 * Initialize a given set of proc files for a device
 *
 * \param files The array of files to create
 * \param count The number of files given
 * \param root DRI proc dir entry.
 * \param minor device minor number
 * \return Zero on success, non-zero on failure
 *
 * Create a given set of proc files represented by an array of
 * gdm_proc_lists in the given root directory.
 */
int drm_proc_create_files(struct drm_info_list *files, int count,
			  struct proc_dir_entry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct proc_dir_entry *ent;
	struct drm_info_node *tmp;
	char name[64];
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
		tmp->minor = minor;
		tmp->info_ent = &files[i];
		list_add(&tmp->list, &minor->proc_nodes.list);

		ent = proc_create_data(files[i].name, S_IRUGO, root,
				       &drm_proc_fops, tmp);
		if (!ent) {
			DRM_ERROR("Cannot create /proc/dri/%s/%s\n",
				  name, files[i].name);
			list_del(&tmp->list);
			kfree(tmp);
			ret = -1;
			goto fail;
		}

	}
	return 0;

fail:
	for (i = 0; i < count; i++)
		remove_proc_entry(drm_proc_list[i].name, minor->proc_root);
	return ret;
}

/**
 * Initialize the DRI proc filesystem for a device
 *
 * \param dev DRM device
 * \param minor device minor number
 * \param root DRI proc dir entry.
 * \param dev_root resulting DRI device proc dir entry.
 * \return root entry pointer on success, or NULL on failure.
 *
 * Create the DRI proc root entry "/proc/dri", the device proc root entry
 * "/proc/dri/%minor%/", and each entry in proc_list as
 * "/proc/dri/%minor%/%name%".
 */
int drm_proc_init(struct drm_minor *minor, int minor_id,
		  struct proc_dir_entry *root)
{
	struct drm_device *dev = minor->dev;
	char name[64];
	int ret;

	INIT_LIST_HEAD(&minor->proc_nodes.list);
	sprintf(name, "%d", minor_id);
	minor->proc_root = proc_mkdir(name, root);
	if (!minor->proc_root) {
		DRM_ERROR("Cannot create /proc/dri/%s\n", name);
		return -1;
	}

	ret = drm_proc_create_files(drm_proc_list, DRM_PROC_ENTRIES,
				    minor->proc_root, minor);
	if (ret) {
		remove_proc_entry(name, root);
		minor->proc_root = NULL;
		DRM_ERROR("Failed to create core drm proc files\n");
		return ret;
	}

	if (dev->driver->proc_init) {
		ret = dev->driver->proc_init(minor);
		if (ret) {
			DRM_ERROR("DRM: Driver failed to initialize "
				  "/proc/dri.\n");
			return ret;
		}
	}
	return 0;
}

int drm_proc_remove_files(struct drm_info_list *files, int count,
			  struct drm_minor *minor)
{
	struct list_head *pos, *q;
	struct drm_info_node *tmp;
	int i;

	for (i = 0; i < count; i++) {
		list_for_each_safe(pos, q, &minor->proc_nodes.list) {
			tmp = list_entry(pos, struct drm_info_node, list);
			if (tmp->info_ent == &files[i]) {
				remove_proc_entry(files[i].name,
						  minor->proc_root);
				list_del(pos);
				kfree(tmp);
			}
		}
	}
	return 0;
}

/**
 * Cleanup the proc filesystem resources.
 *
 * \param minor device minor number.
 * \param root DRI proc dir entry.
 * \param dev_root DRI device proc dir entry.
 * \return always zero.
 *
 * Remove all proc entries created by proc_init().
 */
int drm_proc_cleanup(struct drm_minor *minor, struct proc_dir_entry *root)
{
	struct drm_device *dev = minor->dev;
	char name[64];

	if (!root || !minor->proc_root)
		return 0;

	if (dev->driver->proc_cleanup)
		dev->driver->proc_cleanup(minor);

	drm_proc_remove_files(drm_proc_list, DRM_PROC_ENTRIES, minor);

	sprintf(name, "%d", minor->index);
	remove_proc_entry(name, root);

	return 0;
}

