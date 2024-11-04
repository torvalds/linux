// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt_sriov_printk.h"
#include "xe_gt_sriov_vf.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_sriov_printk.h"
#include "xe_sriov_vf.h"

/**
 * DOC: VF restore procedure in PF KMD and VF KMD
 *
 * Restoring previously saved state of a VF is one of core features of
 * SR-IOV. All major VM Management applications allow saving and restoring
 * the VM state, and doing that to a VM which uses SRIOV VF as one of
 * the accessible devices requires support from KMD on both PF and VF side.
 * VMM initiates all required operations through VFIO module, which then
 * translates them into PF KMD calls. This description will focus on these
 * calls, leaving out the module which initiates these steps (VFIO).
 *
 * In order to start the restore procedure, GuC needs to keep the VF in
 * proper state. The PF driver can ensure GuC set it to VF_READY state
 * by provisioning the VF, which in turn can be done after Function Level
 * Reset of said VF (or after it was freshly created - in that case FLR
 * is not needed). The FLR procedure ends with GuC sending message
 * `GUC_PF_NOTIFY_VF_FLR_DONE`, and then provisioning data is sent to GuC.
 * After the provisioning is completed, the VF needs to be paused, and
 * at that point the actual restore can begin.
 *
 * During VF Restore, state of several resources is restored. These may
 * include local memory content (system memory is restored by VMM itself),
 * values of MMIO registers, stateless compression metadata and others.
 * The final resource which also needs restoring is state of the VF
 * submission maintained within GuC. For that, `GUC_PF_OPCODE_VF_RESTORE`
 * message is used, with reference to the state blob to be consumed by
 * GuC.
 *
 * Next, when VFIO is asked to set the VM into running state, the PF driver
 * sends `GUC_PF_TRIGGER_VF_RESUME` to GuC. When sent after restore, this
 * changes VF state within GuC to `VF_RESFIX_BLOCKED` rather than the
 * usual `VF_RUNNING`. At this point GuC triggers an interrupt to inform
 * the VF KMD within the VM that it was migrated.
 *
 * As soon as Virtual GPU of the VM starts, the VF driver within receives
 * the MIGRATED interrupt and schedules post-migration recovery worker.
 * That worker queries GuC for new provisioning (using MMIO communication),
 * and applies fixups to any non-virtualized resources used by the VF.
 *
 * When the VF driver is ready to continue operation on the newly connected
 * hardware, it sends `VF2GUC_NOTIFY_RESFIX_DONE` which causes it to
 * enter the long awaited `VF_RUNNING` state, and therefore start handling
 * CTB messages and scheduling workloads from the VF::
 *
 *      PF                             GuC                              VF
 *     [ ]                              |                               |
 *     [ ] PF2GUC_VF_CONTROL(pause)     |                               |
 *     [ ]---------------------------> [ ]                              |
 *     [ ]                             [ ]  GuC sets new VF state to    |
 *     [ ]                             [ ]------- VF_READY_PAUSED       |
 *     [ ]                             [ ]      |                       |
 *     [ ]                             [ ] <-----                       |
 *     [ ] success                     [ ]                              |
 *     [ ] <---------------------------[ ]                              |
 *     [ ]                              |                               |
 *     [ ] PF loads resources from the  |                               |
 *     [ ]------- saved image supplied  |                               |
 *     [ ]      |                       |                               |
 *     [ ] <-----                       |                               |
 *     [ ]                              |                               |
 *     [ ] GUC_PF_OPCODE_VF_RESTORE     |                               |
 *     [ ]---------------------------> [ ]                              |
 *     [ ]                             [ ]  GuC loads contexts and CTB  |
 *     [ ]                             [ ]------- state from image      |
 *     [ ]                             [ ]      |                       |
 *     [ ]                             [ ] <-----                       |
 *     [ ]                             [ ]                              |
 *     [ ]                             [ ]  GuC sets new VF state to    |
 *     [ ]                             [ ]------- VF_RESFIX_PAUSED      |
 *     [ ]                             [ ]      |                       |
 *     [ ] success                     [ ] <-----                       |
 *     [ ] <---------------------------[ ]                              |
 *     [ ]                              |                               |
 *     [ ] GUC_PF_TRIGGER_VF_RESUME     |                               |
 *     [ ]---------------------------> [ ]                              |
 *     [ ]                             [ ]  GuC sets new VF state to    |
 *     [ ]                             [ ]------- VF_RESFIX_BLOCKED     |
 *     [ ]                             [ ]      |                       |
 *     [ ]                             [ ] <-----                       |
 *     [ ]                             [ ]                              |
 *     [ ]                             [ ] GUC_INTR_SW_INT_0            |
 *     [ ] success                     [ ]---------------------------> [ ]
 *     [ ] <---------------------------[ ]                             [ ]
 *      |                               |      VF2GUC_QUERY_SINGLE_KLV [ ]
 *      |                              [ ] <---------------------------[ ]
 *      |                              [ ]                             [ ]
 *      |                              [ ]        new VF provisioning  [ ]
 *      |                              [ ]---------------------------> [ ]
 *      |                               |                              [ ]
 *      |                               |       VF driver applies post [ ]
 *      |                               |      migration fixups -------[ ]
 *      |                               |                       |      [ ]
 *      |                               |                       -----> [ ]
 *      |                               |                              [ ]
 *      |                               |    VF2GUC_NOTIFY_RESFIX_DONE [ ]
 *      |                              [ ] <---------------------------[ ]
 *      |                              [ ]                             [ ]
 *      |                              [ ]  GuC sets new VF state to   [ ]
 *      |                              [ ]------- VF_RUNNING           [ ]
 *      |                              [ ]      |                      [ ]
 *      |                              [ ] <-----                      [ ]
 *      |                              [ ]                     success [ ]
 *      |                              [ ]---------------------------> [ ]
 *      |                               |                               |
 *      |                               |                               |
 */

static void migration_worker_func(struct work_struct *w);

/**
 * xe_sriov_vf_init_early - Initialize SR-IOV VF specific data.
 * @xe: the &xe_device to initialize
 */
void xe_sriov_vf_init_early(struct xe_device *xe)
{
	INIT_WORK(&xe->sriov.vf.migration.worker, migration_worker_func);
}

/*
 * Notify all GuCs about resource fixups apply finished.
 */
static void vf_post_migration_notify_resfix_done(struct xe_device *xe)
{
	struct xe_gt *gt;
	unsigned int id;

	for_each_gt(gt, xe, id) {
		xe_gt_sriov_vf_notify_resfix_done(gt);
	}
}

static void vf_post_migration_recovery(struct xe_device *xe)
{
	drm_dbg(&xe->drm, "migration recovery in progress\n");
	xe_pm_runtime_get(xe);
	/* FIXME: add the recovery steps */
	vf_post_migration_notify_resfix_done(xe);
	xe_pm_runtime_put(xe);
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
