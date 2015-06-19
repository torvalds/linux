/**
 * \file drm_info.c
 * DRM info file implementations
 *
 * \author Ben Gamari <bgamari@gmail.com>
 */

/*
 * Created: Sun Dec 21 13:09:50 2008 by bgamari@gmail.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright 2008 Ben Gamari <bgamari@gmail.com>
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
#include <drm/drmP.h>
#include <drm/drm_gem.h>

#include "drm_internal.h"
#include "drm_legacy.h"

/**
 * Called when "/proc/dri/.../name" is read.
 *
 * Prints the device name together with the bus id if available.
 */
int drm_name_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *dev = minor->dev;
	struct drm_master *master = minor->master;
	if (!master)
		return 0;

	if (master->unique) {
		seq_printf(m, "%s %s %s\n",
			   dev->driver->name,
			   dev_name(dev->dev), master->unique);
	} else {
		seq_printf(m, "%s %s\n",
			   dev->driver->name, dev_name(dev->dev));
	}
	return 0;
}

/**
 * Called when "/proc/dri/.../vm" is read.
 *
 * Prints information about all mappings in drm_device::maplist.
 */
int drm_vm_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_local_map *map;
	struct drm_map_list *r_list;

	/* Hardcoded from _DRM_FRAME_BUFFER,
	   _DRM_REGISTERS, _DRM_SHM, _DRM_AGP, and
	   _DRM_SCATTER_GATHER and _DRM_CONSISTENT */
	const char *types[] = { "FB", "REG", "SHM", "AGP", "SG", "PCI" };
	const char *type;
	int i;

	mutex_lock(&dev->struct_mutex);
	seq_printf(m, "slot	 offset	      size type flags	 address mtrr\n\n");
	i = 0;
	list_for_each_entry(r_list, &dev->maplist, head) {
		map = r_list->map;
		if (!map)
			continue;
		if (map->type < 0 || map->type > 5)
			type = "??";
		else
			type = types[map->type];

		seq_printf(m, "%4d 0x%016llx 0x%08lx %4.4s  0x%02x 0x%08lx ",
			   i,
			   (unsigned long long)map->offset,
			   map->size, type, map->flags,
			   (unsigned long) r_list->user_token);
		if (map->mtrr < 0)
			seq_printf(m, "none\n");
		else
			seq_printf(m, "%4d\n", map->mtrr);
		i++;
	}
	mutex_unlock(&dev->struct_mutex);
	return 0;
}

/**
 * Called when "/proc/dri/.../bufs" is read.
 */
int drm_bufs_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_device_dma *dma;
	int i, seg_pages;

	mutex_lock(&dev->struct_mutex);
	dma = dev->dma;
	if (!dma) {
		mutex_unlock(&dev->struct_mutex);
		return 0;
	}

	seq_printf(m, " o     size count  free	 segs pages    kB\n\n");
	for (i = 0; i <= DRM_MAX_ORDER; i++) {
		if (dma->bufs[i].buf_count) {
			seg_pages = dma->bufs[i].seg_count * (1 << dma->bufs[i].page_order);
			seq_printf(m, "%2d %8d %5d %5d %5d %5d %5ld\n",
				   i,
				   dma->bufs[i].buf_size,
				   dma->bufs[i].buf_count,
				   0,
				   dma->bufs[i].seg_count,
				   seg_pages,
				   seg_pages * PAGE_SIZE / 1024);
		}
	}
	seq_printf(m, "\n");
	for (i = 0; i < dma->buf_count; i++) {
		if (i && !(i % 32))
			seq_printf(m, "\n");
		seq_printf(m, " %d", dma->buflist[i]->list);
	}
	seq_printf(m, "\n");
	mutex_unlock(&dev->struct_mutex);
	return 0;
}

/**
 * Called when "/proc/dri/.../clients" is read.
 *
 */
int drm_clients_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_file *priv;

	seq_printf(m,
		   "%20s %5s %3s master a %5s %10s\n",
		   "command",
		   "pid",
		   "dev",
		   "uid",
		   "magic");

	/* dev->filelist is sorted youngest first, but we want to present
	 * oldest first (i.e. kernel, servers, clients), so walk backwardss.
	 */
	mutex_lock(&dev->struct_mutex);
	list_for_each_entry_reverse(priv, &dev->filelist, lhead) {
		struct task_struct *task;

		rcu_read_lock(); /* locks pid_task()->comm */
		task = pid_task(priv->pid, PIDTYPE_PID);
		seq_printf(m, "%20s %5d %3d   %c    %c %5d %10u\n",
			   task ? task->comm : "<unknown>",
			   pid_vnr(priv->pid),
			   priv->minor->index,
			   priv->is_master ? 'y' : 'n',
			   priv->authenticated ? 'y' : 'n',
			   from_kuid_munged(seq_user_ns(m), priv->uid),
			   priv->magic);
		rcu_read_unlock();
	}
	mutex_unlock(&dev->struct_mutex);
	return 0;
}


static int drm_gem_one_name_info(int id, void *ptr, void *data)
{
	struct drm_gem_object *obj = ptr;
	struct seq_file *m = data;

	seq_printf(m, "%6d %8zd %7d %8d\n",
		   obj->name, obj->size,
		   obj->handle_count,
		   atomic_read(&obj->refcount.refcount));
	return 0;
}

int drm_gem_name_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;

	seq_printf(m, "  name     size handles refcount\n");

	mutex_lock(&dev->object_name_lock);
	idr_for_each(&dev->object_name_idr, drm_gem_one_name_info, m);
	mutex_unlock(&dev->object_name_lock);

	return 0;
}
