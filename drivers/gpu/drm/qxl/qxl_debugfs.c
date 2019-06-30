/*
 * Copyright (C) 2009 Red Hat <bskeggs@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Authors:
 *  Alon Levy <alevy@redhat.com>
 */

#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>

#include "qxl_drv.h"
#include "qxl_object.h"

#if defined(CONFIG_DEBUG_FS)
static int
qxl_debugfs_irq_received(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;

	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received));
	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received_display));
	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received_cursor));
	seq_printf(m, "%d\n", atomic_read(&qdev->irq_received_io_cmd));
	seq_printf(m, "%d\n", qdev->irq_received_error);
	return 0;
}

static int
qxl_debugfs_buffers_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct qxl_device *qdev = node->minor->dev->dev_private;
	struct qxl_bo *bo;

	list_for_each_entry(bo, &qdev->gem.objects, list) {
		struct reservation_object_list *fobj;
		int rel;

		rcu_read_lock();
		fobj = rcu_dereference(bo->tbo.resv->fence);
		rel = fobj ? fobj->shared_count : 0;
		rcu_read_unlock();

		seq_printf(m, "size %ld, pc %d, num releases %d\n",
			   (unsigned long)bo->gem_base.size,
			   bo->pin_count, rel);
	}
	return 0;
}

static struct drm_info_list qxl_debugfs_list[] = {
	{ "irq_received", qxl_debugfs_irq_received, 0, NULL },
	{ "qxl_buffers", qxl_debugfs_buffers_info, 0, NULL },
};
#define QXL_DEBUGFS_ENTRIES ARRAY_SIZE(qxl_debugfs_list)
#endif

int
qxl_debugfs_init(struct drm_minor *minor)
{
#if defined(CONFIG_DEBUG_FS)
	int r;
	struct qxl_device *dev =
		(struct qxl_device *) minor->dev->dev_private;

	drm_debugfs_create_files(qxl_debugfs_list, QXL_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);

	r = qxl_ttm_debugfs_init(dev);
	if (r) {
		DRM_ERROR("Failed to init TTM debugfs\n");
		return r;
	}
#endif
	return 0;
}

int qxl_debugfs_add_files(struct qxl_device *qdev,
			  struct drm_info_list *files,
			  unsigned int nfiles)
{
	unsigned int i;

	for (i = 0; i < qdev->debugfs_count; i++) {
		if (qdev->debugfs[i].files == files) {
			/* Already registered */
			return 0;
		}
	}

	i = qdev->debugfs_count + 1;
	if (i > QXL_DEBUGFS_MAX_COMPONENTS) {
		DRM_ERROR("Reached maximum number of debugfs components.\n");
		DRM_ERROR("Report so we increase QXL_DEBUGFS_MAX_COMPONENTS.\n");
		return -EINVAL;
	}
	qdev->debugfs[qdev->debugfs_count].files = files;
	qdev->debugfs[qdev->debugfs_count].num_files = nfiles;
	qdev->debugfs_count = i;
#if defined(CONFIG_DEBUG_FS)
	drm_debugfs_create_files(files, nfiles,
				 qdev->ddev.primary->debugfs_root,
				 qdev->ddev.primary);
#endif
	return 0;
}
