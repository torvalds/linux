// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <drm/drm_managed.h>

#include "xe_tile.h"
#include "xe_tile_sysfs.h"
#include "xe_vram_freq.h"

static void xe_tile_sysfs_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_type xe_tile_sysfs_kobj_type = {
	.release = xe_tile_sysfs_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static void tile_sysfs_fini(struct drm_device *drm, void *arg)
{
	struct xe_tile *tile = arg;

	kobject_put(tile->sysfs);
}

void xe_tile_sysfs_init(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct device *dev = xe->drm.dev;
	struct kobj_tile *kt;
	int err;

	kt = kzalloc(sizeof(*kt), GFP_KERNEL);
	if (!kt)
		return;

	kobject_init(&kt->base, &xe_tile_sysfs_kobj_type);
	kt->tile = tile;

	err = kobject_add(&kt->base, &dev->kobj, "tile%d", tile->id);
	if (err) {
		kobject_put(&kt->base);
		drm_warn(&xe->drm, "failed to register TILE sysfs directory, err: %d\n", err);
		return;
	}

	tile->sysfs = &kt->base;

	xe_vram_freq_sysfs_init(tile);

	err = drmm_add_action_or_reset(&xe->drm, tile_sysfs_fini, tile);
	if (err)
		drm_warn(&xe->drm, "%s: drmm_add_action_or_reset failed, err: %d\n",
			 __func__, err);
}
