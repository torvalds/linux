/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_TILE_DEBUGFS_H_
#define _XE_TILE_DEBUGFS_H_

struct seq_file;
struct xe_tile;

void xe_tile_debugfs_register(struct xe_tile *tile);
int xe_tile_debugfs_simple_show(struct seq_file *m, void *data);
int xe_tile_debugfs_show_with_rpm(struct seq_file *m, void *data);

#endif
