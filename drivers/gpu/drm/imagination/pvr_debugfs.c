// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_debugfs.h"

#include "pvr_device.h"
#include "pvr_fw_trace.h"
#include "pvr_params.h"

#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

static const struct pvr_debugfs_entry pvr_debugfs_entries[] = {
	{"pvr_params", pvr_params_debugfs_init},
	{"pvr_fw", pvr_fw_trace_debugfs_init},
};

void
pvr_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *drm_dev = minor->dev;
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	struct dentry *root = minor->debugfs_root;

	for (size_t i = 0; i < ARRAY_SIZE(pvr_debugfs_entries); ++i) {
		const struct pvr_debugfs_entry *entry = &pvr_debugfs_entries[i];
		struct dentry *dir;

		dir = debugfs_create_dir(entry->name, root);
		if (IS_ERR(dir)) {
			drm_warn(drm_dev,
				 "failed to create debugfs dir '%s' (err=%d)",
				 entry->name, (int)PTR_ERR(dir));
			continue;
		}

		entry->init(pvr_dev, dir);
	}
}

/*
 * Since all entries are created under &drm_minor->debugfs_root, there's no
 * need for a pvr_debugfs_fini() as DRM will clean up everything under its root
 * automatically.
 */
