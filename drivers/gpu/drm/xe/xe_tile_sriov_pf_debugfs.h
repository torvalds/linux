/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_TILE_SRIOV_PF_DEBUGFS_H_
#define _XE_TILE_SRIOV_PF_DEBUGFS_H_

struct dentry;
struct xe_tile;

void xe_tile_sriov_pf_debugfs_populate(struct xe_tile *tile, struct dentry *parent,
				       unsigned int vfid);

#endif
