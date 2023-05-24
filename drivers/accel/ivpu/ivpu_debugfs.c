// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include <uapi/drm/ivpu_accel.h>

#include "ivpu_debugfs.h"
#include "ivpu_drv.h"
#include "ivpu_gem.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_pm.h"

static int bo_list_show(struct seq_file *s, void *v)
{
	struct drm_info_node *node = (struct drm_info_node *)s->private;
	struct drm_printer p = drm_seq_file_printer(s);

	ivpu_bo_list(node->minor->dev, &p);

	return 0;
}

static int last_bootmode_show(struct seq_file *s, void *v)
{
	struct drm_info_node *node = (struct drm_info_node *)s->private;
	struct ivpu_device *vdev = to_ivpu_device(node->minor->dev);

	seq_printf(s, "%s\n", (vdev->pm->is_warmboot) ? "warmboot" : "coldboot");

	return 0;
}

static const struct drm_info_list vdev_debugfs_list[] = {
	{"bo_list", bo_list_show, 0},
	{"last_bootmode", last_bootmode_show, 0},
};

static ssize_t
ivpu_reset_engine_fn(struct file *file, const char __user *user_buf, size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;

	if (!size)
		return -EINVAL;

	if (ivpu_jsm_reset_engine(vdev, DRM_IVPU_ENGINE_COMPUTE))
		return -ENODEV;
	if (ivpu_jsm_reset_engine(vdev, DRM_IVPU_ENGINE_COPY))
		return -ENODEV;

	return size;
}

static const struct file_operations ivpu_reset_engine_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = ivpu_reset_engine_fn,
};

void ivpu_debugfs_init(struct drm_minor *minor)
{
	struct ivpu_device *vdev = to_ivpu_device(minor->dev);

	drm_debugfs_create_files(vdev_debugfs_list, ARRAY_SIZE(vdev_debugfs_list),
				 minor->debugfs_root, minor);

	debugfs_create_file("reset_engine", 0200, minor->debugfs_root, vdev,
			    &ivpu_reset_engine_fops);
}
