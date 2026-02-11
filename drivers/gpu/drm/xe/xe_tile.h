/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_TILE_H_
#define _XE_TILE_H_

#include "xe_device_types.h"

struct xe_pagemap;
struct xe_tile;

int xe_tile_init_early(struct xe_tile *tile, struct xe_device *xe, u8 id);
int xe_tile_init_noalloc(struct xe_tile *tile);
int xe_tile_init(struct xe_tile *tile);

int xe_tile_alloc_vram(struct xe_tile *tile);

void xe_tile_migrate_wait(struct xe_tile *tile);

static inline bool xe_tile_is_root(struct xe_tile *tile)
{
	return tile->id == 0;
}

/**
 * xe_tile_to_vr() - Return the struct xe_vram_region pointer from a
 * struct xe_tile pointer
 * @tile: Pointer to the struct xe_tile.
 *
 * Return: Pointer to the struct xe_vram_region embedded in *@tile.
 */
static inline struct xe_vram_region *xe_tile_to_vr(struct xe_tile *tile)
{
	return tile->mem.vram;
}

#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)
struct drm_pagemap *xe_tile_local_pagemap(struct xe_tile *tile);
#else
static inline struct drm_pagemap *xe_tile_local_pagemap(struct xe_tile *tile)
{
	return NULL;
}
#endif
#endif
