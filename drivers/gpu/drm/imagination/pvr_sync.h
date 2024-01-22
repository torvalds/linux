/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_SYNC_H
#define PVR_SYNC_H

#include <uapi/drm/pvr_drm.h>

/* Forward declaration from <linux/xarray.h>. */
struct xarray;

/* Forward declaration from <drm/drm_file.h>. */
struct drm_file;

/* Forward declaration from <drm/gpu_scheduler.h>. */
struct drm_sched_job;

/* Forward declaration from "pvr_device.h". */
struct pvr_file;

/**
 * struct pvr_sync_signal - Object encoding a syncobj signal operation
 *
 * The job submission logic collects all signal operations in an array of
 * pvr_sync_signal objects. This array also serves as a cache to get the
 * latest dma_fence when multiple jobs are submitted at once, and one job
 * signals a syncobj point that's later waited on by a subsequent job.
 */
struct pvr_sync_signal {
	/** @handle: Handle of the syncobj to signal. */
	u32 handle;

	/**
	 * @point: Point to signal in the syncobj.
	 *
	 * Only relevant for timeline syncobjs.
	 */
	u64 point;

	/** @syncobj: Syncobj retrieved from the handle. */
	struct drm_syncobj *syncobj;

	/**
	 * @chain: Chain object used to link the new fence with the
	 *	   existing timeline syncobj.
	 *
	 * Should be zero when manipulating a regular syncobj.
	 */
	struct dma_fence_chain *chain;

	/**
	 * @fence: New fence object to attach to the syncobj.
	 *
	 * This pointer starts with the current fence bound to
	 * the <handle,point> pair.
	 */
	struct dma_fence *fence;
};

void
pvr_sync_signal_array_cleanup(struct xarray *array);

int
pvr_sync_signal_array_collect_ops(struct xarray *array,
				  struct drm_file *file,
				  u32 sync_op_count,
				  const struct drm_pvr_sync_op *sync_ops);

int
pvr_sync_signal_array_update_fences(struct xarray *array,
				    u32 sync_op_count,
				    const struct drm_pvr_sync_op *sync_ops,
				    struct dma_fence *done_fence);

void
pvr_sync_signal_array_push_fences(struct xarray *array);

int
pvr_sync_add_deps_to_job(struct pvr_file *pvr_file, struct drm_sched_job *job,
			 u32 sync_op_count,
			 const struct drm_pvr_sync_op *sync_ops,
			 struct xarray *signal_array);

#endif /* PVR_SYNC_H */
