/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_TILE_H_
#define _XE_TILE_H_

struct xe_tile;

int xe_tile_alloc(struct xe_tile *tile);
int xe_tile_init_noalloc(struct xe_tile *tile);

#endif
