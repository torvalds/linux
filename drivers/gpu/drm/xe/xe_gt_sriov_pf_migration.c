// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_sriov_abi.h"
#include "xe_bo.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_sriov.h"

/* Return: number of dwords saved/restored/required or a negative error code on failure */
static int guc_action_vf_save_restore(struct xe_guc *guc, u32 vfid, u32 opcode,
				      u64 addr, u32 ndwords)
{
	u32 request[PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_SAVE_RESTORE_VF) |
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_0_OPCODE, opcode),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_1_VFID, vfid),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_2_ADDR_LO, lower_32_bits(addr)),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_3_ADDR_HI, upper_32_bits(addr)),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_4_SIZE, ndwords),
	};

	return xe_guc_ct_send_block(&guc->ct, request, ARRAY_SIZE(request));
}

/* Return: size of the state in dwords or a negative error code on failure */
static int pf_send_guc_query_vf_state_size(struct xe_gt *gt, unsigned int vfid)
{
	int ret;

	ret = guc_action_vf_save_restore(&gt->uc.guc, vfid, GUC_PF_OPCODE_VF_SAVE, 0, 0);
	return ret ?: -ENODATA;
}

/* Return: number of state dwords saved or a negative error code on failure */
static int pf_send_guc_save_vf_state(struct xe_gt *gt, unsigned int vfid,
				     void *buff, size_t size)
{
	const int ndwords = size / sizeof(u32);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_guc *guc = &gt->uc.guc;
	struct xe_bo *bo;
	int ret;

	xe_gt_assert(gt, size % sizeof(u32) == 0);
	xe_gt_assert(gt, size == ndwords * sizeof(u32));

	bo = xe_bo_create_pin_map(xe, tile, NULL,
				  ALIGN(size, PAGE_SIZE),
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM |
				  XE_BO_FLAG_GGTT |
				  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = guc_action_vf_save_restore(guc, vfid, GUC_PF_OPCODE_VF_SAVE,
					 xe_bo_ggtt_addr(bo), ndwords);
	if (!ret)
		ret = -ENODATA;
	else if (ret > ndwords)
		ret = -EPROTO;
	else if (ret > 0)
		xe_map_memcpy_from(xe, buff, &bo->vmap, 0, ret * sizeof(u32));

	xe_bo_unpin_map_no_vm(bo);
	return ret;
}

/* Return: number of state dwords restored or a negative error code on failure */
static int pf_send_guc_restore_vf_state(struct xe_gt *gt, unsigned int vfid,
					const void *buff, size_t size)
{
	const int ndwords = size / sizeof(u32);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_guc *guc = &gt->uc.guc;
	struct xe_bo *bo;
	int ret;

	xe_gt_assert(gt, size % sizeof(u32) == 0);
	xe_gt_assert(gt, size == ndwords * sizeof(u32));

	bo = xe_bo_create_pin_map(xe, tile, NULL,
				  ALIGN(size, PAGE_SIZE),
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM |
				  XE_BO_FLAG_GGTT |
				  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	xe_map_memcpy_to(xe, &bo->vmap, 0, buff, size);

	ret = guc_action_vf_save_restore(guc, vfid, GUC_PF_OPCODE_VF_RESTORE,
					 xe_bo_ggtt_addr(bo), ndwords);
	if (!ret)
		ret = -ENODATA;
	else if (ret > ndwords)
		ret = -EPROTO;

	xe_bo_unpin_map_no_vm(bo);
	return ret;
}

static bool pf_migration_supported(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	return gt->sriov.pf.migration.supported;
}

static struct mutex *pf_migration_mutex(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	return &gt->sriov.pf.migration.snapshot_lock;
}

static struct xe_gt_sriov_state_snapshot *pf_pick_vf_snapshot(struct xe_gt *gt,
							      unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));
	lockdep_assert_held(pf_migration_mutex(gt));

	return &gt->sriov.pf.vfs[vfid].snapshot;
}

static unsigned int pf_snapshot_index(struct xe_gt *gt, struct xe_gt_sriov_state_snapshot *snapshot)
{
	return container_of(snapshot, struct xe_gt_sriov_metadata, snapshot) - gt->sriov.pf.vfs;
}

static void pf_free_guc_state(struct xe_gt *gt, struct xe_gt_sriov_state_snapshot *snapshot)
{
	struct xe_device *xe = gt_to_xe(gt);

	drmm_kfree(&xe->drm, snapshot->guc.buff);
	snapshot->guc.buff = NULL;
	snapshot->guc.size = 0;
}

static int pf_alloc_guc_state(struct xe_gt *gt,
			      struct xe_gt_sriov_state_snapshot *snapshot,
			      size_t size)
{
	struct xe_device *xe = gt_to_xe(gt);
	void *p;

	pf_free_guc_state(gt, snapshot);

	if (!size)
		return -ENODATA;

	if (size % sizeof(u32))
		return -EINVAL;

	if (size > SZ_2M)
		return -EFBIG;

	p = drmm_kzalloc(&xe->drm, size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	snapshot->guc.buff = p;
	snapshot->guc.size = size;
	return 0;
}

static void pf_dump_guc_state(struct xe_gt *gt, struct xe_gt_sriov_state_snapshot *snapshot)
{
	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV)) {
		unsigned int vfid __maybe_unused = pf_snapshot_index(gt, snapshot);

		xe_gt_sriov_dbg_verbose(gt, "VF%u GuC state is %zu dwords:\n",
					vfid, snapshot->guc.size / sizeof(u32));
		print_hex_dump_bytes("state: ", DUMP_PREFIX_OFFSET,
				     snapshot->guc.buff, min(SZ_64, snapshot->guc.size));
	}
}

static int pf_save_vf_guc_state(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_state_snapshot *snapshot = pf_pick_vf_snapshot(gt, vfid);
	size_t size;
	int ret;

	ret = pf_send_guc_query_vf_state_size(gt, vfid);
	if (ret < 0)
		goto fail;
	size = ret * sizeof(u32);
	xe_gt_sriov_dbg_verbose(gt, "VF%u state size is %d dwords (%zu bytes)\n", vfid, ret, size);

	ret = pf_alloc_guc_state(gt, snapshot, size);
	if (ret < 0)
		goto fail;

	ret = pf_send_guc_save_vf_state(gt, vfid, snapshot->guc.buff, size);
	if (ret < 0)
		goto fail;
	size = ret * sizeof(u32);
	xe_gt_assert(gt, size);
	xe_gt_assert(gt, size <= snapshot->guc.size);
	snapshot->guc.size = size;

	pf_dump_guc_state(gt, snapshot);
	return 0;

fail:
	xe_gt_sriov_dbg(gt, "Unable to save VF%u state (%pe)\n", vfid, ERR_PTR(ret));
	pf_free_guc_state(gt, snapshot);
	return ret;
}

/**
 * xe_gt_sriov_pf_migration_save_guc_state() - Take a GuC VF state snapshot.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_save_guc_state(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	if (!pf_migration_supported(gt))
		return -ENOPKG;

	mutex_lock(pf_migration_mutex(gt));
	err = pf_save_vf_guc_state(gt, vfid);
	mutex_unlock(pf_migration_mutex(gt));

	return err;
}

static int pf_restore_vf_guc_state(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_state_snapshot *snapshot = pf_pick_vf_snapshot(gt, vfid);
	int ret;

	if (!snapshot->guc.size)
		return -ENODATA;

	xe_gt_sriov_dbg_verbose(gt, "restoring %zu dwords of VF%u GuC state\n",
				snapshot->guc.size / sizeof(u32), vfid);
	ret = pf_send_guc_restore_vf_state(gt, vfid, snapshot->guc.buff, snapshot->guc.size);
	if (ret < 0)
		goto fail;

	xe_gt_sriov_dbg_verbose(gt, "restored %d dwords of VF%u GuC state\n", ret, vfid);
	return 0;

fail:
	xe_gt_sriov_dbg(gt, "Failed to restore VF%u GuC state (%pe)\n", vfid, ERR_PTR(ret));
	return ret;
}

/**
 * xe_gt_sriov_pf_migration_restore_guc_state() - Restore a GuC VF state.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_restore_guc_state(struct xe_gt *gt, unsigned int vfid)
{
	int ret;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	if (!pf_migration_supported(gt))
		return -ENOPKG;

	mutex_lock(pf_migration_mutex(gt));
	ret = pf_restore_vf_guc_state(gt, vfid);
	mutex_unlock(pf_migration_mutex(gt));

	return ret;
}

static bool pf_check_migration_support(struct xe_gt *gt)
{
	/* GuC 70.25 with save/restore v2 is required */
	xe_gt_assert(gt, GUC_FIRMWARE_VER(&gt->uc.guc) >= MAKE_GUC_VER(70, 25, 0));

	/* XXX: for now this is for feature enabling only */
	return IS_ENABLED(CONFIG_DRM_XE_DEBUG);
}

/**
 * xe_gt_sriov_pf_migration_init() - Initialize support for VF migration.
 * @gt: the &xe_gt
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	gt->sriov.pf.migration.supported = pf_check_migration_support(gt);

	if (!pf_migration_supported(gt))
		return 0;

	err = drmm_mutex_init(&xe->drm, &gt->sriov.pf.migration.snapshot_lock);
	if (err)
		return err;

	return 0;
}
