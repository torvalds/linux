// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_devcoredump.h"
#include "xe_devcoredump_types.h"

#include <linux/devcoredump.h>
#include <generated/utsrelease.h>

#include "xe_engine.h"
#include "xe_gt.h"

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

static ssize_t xe_devcoredump_read(char *buffer, loff_t offset,
				   size_t count, void *data, size_t datalen)
{
	struct xe_devcoredump *coredump = data;
	struct xe_devcoredump_snapshot *ss;
	struct drm_printer p;
	struct drm_print_iterator iter;
	struct timespec64 ts;

	iter.data = buffer;
	iter.offset = 0;
	iter.start = offset;
	iter.remain = count;

	ss = &coredump->snapshot;
	p = drm_coredump_printer(&iter);

	drm_printf(&p, "**** Xe Device Coredump ****\n");
	drm_printf(&p, "kernel: " UTS_RELEASE "\n");
	drm_printf(&p, "module: " KBUILD_MODNAME "\n");

	ts = ktime_to_timespec64(ss->snapshot_time);
	drm_printf(&p, "Snapshot time: %lld.%09ld\n", ts.tv_sec, ts.tv_nsec);
	ts = ktime_to_timespec64(ss->boot_time);
	drm_printf(&p, "Uptime: %lld.%09ld\n", ts.tv_sec, ts.tv_nsec);

	return count - iter.remain;
}

static void xe_devcoredump_free(void *data)
{
	struct xe_devcoredump *coredump = data;

	coredump->captured = false;
	drm_info(&coredump_to_xe(coredump)->drm,
		 "Xe device coredump has been deleted.\n");
}

static void devcoredump_snapshot(struct xe_devcoredump *coredump,
				 struct xe_engine *e)
{
	struct xe_devcoredump_snapshot *ss = &coredump->snapshot;

	ss->snapshot_time = ktime_get_real();
	ss->boot_time = ktime_get_boottime();
}

/**
 * xe_devcoredump - Take the required snapshots and initialize coredump device.
 * @e: The faulty xe_engine, where the issue was detected.
 *
 * This function should be called at the crash time within the serialized
 * gt_reset. It is skipped if we still have the core dump device available
 * with the information of the 'first' snapshot.
 */
void xe_devcoredump(struct xe_engine *e)
{
	struct xe_device *xe = gt_to_xe(e->gt);
	struct xe_devcoredump *coredump = &xe->devcoredump;

	if (coredump->captured) {
		drm_dbg(&xe->drm, "Multiple hangs are occurring, but only the first snapshot was taken\n");
		return;
	}

	coredump->captured = true;
	devcoredump_snapshot(coredump, e);

	drm_info(&xe->drm, "Xe device coredump has been created\n");
	drm_info(&xe->drm, "Check your /sys/class/drm/card%d/device/devcoredump/data\n",
		 xe->drm.primary->index);

	dev_coredumpm(xe->drm.dev, THIS_MODULE, coredump, 0, GFP_KERNEL,
		      xe_devcoredump_read, xe_devcoredump_free);
}
#endif
