// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_devcoredump.h"
#include "xe_devcoredump_types.h"

#include <linux/devcoredump.h>
#include <generated/utsrelease.h>

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_guc_ct.h"
#include "xe_guc_submit.h"
#include "xe_hw_engine.h"
#include "xe_sched_job.h"
#include "xe_vm.h"

/**
 * DOC: Xe device coredump
 *
 * Devices overview:
 * Xe uses dev_coredump infrastructure for exposing the crash errors in a
 * standardized way.
 * devcoredump exposes a temporary device under /sys/class/devcoredump/
 * which is linked with our card device directly.
 * The core dump can be accessed either from
 * /sys/class/drm/card<n>/device/devcoredump/ or from
 * /sys/class/devcoredump/devcd<m> where
 * /sys/class/devcoredump/devcd<m>/failing_device is a link to
 * /sys/class/drm/card<n>/device/.
 *
 * Snapshot at hang:
 * The 'data' file is printed with a drm_printer pointer at devcoredump read
 * time. For this reason, we need to take snapshots from when the hang has
 * happened, and not only when the user is reading the file. Otherwise the
 * information is outdated since the resets might have happened in between.
 *
 * 'First' failure snapshot:
 * In general, the first hang is the most critical one since the following hangs
 * can be a consequence of the initial hang. For this reason we only take the
 * snapshot of the 'first' failure and ignore subsequent calls of this function,
 * at least while the coredump device is alive. Dev_coredump has a delayed work
 * queue that will eventually delete the device and free all the dump
 * information.
 */

#ifdef CONFIG_DEV_COREDUMP

static struct xe_device *coredump_to_xe(const struct xe_devcoredump *coredump)
{
	return container_of(coredump, struct xe_device, devcoredump);
}

static struct xe_guc *exec_queue_to_guc(struct xe_exec_queue *q)
{
	return &q->gt->uc.guc;
}

static void xe_devcoredump_deferred_snap_work(struct work_struct *work)
{
	struct xe_devcoredump_snapshot *ss = container_of(work, typeof(*ss), work);

	/* keep going if fw fails as we still want to save the memory and SW data */
	if (xe_force_wake_get(gt_to_fw(ss->gt), XE_FORCEWAKE_ALL))
		xe_gt_info(ss->gt, "failed to get forcewake for coredump capture\n");
	xe_vm_snapshot_capture_delayed(ss->vm);
	xe_guc_exec_queue_snapshot_capture_delayed(ss->ge);
	xe_force_wake_put(gt_to_fw(ss->gt), XE_FORCEWAKE_ALL);
}

static ssize_t xe_devcoredump_read(char *buffer, loff_t offset,
				   size_t count, void *data, size_t datalen)
{
	struct xe_devcoredump *coredump = data;
	struct xe_device *xe;
	struct xe_devcoredump_snapshot *ss;
	struct drm_printer p;
	struct drm_print_iterator iter;
	struct timespec64 ts;
	int i;

	if (!coredump)
		return -ENODEV;

	xe = coredump_to_xe(coredump);
	ss = &coredump->snapshot;

	/* Ensure delayed work is captured before continuing */
	flush_work(&ss->work);

	iter.data = buffer;
	iter.offset = 0;
	iter.start = offset;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	drm_printf(&p, "**** Xe Device Coredump ****\n");
	drm_printf(&p, "kernel: " UTS_RELEASE "\n");
	drm_printf(&p, "module: " KBUILD_MODNAME "\n");

	ts = ktime_to_timespec64(ss->snapshot_time);
	drm_printf(&p, "Snapshot time: %lld.%09ld\n", ts.tv_sec, ts.tv_nsec);
	ts = ktime_to_timespec64(ss->boot_time);
	drm_printf(&p, "Uptime: %lld.%09ld\n", ts.tv_sec, ts.tv_nsec);
	drm_printf(&p, "Process: %s\n", ss->process_name);
	xe_device_snapshot_print(xe, &p);

	drm_printf(&p, "\n**** GuC CT ****\n");
	xe_guc_ct_snapshot_print(coredump->snapshot.ct, &p);
	xe_guc_exec_queue_snapshot_print(coredump->snapshot.ge, &p);

	drm_printf(&p, "\n**** Job ****\n");
	xe_sched_job_snapshot_print(coredump->snapshot.job, &p);

	drm_printf(&p, "\n**** HW Engines ****\n");
	for (i = 0; i < XE_NUM_HW_ENGINES; i++)
		if (coredump->snapshot.hwe[i])
			xe_hw_engine_snapshot_print(coredump->snapshot.hwe[i],
						    &p);
	drm_printf(&p, "\n**** VM state ****\n");
	xe_vm_snapshot_print(coredump->snapshot.vm, &p);

	return count - iter.remain;
}

static void xe_devcoredump_free(void *data)
{
	struct xe_devcoredump *coredump = data;
	int i;

	/* Our device is gone. Nothing to do... */
	if (!data || !coredump_to_xe(coredump))
		return;

	cancel_work_sync(&coredump->snapshot.work);

	xe_guc_ct_snapshot_free(coredump->snapshot.ct);
	xe_guc_exec_queue_snapshot_free(coredump->snapshot.ge);
	xe_sched_job_snapshot_free(coredump->snapshot.job);
	for (i = 0; i < XE_NUM_HW_ENGINES; i++)
		if (coredump->snapshot.hwe[i])
			xe_hw_engine_snapshot_free(coredump->snapshot.hwe[i]);
	xe_vm_snapshot_free(coredump->snapshot.vm);

	/* To prevent stale data on next snapshot, clear everything */
	memset(&coredump->snapshot, 0, sizeof(coredump->snapshot));
	coredump->captured = false;
	drm_info(&coredump_to_xe(coredump)->drm,
		 "Xe device coredump has been deleted.\n");
}

static void devcoredump_snapshot(struct xe_devcoredump *coredump,
				 struct xe_sched_job *job)
{
	struct xe_devcoredump_snapshot *ss = &coredump->snapshot;
	struct xe_exec_queue *q = job->q;
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	u32 adj_logical_mask = q->logical_mask;
	u32 width_mask = (0x1 << q->width) - 1;
	const char *process_name = "no process";
	struct task_struct *task = NULL;

	int i;
	bool cookie;

	ss->snapshot_time = ktime_get_real();
	ss->boot_time = ktime_get_boottime();

	if (q->vm) {
		task = get_pid_task(q->vm->xef->drm->pid, PIDTYPE_PID);
		if (task)
			process_name = task->comm;
	}
	snprintf(ss->process_name, sizeof(ss->process_name), process_name);
	if (task)
		put_task_struct(task);

	ss->gt = q->gt;
	INIT_WORK(&ss->work, xe_devcoredump_deferred_snap_work);

	cookie = dma_fence_begin_signalling();
	for (i = 0; q->width > 1 && i < XE_HW_ENGINE_MAX_INSTANCE;) {
		if (adj_logical_mask & BIT(i)) {
			adj_logical_mask |= width_mask << i;
			i += q->width;
		} else {
			++i;
		}
	}

	/* keep going if fw fails as we still want to save the memory and SW data */
	if (xe_force_wake_get(gt_to_fw(q->gt), XE_FORCEWAKE_ALL))
		xe_gt_info(ss->gt, "failed to get forcewake for coredump capture\n");

	coredump->snapshot.ct = xe_guc_ct_snapshot_capture(&guc->ct, true);
	coredump->snapshot.ge = xe_guc_exec_queue_snapshot_capture(q);
	coredump->snapshot.job = xe_sched_job_snapshot_capture(job);
	coredump->snapshot.vm = xe_vm_snapshot_capture(q->vm);

	for_each_hw_engine(hwe, q->gt, id) {
		if (hwe->class != q->hwe->class ||
		    !(BIT(hwe->logical_instance) & adj_logical_mask)) {
			coredump->snapshot.hwe[id] = NULL;
			continue;
		}
		coredump->snapshot.hwe[id] = xe_hw_engine_snapshot_capture(hwe);
	}

	queue_work(system_unbound_wq, &ss->work);

	xe_force_wake_put(gt_to_fw(q->gt), XE_FORCEWAKE_ALL);
	dma_fence_end_signalling(cookie);
}

/**
 * xe_devcoredump - Take the required snapshots and initialize coredump device.
 * @job: The faulty xe_sched_job, where the issue was detected.
 *
 * This function should be called at the crash time within the serialized
 * gt_reset. It is skipped if we still have the core dump device available
 * with the information of the 'first' snapshot.
 */
void xe_devcoredump(struct xe_sched_job *job)
{
	struct xe_device *xe = gt_to_xe(job->q->gt);
	struct xe_devcoredump *coredump = &xe->devcoredump;

	if (coredump->captured) {
		drm_dbg(&xe->drm, "Multiple hangs are occurring, but only the first snapshot was taken\n");
		return;
	}

	coredump->captured = true;
	devcoredump_snapshot(coredump, job);

	drm_info(&xe->drm, "Xe device coredump has been created\n");
	drm_info(&xe->drm, "Check your /sys/class/drm/card%d/device/devcoredump/data\n",
		 xe->drm.primary->index);

	dev_coredumpm(xe->drm.dev, THIS_MODULE, coredump, 0, GFP_KERNEL,
		      xe_devcoredump_read, xe_devcoredump_free);
}

static void xe_driver_devcoredump_fini(void *arg)
{
	struct drm_device *drm = arg;

	dev_coredump_put(drm->dev);
}

int xe_devcoredump_init(struct xe_device *xe)
{
	return devm_add_action_or_reset(xe->drm.dev, xe_driver_devcoredump_fini, &xe->drm);
}
#endif
