// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>

#include "xe_device_types.h"
#include "xe_tile_sriov_pf_debugfs.h"
#include "xe_sriov.h"

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov		# d_inode->i_private = (xe_device*)
 *      │   ├── pf		# d_inode->i_private = (xe_device*)
 *      │   │   ├── tile0	# d_inode->i_private = (xe_tile*)
 *      │   │   ├── tile1
 *      │   │   :   :
 *      │   ├── vf1		# d_inode->i_private = VFID(1)
 *      │   │   ├── tile0	# d_inode->i_private = (xe_tile*)
 *      │   │   ├── tile1
 *      │   │   :   :
 *      │   ├── vfN		# d_inode->i_private = VFID(N)
 *      │   │   ├── tile0	# d_inode->i_private = (xe_tile*)
 *      │   │   ├── tile1
 *      :   :   :   :
 */

static void *extract_priv(struct dentry *d)
{
	return d->d_inode->i_private;
}

__maybe_unused
static struct xe_tile *extract_tile(struct dentry *d)
{
	return extract_priv(d);
}

static struct xe_device *extract_xe(struct dentry *d)
{
	return extract_priv(d->d_parent->d_parent);
}

__maybe_unused
static unsigned int extract_vfid(struct dentry *d)
{
	void *pp = extract_priv(d->d_parent);

	return pp == extract_xe(d) ? PFID : (uintptr_t)pp;
}

/**
 * xe_tile_sriov_pf_debugfs_populate() - Populate SR-IOV debugfs tree with tile files.
 * @tile: the &xe_tile to register
 * @parent: the parent &dentry that represents the SR-IOV @vfid function
 * @vfid: the VF identifier
 *
 * Add to the @parent directory new debugfs directory that will represent a @tile and
 * populate it with files that are related to the SR-IOV @vfid function.
 *
 * This function can only be called on PF.
 */
void xe_tile_sriov_pf_debugfs_populate(struct xe_tile *tile, struct dentry *parent,
				       unsigned int vfid)
{
	struct xe_device *xe = tile->xe;
	struct dentry *dent;
	char name[10]; /* should be enough up to "tile%u\0" for 2^16 - 1 */

	xe_tile_assert(tile, IS_SRIOV_PF(xe));
	xe_tile_assert(tile, extract_priv(parent->d_parent) == xe);
	xe_tile_assert(tile, extract_priv(parent) == tile->xe ||
		       (uintptr_t)extract_priv(parent) == vfid);

	/*
	 *      /sys/kernel/debug/dri/BDF/
	 *      ├── sriov
	 *      │   ├── pf		# parent, d_inode->i_private = (xe_device*)
	 *      │   │   ├── tile0	# d_inode->i_private = (xe_tile*)
	 *      │   │   ├── tile1
	 *      │   │   :   :
	 *      │   ├── vf1		# parent, d_inode->i_private = VFID(1)
	 *      │   │   ├── tile0	# d_inode->i_private = (xe_tile*)
	 *      │   │   ├── tile1
	 *      :   :   :   :
	 */
	snprintf(name, sizeof(name), "tile%u", tile->id);
	dent = debugfs_create_dir(name, parent);
	if (IS_ERR(dent))
		return;
	dent->d_inode->i_private = tile;

	xe_tile_assert(tile, extract_tile(dent) == tile);
	xe_tile_assert(tile, extract_vfid(dent) == vfid);
	xe_tile_assert(tile, extract_xe(dent) == xe);
}
