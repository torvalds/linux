/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_DEBUGFS_H
#define PVR_DEBUGFS_H

/* Forward declaration from <drm/drm_drv.h>. */
struct drm_minor;

#if defined(CONFIG_DEBUG_FS)
/* Forward declaration from "pvr_device.h". */
struct pvr_device;

/* Forward declaration from <linux/dcache.h>. */
struct dentry;

struct pvr_debugfs_entry {
	const char *name;
	void (*init)(struct pvr_device *pvr_dev, struct dentry *dir);
};

void pvr_debugfs_init(struct drm_minor *minor);
#else /* defined(CONFIG_DEBUG_FS) */
#include <linux/compiler_attributes.h>

static __always_inline void pvr_debugfs_init(struct drm_minor *minor) {}
#endif /* defined(CONFIG_DEBUG_FS) */

#endif /* PVR_DEBUGFS_H */
