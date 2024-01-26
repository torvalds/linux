/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_TILE_SYSFS_H_
#define _XE_TILE_SYSFS_H_

#include "xe_tile_sysfs_types.h"

void xe_tile_sysfs_init(struct xe_tile *tile);

static inline struct xe_tile *
kobj_to_tile(struct kobject *kobj)
{
	return container_of(kobj, struct kobj_tile, base)->tile;
}

#endif /* _XE_TILE_SYSFS_H_ */
