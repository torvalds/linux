// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include <uapi/drm/pvr_drm.h>

#include <drm/drm_syncobj.h>
#include <drm/gpu_scheduler.h>
#include <linux/xarray.h>
#include <linux/dma-fence-unwrap.h>

#include "pvr_device.h"
#include "pvr_queue.h"
#include "pvr_sync.h"

static int
pvr_check_sync_op(const struct drm_pvr_sync_op *sync_op)
{
	u8 handle_type;

	if (sync_op->flags & ~DRM_PVR_SYNC_OP_FLAGS_MASK)
		return -EINVAL;

	handle_type = sync_op->flags & DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK;
	if (handle_type != DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_SYNCOBJ &&
	    handle_type != DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_TIMELINE_SYNCOBJ)
		return -EINVAL;

	if (handle_type == DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_SYNCOBJ &&
	    sync_op->value != 0)
		return -EINVAL;

	return 0;
}

static void
pvr_sync_signal_free(struct pvr_sync_signal *sig_sync)
{
	if (!sig_sync)
		return;

	drm_syncobj_put(sig_sync->syncobj);
	dma_fence_chain_free(sig_sync->chain);
	dma_fence_put(sig_sync->fence);
	kfree(sig_sync);
}

void
pvr_sync_signal_array_cleanup(struct xarray *array)
{
	struct pvr_sync_signal *sig_sync;
	unsigned long i;

	xa_for_each(array, i, sig_sync)
		pvr_sync_signal_free(sig_sync);

	xa_destroy(array);
}

static struct pvr_sync_signal *
pvr_sync_signal_array_add(struct xarray *array, struct drm_file *file, u32 handle, u64 point)
{
	struct pvr_sync_signal *sig_sync;
	struct dma_fence *cur_fence;
	int err;
	u32 id;

	sig_sync = kzalloc(sizeof(*sig_sync), GFP_KERNEL);
	if (!sig_sync)
		return ERR_PTR(-ENOMEM);

	sig_sync->handle = handle;
	sig_sync->point = point;

	if (point > 0) {
		sig_sync->chain = dma_fence_chain_alloc();
		if (!sig_sync->chain) {
			err = -ENOMEM;
			goto err_free_sig_sync;
		}
	}

	sig_sync->syncobj = drm_syncobj_find(file, handle);
	if (!sig_sync->syncobj) {
		err = -EINVAL;
		goto err_free_sig_sync;
	}

	/* Retrieve the current fence attached to that point. It's
	 * perfectly fine to get a NULL fence here, it just means there's
	 * no fence attached to that point yet.
	 */
	if (!drm_syncobj_find_fence(file, handle, point, 0, &cur_fence))
		sig_sync->fence = cur_fence;

	err = xa_alloc(array, &id, sig_sync, xa_limit_32b, GFP_KERNEL);
	if (err)
		goto err_free_sig_sync;

	return sig_sync;

err_free_sig_sync:
	pvr_sync_signal_free(sig_sync);
	return ERR_PTR(err);
}

static struct pvr_sync_signal *
pvr_sync_signal_array_search(struct xarray *array, u32 handle, u64 point)
{
	struct pvr_sync_signal *sig_sync;
	unsigned long i;

	xa_for_each(array, i, sig_sync) {
		if (handle == sig_sync->handle && point == sig_sync->point)
			return sig_sync;
	}

	return NULL;
}

static struct pvr_sync_signal *
pvr_sync_signal_array_get(struct xarray *array, struct drm_file *file, u32 handle, u64 point)
{
	struct pvr_sync_signal *sig_sync;

	sig_sync = pvr_sync_signal_array_search(array, handle, point);
	if (sig_sync)
		return sig_sync;

	return pvr_sync_signal_array_add(array, file, handle, point);
}

int
pvr_sync_signal_array_collect_ops(struct xarray *array,
				  struct drm_file *file,
				  u32 sync_op_count,
				  const struct drm_pvr_sync_op *sync_ops)
{
	for (u32 i = 0; i < sync_op_count; i++) {
		struct pvr_sync_signal *sig_sync;
		int ret;

		if (!(sync_ops[i].flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL))
			continue;

		ret = pvr_check_sync_op(&sync_ops[i]);
		if (ret)
			return ret;

		sig_sync = pvr_sync_signal_array_get(array, file,
						     sync_ops[i].handle,
						     sync_ops[i].value);
		if (IS_ERR(sig_sync))
			return PTR_ERR(sig_sync);
	}

	return 0;
}

int
pvr_sync_signal_array_update_fences(struct xarray *array,
				    u32 sync_op_count,
				    const struct drm_pvr_sync_op *sync_ops,
				    struct dma_fence *done_fence)
{
	for (u32 i = 0; i < sync_op_count; i++) {
		struct dma_fence *old_fence;
		struct pvr_sync_signal *sig_sync;

		if (!(sync_ops[i].flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL))
			continue;

		sig_sync = pvr_sync_signal_array_search(array, sync_ops[i].handle,
							sync_ops[i].value);
		if (WARN_ON(!sig_sync))
			return -EINVAL;

		old_fence = sig_sync->fence;
		sig_sync->fence = dma_fence_get(done_fence);
		dma_fence_put(old_fence);

		if (WARN_ON(!sig_sync->fence))
			return -EINVAL;
	}

	return 0;
}

void
pvr_sync_signal_array_push_fences(struct xarray *array)
{
	struct pvr_sync_signal *sig_sync;
	unsigned long i;

	xa_for_each(array, i, sig_sync) {
		if (sig_sync->chain) {
			drm_syncobj_add_point(sig_sync->syncobj, sig_sync->chain,
					      sig_sync->fence, sig_sync->point);
			sig_sync->chain = NULL;
		} else {
			drm_syncobj_replace_fence(sig_sync->syncobj, sig_sync->fence);
		}
	}
}

static int
pvr_sync_add_dep_to_job(struct drm_sched_job *job, struct dma_fence *f)
{
	struct dma_fence_unwrap iter;
	u32 native_fence_count = 0;
	struct dma_fence *uf;
	int err = 0;

	dma_fence_unwrap_for_each(uf, &iter, f) {
		if (pvr_queue_fence_is_ufo_backed(uf))
			native_fence_count++;
	}

	/* No need to unwrap the fence if it's fully non-native. */
	if (!native_fence_count)
		return drm_sched_job_add_dependency(job, f);

	dma_fence_unwrap_for_each(uf, &iter, f) {
		/* There's no dma_fence_unwrap_stop() helper cleaning up the refs
		 * owned by dma_fence_unwrap(), so let's just iterate over all
		 * entries without doing anything when something failed.
		 */
		if (err)
			continue;

		if (pvr_queue_fence_is_ufo_backed(uf)) {
			struct drm_sched_fence *s_fence = to_drm_sched_fence(uf);

			/* If this is a native dependency, we wait for the scheduled fence,
			 * and we will let pvr_queue_run_job() issue FW waits.
			 */
			err = drm_sched_job_add_dependency(job,
							   dma_fence_get(&s_fence->scheduled));
		} else {
			err = drm_sched_job_add_dependency(job, dma_fence_get(uf));
		}
	}

	dma_fence_put(f);
	return err;
}

int
pvr_sync_add_deps_to_job(struct pvr_file *pvr_file, struct drm_sched_job *job,
			 u32 sync_op_count,
			 const struct drm_pvr_sync_op *sync_ops,
			 struct xarray *signal_array)
{
	int err = 0;

	if (!sync_op_count)
		return 0;

	for (u32 i = 0; i < sync_op_count; i++) {
		struct pvr_sync_signal *sig_sync;
		struct dma_fence *fence;

		if (sync_ops[i].flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL)
			continue;

		err = pvr_check_sync_op(&sync_ops[i]);
		if (err)
			return err;

		sig_sync = pvr_sync_signal_array_search(signal_array, sync_ops[i].handle,
							sync_ops[i].value);
		if (sig_sync) {
			if (WARN_ON(!sig_sync->fence))
				return -EINVAL;

			fence = dma_fence_get(sig_sync->fence);
		} else {
			err = drm_syncobj_find_fence(from_pvr_file(pvr_file), sync_ops[i].handle,
						     sync_ops[i].value, 0, &fence);
			if (err)
				return err;
		}

		err = pvr_sync_add_dep_to_job(job, fence);
		if (err)
			return err;
	}

	return 0;
}
