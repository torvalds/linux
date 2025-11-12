// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_sriov_packet_types.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_migration.h"
#include "xe_sriov_printk.h"

static struct xe_sriov_migration_state *pf_pick_migration(struct xe_device *xe, unsigned int vfid)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid <= xe_sriov_pf_get_totalvfs(xe));

	return &xe->sriov.pf.vfs[vfid].migration;
}

/**
 * xe_sriov_pf_migration_waitqueue() - Get waitqueue for migration.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * Return: pointer to the migration waitqueue.
 */
wait_queue_head_t *xe_sriov_pf_migration_waitqueue(struct xe_device *xe, unsigned int vfid)
{
	return &pf_pick_migration(xe, vfid)->wq;
}

/**
 * xe_sriov_pf_migration_supported() - Check if SR-IOV VF migration is supported by the device
 * @xe: the &xe_device
 *
 * Return: true if migration is supported, false otherwise
 */
bool xe_sriov_pf_migration_supported(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	return xe->sriov.pf.migration.supported;
}

static bool pf_check_migration_support(struct xe_device *xe)
{
	/* XXX: for now this is for feature enabling only */
	return IS_ENABLED(CONFIG_DRM_XE_DEBUG);
}

/**
 * xe_sriov_pf_migration_init() - Initialize support for SR-IOV VF migration.
 * @xe: the &xe_device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_migration_init(struct xe_device *xe)
{
	unsigned int n, totalvfs;

	xe_assert(xe, IS_SRIOV_PF(xe));

	xe->sriov.pf.migration.supported = pf_check_migration_support(xe);
	if (!xe_sriov_pf_migration_supported(xe))
		return 0;

	totalvfs = xe_sriov_pf_get_totalvfs(xe);
	for (n = 1; n <= totalvfs; n++) {
		struct xe_sriov_migration_state *migration = pf_pick_migration(xe, n);

		init_waitqueue_head(&migration->wq);
	}

	return 0;
}

static bool pf_migration_data_ready(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	u8 gt_id;

	for_each_gt(gt, xe, gt_id) {
		if (xe_gt_sriov_pf_control_check_save_failed(gt, vfid) ||
		    xe_gt_sriov_pf_control_check_save_data_done(gt, vfid) ||
		    !xe_gt_sriov_pf_migration_ring_empty(gt, vfid))
			return true;
	}

	return false;
}

static struct xe_sriov_packet *
pf_migration_consume(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_packet *data;
	bool more_data = false;
	struct xe_gt *gt;
	u8 gt_id;

	for_each_gt(gt, xe, gt_id) {
		data = xe_gt_sriov_pf_migration_save_consume(gt, vfid);
		if (data && PTR_ERR(data) != EAGAIN)
			return data;
		if (PTR_ERR(data) == -EAGAIN)
			more_data = true;
	}

	if (!more_data)
		return NULL;

	return ERR_PTR(-EAGAIN);
}

/**
 * xe_sriov_pf_migration_save_consume() - Consume a VF migration data packet from the device.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * Called by the save migration data consumer (userspace) when
 * processing migration data.
 * If there is no migration data to process, wait until more data is available.
 *
 * Return: Pointer to &xe_sriov_packet on success,
 *	   NULL if ring is empty and no more migration data is expected,
 *	   ERR_PTR value in case of error.
 *
 * Return: 0 on success or a negative error code on failure.
 */
struct xe_sriov_packet *
xe_sriov_pf_migration_save_consume(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_migration_state *migration = pf_pick_migration(xe, vfid);
	struct xe_sriov_packet *data;
	int ret;

	xe_assert(xe, IS_SRIOV_PF(xe));

	for (;;) {
		data = pf_migration_consume(xe, vfid);
		if (PTR_ERR(data) != -EAGAIN)
			break;

		ret = wait_event_interruptible(migration->wq,
					       pf_migration_data_ready(xe, vfid));
		if (ret)
			return ERR_PTR(ret);
	}

	return data;
}

/**
 * xe_sriov_pf_migration_restore_produce() - Produce a VF migration data packet to the device.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @data: Pointer to &xe_sriov_packet
 *
 * Called by the restore migration data producer (userspace) when processing
 * migration data.
 * If the underlying data structure is full, wait until there is space.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_migration_restore_produce(struct xe_device *xe, unsigned int vfid,
					  struct xe_sriov_packet *data)
{
	struct xe_gt *gt;

	xe_assert(xe, IS_SRIOV_PF(xe));

	gt = xe_device_get_gt(xe, data->hdr.gt_id);
	if (!gt || data->hdr.tile_id != gt->tile->id || data->hdr.type == 0) {
		xe_sriov_err_ratelimited(xe, "Received invalid restore packet for VF%u (type:%u, tile:%u, GT:%u)\n",
					 vfid, data->hdr.type, data->hdr.tile_id, data->hdr.gt_id);
		return -EINVAL;
	}

	return xe_gt_sriov_pf_migration_restore_produce(gt, vfid, data);
}
