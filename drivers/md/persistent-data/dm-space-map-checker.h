/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef SNAPSHOTS_SPACE_MAP_CHECKER_H
#define SNAPSHOTS_SPACE_MAP_CHECKER_H

#include "dm-space-map.h"

/*----------------------------------------------------------------*/

/*
 * This space map wraps a real on-disk space map, and verifies all of its
 * operations.  It uses a lot of memory, so only use if you have a specific
 * problem that you're debugging.
 *
 * Ownership of @sm passes.
 */
struct dm_space_map *dm_sm_checker_create(struct dm_space_map *sm);
struct dm_space_map *dm_sm_checker_create_fresh(struct dm_space_map *sm);

/*----------------------------------------------------------------*/

#endif
