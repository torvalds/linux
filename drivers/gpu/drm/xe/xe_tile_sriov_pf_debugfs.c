// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_debugfs.h"
#include "xe_pm.h"
#include "xe_tile_debugfs.h"
#include "xe_tile_sriov_pf_debugfs.h"
#include "xe_sriov.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_provision.h"

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

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov
 *      :   ├── pf
 *          :   ├── tile0
 *              :   ├── ggtt_available
 *                  ├── ggtt_provisioned
 */

static int pf_config_print_available_ggtt(struct xe_tile *tile, struct drm_printer *p)
{
	return xe_gt_sriov_pf_config_print_available_ggtt(tile->primary_gt, p);
}

static int pf_config_print_ggtt(struct xe_tile *tile, struct drm_printer *p)
{
	return xe_gt_sriov_pf_config_print_ggtt(tile->primary_gt, p);
}

static const struct drm_info_list pf_ggtt_info[] = {
	{
		"ggtt_available",
		.show = xe_tile_debugfs_simple_show,
		.data = pf_config_print_available_ggtt,
	},
	{
		"ggtt_provisioned",
		.show = xe_tile_debugfs_simple_show,
		.data = pf_config_print_ggtt,
	},
};

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov
 *      :   ├── pf
 *          :   ├── tile0
 *              :   ├── vram_provisioned
 */

static int pf_config_print_vram(struct xe_tile *tile, struct drm_printer *p)
{
	return xe_gt_sriov_pf_config_print_lmem(tile->primary_gt, p);
}

static const struct drm_info_list pf_vram_info[] = {
	{
		"vram_provisioned",
		.show = xe_tile_debugfs_simple_show,
		.data = pf_config_print_vram,
	},
};

/*
 *      /sys/kernel/debug/dri/BDF/
 *      ├── sriov
 *      │   ├── pf
 *      │   │   ├── tile0
 *      │   │   │   ├── ggtt_spare
 *      │   │   │   ├── vram_spare
 *      │   │   ├── tile1
 *      │   │   :   :
 *      │   ├── vf1
 *      │   :   ├── tile0
 *      │       │   ├── ggtt_quota
 *      │       │   ├── vram_quota
 *      │       ├── tile1
 *      │       :   :
 */

#define DEFINE_SRIOV_TILE_CONFIG_DEBUGFS_ATTRIBUTE(NAME, CONFIG, TYPE, FORMAT)	\
										\
static int NAME##_set(void *data, u64 val)					\
{										\
	struct xe_tile *tile = extract_tile(data);				\
	unsigned int vfid = extract_vfid(data);					\
	struct xe_gt *gt = tile->primary_gt;					\
	struct xe_device *xe = tile->xe;					\
	int err;								\
										\
	if (val > (TYPE)~0ull)							\
		return -EOVERFLOW;						\
										\
	xe_pm_runtime_get(xe);							\
	err = xe_sriov_pf_wait_ready(xe) ?:					\
	      xe_gt_sriov_pf_config_set_##CONFIG(gt, vfid, val);		\
	if (!err)								\
		xe_sriov_pf_provision_set_custom_mode(xe);			\
	xe_pm_runtime_put(xe);							\
										\
	return err;								\
}										\
										\
static int NAME##_get(void *data, u64 *val)					\
{										\
	struct xe_tile *tile = extract_tile(data);				\
	unsigned int vfid = extract_vfid(data);					\
	struct xe_gt *gt = tile->primary_gt;					\
										\
	*val = xe_gt_sriov_pf_config_get_##CONFIG(gt, vfid);			\
	return 0;								\
}										\
										\
DEFINE_DEBUGFS_ATTRIBUTE(NAME##_fops, NAME##_get, NAME##_set, FORMAT)

DEFINE_SRIOV_TILE_CONFIG_DEBUGFS_ATTRIBUTE(ggtt, ggtt, u64, "%llu\n");
DEFINE_SRIOV_TILE_CONFIG_DEBUGFS_ATTRIBUTE(vram, lmem, u64, "%llu\n");

static void pf_add_config_attrs(struct xe_tile *tile, struct dentry *dent, unsigned int vfid)
{
	struct xe_device *xe = tile->xe;

	xe_tile_assert(tile, tile == extract_tile(dent));
	xe_tile_assert(tile, vfid == extract_vfid(dent));

	debugfs_create_file_unsafe(vfid ? "ggtt_quota" : "ggtt_spare",
				   0644, dent, dent, &ggtt_fops);
	if (IS_DGFX(xe))
		debugfs_create_file_unsafe(vfid ? "vram_quota" : "vram_spare",
					   xe_device_has_lmtt(xe) ? 0644 : 0444,
					   dent, dent, &vram_fops);
}

static void pf_populate_tile(struct xe_tile *tile, struct dentry *dent, unsigned int vfid)
{
	struct xe_device *xe = tile->xe;
	struct drm_minor *minor = xe->drm.primary;
	struct xe_gt *gt;
	unsigned int id;

	pf_add_config_attrs(tile, dent, vfid);

	if (!vfid) {
		drm_debugfs_create_files(pf_ggtt_info,
					 ARRAY_SIZE(pf_ggtt_info),
					 dent, minor);
		if (IS_DGFX(xe))
			drm_debugfs_create_files(pf_vram_info,
						 ARRAY_SIZE(pf_vram_info),
						 dent, minor);
	}

	for_each_gt_on_tile(gt, tile, id)
		xe_gt_sriov_pf_debugfs_populate(gt, dent, vfid);
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

	pf_populate_tile(tile, dent, vfid);
}
