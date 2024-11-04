// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt_sriov_printk.h"
#include "xe_sriov.h"
#include "xe_sriov_printk.h"
#include "xe_sriov_vf.h"

static void migration_worker_func(struct work_struct *w);

/**
 * xe_sriov_vf_init_early - Initialize SR-IOV VF specific data.
 * @xe: the &xe_device to initialize
 */
void xe_sriov_vf_init_early(struct xe_device *xe)
{
	INIT_WORK(&xe->sriov.vf.migration.worker, migration_worker_func);
}

static void vf_post_migration_recovery(struct xe_device *xe)
{
	drm_dbg(&xe->drm, "migration recovery in progress\n");
	/* FIXME: add the recovery steps */
	drm_notice(&xe->drm, "migration recovery ended\n");
}

static void migration_worker_func(struct work_struct *w)
{
	struct xe_device *xe = container_of(w, struct xe_device,
					    sriov.vf.migration.worker);

	vf_post_migration_recovery(xe);
}

static bool vf_ready_to_recovery_on_all_gts(struct xe_device *xe)
{
	struct xe_gt *gt;
	unsigned int id;

	for_each_gt(gt, xe, id) {
		if (!test_bit(id, &xe->sriov.vf.migration.gt_flags)) {
			xe_gt_sriov_dbg_verbose(gt, "still not ready to recover\n");
			return false;
		}
	}
	return true;
}

/**
 * xe_sriov_vf_start_migration_recovery - Start VF migration recovery.
 * @xe: the &xe_device to start recovery on
 *
 * This function shall be called only by VF.
 */
void xe_sriov_vf_start_migration_recovery(struct xe_device *xe)
{
	bool started;

	xe_assert(xe, IS_SRIOV_VF(xe));

	if (!vf_ready_to_recovery_on_all_gts(xe))
		return;

	WRITE_ONCE(xe->sriov.vf.migration.gt_flags, 0);
	/* Ensure other threads see that no flags are set now. */
	smp_mb();

	started = queue_work(xe->sriov.wq, &xe->sriov.vf.migration.worker);
	drm_info(&xe->drm, "VF migration recovery %s\n", started ?
		 "scheduled" : "already in progress");
}
