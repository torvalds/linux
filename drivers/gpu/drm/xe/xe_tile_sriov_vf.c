// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_gtt_defs.h"

#include "xe_assert.h"
#include "xe_ggtt.h"
#include "xe_gt_sriov_vf.h"
#include "xe_sriov.h"
#include "xe_sriov_printk.h"
#include "xe_tile_sriov_vf.h"
#include "xe_wopcm.h"

static int vf_init_ggtt_balloons(struct xe_tile *tile)
{
	struct xe_ggtt *ggtt = tile->mem.ggtt;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	tile->sriov.vf.ggtt_balloon[0] = xe_ggtt_node_init(ggtt);
	if (IS_ERR(tile->sriov.vf.ggtt_balloon[0]))
		return PTR_ERR(tile->sriov.vf.ggtt_balloon[0]);

	tile->sriov.vf.ggtt_balloon[1] = xe_ggtt_node_init(ggtt);
	if (IS_ERR(tile->sriov.vf.ggtt_balloon[1])) {
		xe_ggtt_node_fini(tile->sriov.vf.ggtt_balloon[0]);
		return PTR_ERR(tile->sriov.vf.ggtt_balloon[1]);
	}

	return 0;
}

/**
 * xe_tile_sriov_vf_balloon_ggtt_locked - Insert balloon nodes to limit used GGTT address range.
 * @tile: the &xe_tile struct instance
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_tile_sriov_vf_balloon_ggtt_locked(struct xe_tile *tile)
{
	u64 ggtt_base = xe_gt_sriov_vf_ggtt_base(tile->primary_gt);
	u64 ggtt_size = xe_gt_sriov_vf_ggtt(tile->primary_gt);
	struct xe_device *xe = tile_to_xe(tile);
	u64 wopcm = xe_wopcm_size(xe);
	u64 start, end;
	int err;

	xe_tile_assert(tile, IS_SRIOV_VF(xe));
	xe_tile_assert(tile, ggtt_size);
	lockdep_assert_held(&tile->mem.ggtt->lock);

	/*
	 * VF can only use part of the GGTT as allocated by the PF:
	 *
	 *      WOPCM                                  GUC_GGTT_TOP
	 *      |<------------ Total GGTT size ------------------>|
	 *
	 *           VF GGTT base -->|<- size ->|
	 *
	 *      +--------------------+----------+-----------------+
	 *      |////////////////////|   block  |\\\\\\\\\\\\\\\\\|
	 *      +--------------------+----------+-----------------+
	 *
	 *      |<--- balloon[0] --->|<-- VF -->|<-- balloon[1] ->|
	 */

	if (ggtt_base < wopcm || ggtt_base > GUC_GGTT_TOP ||
	    ggtt_size > GUC_GGTT_TOP - ggtt_base) {
		xe_sriov_err(xe, "tile%u: Invalid GGTT configuration: %#llx-%#llx\n",
			     tile->id, ggtt_base, ggtt_base + ggtt_size - 1);
		return -ERANGE;
	}

	start = wopcm;
	end = ggtt_base;
	if (end != start) {
		err = xe_ggtt_node_insert_balloon_locked(tile->sriov.vf.ggtt_balloon[0],
							 start, end);
		if (err)
			return err;
	}

	start = ggtt_base + ggtt_size;
	end = GUC_GGTT_TOP;
	if (end != start) {
		err = xe_ggtt_node_insert_balloon_locked(tile->sriov.vf.ggtt_balloon[1],
							 start, end);
		if (err) {
			xe_ggtt_node_remove_balloon_locked(tile->sriov.vf.ggtt_balloon[0]);
			return err;
		}
	}

	return 0;
}

static int vf_balloon_ggtt(struct xe_tile *tile)
{
	struct xe_ggtt *ggtt = tile->mem.ggtt;
	int err;

	mutex_lock(&ggtt->lock);
	err = xe_tile_sriov_vf_balloon_ggtt_locked(tile);
	mutex_unlock(&ggtt->lock);

	return err;
}

/**
 * xe_tile_sriov_vf_deballoon_ggtt_locked - Remove balloon nodes.
 * @tile: the &xe_tile struct instance
 */
void xe_tile_sriov_vf_deballoon_ggtt_locked(struct xe_tile *tile)
{
	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	xe_ggtt_node_remove_balloon_locked(tile->sriov.vf.ggtt_balloon[1]);
	xe_ggtt_node_remove_balloon_locked(tile->sriov.vf.ggtt_balloon[0]);
}

static void vf_deballoon_ggtt(struct xe_tile *tile)
{
	mutex_lock(&tile->mem.ggtt->lock);
	xe_tile_sriov_vf_deballoon_ggtt_locked(tile);
	mutex_unlock(&tile->mem.ggtt->lock);
}

static void vf_fini_ggtt_balloons(struct xe_tile *tile)
{
	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	xe_ggtt_node_fini(tile->sriov.vf.ggtt_balloon[1]);
	xe_ggtt_node_fini(tile->sriov.vf.ggtt_balloon[0]);
}

static void cleanup_ggtt(struct drm_device *drm, void *arg)
{
	struct xe_tile *tile = arg;

	vf_deballoon_ggtt(tile);
	vf_fini_ggtt_balloons(tile);
}

/**
 * xe_tile_sriov_vf_prepare_ggtt - Prepare a VF's GGTT configuration.
 * @tile: the &xe_tile
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_tile_sriov_vf_prepare_ggtt(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	int err;

	err = vf_init_ggtt_balloons(tile);
	if (err)
		return err;

	err = vf_balloon_ggtt(tile);
	if (err) {
		vf_fini_ggtt_balloons(tile);
		return err;
	}

	return drmm_add_action_or_reset(&xe->drm, cleanup_ggtt, tile);
}

/**
 * DOC: GGTT nodes shifting during VF post-migration recovery
 *
 * The first fixup applied to the VF KMD structures as part of post-migration
 * recovery is shifting nodes within &xe_ggtt instance. The nodes are moved
 * from range previously assigned to this VF, into newly provisioned area.
 * The changes include balloons, which are resized accordingly.
 *
 * The balloon nodes are there to eliminate unavailable ranges from use: one
 * reserves the GGTT area below the range for current VF, and another one
 * reserves area above.
 *
 * Below is a GGTT layout of example VF, with a certain address range assigned to
 * said VF, and inaccessible areas above and below:
 *
 *  0                                                                        4GiB
 *  |<--------------------------- Total GGTT size ----------------------------->|
 *      WOPCM                                                         GUC_TOP
 *      |<-------------- Area mappable by xe_ggtt instance ---------------->|
 *
 *  +---+---------------------------------+----------+----------------------+---+
 *  |\\\|/////////////////////////////////|  VF mem  |//////////////////////|\\\|
 *  +---+---------------------------------+----------+----------------------+---+
 *
 * Hardware enforced access rules before migration:
 *
 *  |<------- inaccessible for VF ------->|<VF owned>|<-- inaccessible for VF ->|
 *
 * GGTT nodes used for tracking allocations:
 *
 *      |<---------- balloon ------------>|<- nodes->|<----- balloon ------>|
 *
 * After the migration, GGTT area assigned to the VF might have shifted, either
 * to lower or to higher address. But we expect the total size and extra areas to
 * be identical, as migration can only happen between matching platforms.
 * Below is an example of GGTT layout of the VF after migration. Content of the
 * GGTT for VF has been moved to a new area, and we receive its address from GuC:
 *
 *  +---+----------------------+----------+---------------------------------+---+
 *  |\\\|//////////////////////|  VF mem  |/////////////////////////////////|\\\|
 *  +---+----------------------+----------+---------------------------------+---+
 *
 * Hardware enforced access rules after migration:
 *
 *  |<- inaccessible for VF -->|<VF owned>|<------- inaccessible for VF ------->|
 *
 * So the VF has a new slice of GGTT assigned, and during migration process, the
 * memory content was copied to that new area. But the &xe_ggtt nodes are still
 * tracking allocations using the old addresses. The nodes within VF owned area
 * have to be shifted, and balloon nodes need to be resized to properly mask out
 * areas not owned by the VF.
 *
 * Fixed &xe_ggtt nodes used for tracking allocations:
 *
 *     |<------ balloon ------>|<- nodes->|<----------- balloon ----------->|
 *
 * Due to use of GPU profiles, we do not expect the old and new GGTT ares to
 * overlap; but our node shifting will fix addresses properly regardless.
 */

/**
 * xe_tile_sriov_vf_fixup_ggtt_nodes - Shift GGTT allocations to match assigned range.
 * @tile: the &xe_tile struct instance
 * @shift: the shift value
 *
 * Since Global GTT is not virtualized, each VF has an assigned range
 * within the global space. This range might have changed during migration,
 * which requires all memory addresses pointing to GGTT to be shifted.
 */
void xe_tile_sriov_vf_fixup_ggtt_nodes(struct xe_tile *tile, s64 shift)
{
	struct xe_ggtt *ggtt = tile->mem.ggtt;

	mutex_lock(&ggtt->lock);

	xe_tile_sriov_vf_deballoon_ggtt_locked(tile);
	xe_ggtt_shift_nodes_locked(ggtt, shift);
	xe_tile_sriov_vf_balloon_ggtt_locked(tile);

	mutex_unlock(&ggtt->lock);
}
