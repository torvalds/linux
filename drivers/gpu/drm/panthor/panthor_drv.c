// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd., Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_exec.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>
#include <drm/gpu_scheduler.h>
#include <drm/panthor_drm.h>

#include "panthor_device.h"
#include "panthor_fw.h"
#include "panthor_gem.h"
#include "panthor_gpu.h"
#include "panthor_heap.h"
#include "panthor_mmu.h"
#include "panthor_regs.h"
#include "panthor_sched.h"

/**
 * DOC: user <-> kernel object copy helpers.
 */

/**
 * panthor_set_uobj() - Copy kernel object to user object.
 * @usr_ptr: Users pointer.
 * @usr_size: Size of the user object.
 * @min_size: Minimum size for this object.
 * @kern_size: Size of the kernel object.
 * @in: Address of the kernel object to copy.
 *
 * Helper automating kernel -> user object copies.
 *
 * Don't use this function directly, use PANTHOR_UOBJ_SET() instead.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_set_uobj(u64 usr_ptr, u32 usr_size, u32 min_size, u32 kern_size, const void *in)
{
	/* User size shouldn't be smaller than the minimal object size. */
	if (usr_size < min_size)
		return -EINVAL;

	if (copy_to_user(u64_to_user_ptr(usr_ptr), in, min_t(u32, usr_size, kern_size)))
		return -EFAULT;

	/* When the kernel object is smaller than the user object, we fill the gap with
	 * zeros.
	 */
	if (usr_size > kern_size &&
	    clear_user(u64_to_user_ptr(usr_ptr + kern_size), usr_size - kern_size)) {
		return -EFAULT;
	}

	return 0;
}

/**
 * panthor_get_uobj_array() - Copy a user object array into a kernel accessible object array.
 * @in: The object array to copy.
 * @min_stride: Minimum array stride.
 * @obj_size: Kernel object size.
 *
 * Helper automating user -> kernel object copies.
 *
 * Don't use this function directly, use PANTHOR_UOBJ_GET_ARRAY() instead.
 *
 * Return: newly allocated object array or an ERR_PTR on error.
 */
static void *
panthor_get_uobj_array(const struct drm_panthor_obj_array *in, u32 min_stride,
		       u32 obj_size)
{
	int ret = 0;
	void *out_alloc;

	/* User stride must be at least the minimum object size, otherwise it might
	 * lack useful information.
	 */
	if (in->stride < min_stride)
		return ERR_PTR(-EINVAL);

	if (!in->count)
		return NULL;

	out_alloc = kvmalloc_array(in->count, obj_size, GFP_KERNEL);
	if (!out_alloc)
		return ERR_PTR(-ENOMEM);

	if (obj_size == in->stride) {
		/* Fast path when user/kernel have the same uAPI header version. */
		if (copy_from_user(out_alloc, u64_to_user_ptr(in->array),
				   (unsigned long)obj_size * in->count))
			ret = -EFAULT;
	} else {
		void __user *in_ptr = u64_to_user_ptr(in->array);
		void *out_ptr = out_alloc;

		/* If the sizes differ, we need to copy elements one by one. */
		for (u32 i = 0; i < in->count; i++) {
			ret = copy_struct_from_user(out_ptr, obj_size, in_ptr, in->stride);
			if (ret)
				break;

			out_ptr += obj_size;
			in_ptr += in->stride;
		}
	}

	if (ret) {
		kvfree(out_alloc);
		return ERR_PTR(ret);
	}

	return out_alloc;
}

/**
 * PANTHOR_UOBJ_MIN_SIZE_INTERNAL() - Get the minimum user object size
 * @_typename: Object type.
 * @_last_mandatory_field: Last mandatory field.
 *
 * Get the minimum user object size based on the last mandatory field name,
 * A.K.A, the name of the last field of the structure at the time this
 * structure was added to the uAPI.
 *
 * Don't use directly, use PANTHOR_UOBJ_DECL() instead.
 */
#define PANTHOR_UOBJ_MIN_SIZE_INTERNAL(_typename, _last_mandatory_field) \
	(offsetof(_typename, _last_mandatory_field) + \
	 sizeof(((_typename *)NULL)->_last_mandatory_field))

/**
 * PANTHOR_UOBJ_DECL() - Declare a new uAPI object whose subject to
 * evolutions.
 * @_typename: Object type.
 * @_last_mandatory_field: Last mandatory field.
 *
 * Should be used to extend the PANTHOR_UOBJ_MIN_SIZE() list.
 */
#define PANTHOR_UOBJ_DECL(_typename, _last_mandatory_field) \
	_typename : PANTHOR_UOBJ_MIN_SIZE_INTERNAL(_typename, _last_mandatory_field)

/**
 * PANTHOR_UOBJ_MIN_SIZE() - Get the minimum size of a given uAPI object
 * @_obj_name: Object to get the minimum size of.
 *
 * Don't use this macro directly, it's automatically called by
 * PANTHOR_UOBJ_{SET,GET_ARRAY}().
 */
#define PANTHOR_UOBJ_MIN_SIZE(_obj_name) \
	_Generic(_obj_name, \
		 PANTHOR_UOBJ_DECL(struct drm_panthor_gpu_info, tiler_present), \
		 PANTHOR_UOBJ_DECL(struct drm_panthor_csif_info, pad), \
		 PANTHOR_UOBJ_DECL(struct drm_panthor_sync_op, timeline_value), \
		 PANTHOR_UOBJ_DECL(struct drm_panthor_queue_submit, syncs), \
		 PANTHOR_UOBJ_DECL(struct drm_panthor_queue_create, ringbuf_size), \
		 PANTHOR_UOBJ_DECL(struct drm_panthor_vm_bind_op, syncs))

/**
 * PANTHOR_UOBJ_SET() - Copy a kernel object to a user object.
 * @_dest_usr_ptr: User pointer to copy to.
 * @_usr_size: Size of the user object.
 * @_src_obj: Kernel object to copy (not a pointer).
 *
 * Return: 0 on success, a negative error code otherwise.
 */
#define PANTHOR_UOBJ_SET(_dest_usr_ptr, _usr_size, _src_obj) \
	panthor_set_uobj(_dest_usr_ptr, _usr_size, \
			 PANTHOR_UOBJ_MIN_SIZE(_src_obj), \
			 sizeof(_src_obj), &(_src_obj))

/**
 * PANTHOR_UOBJ_GET_ARRAY() - Copy a user object array to a kernel accessible
 * object array.
 * @_dest_array: Local variable that will hold the newly allocated kernel
 * object array.
 * @_uobj_array: The drm_panthor_obj_array object describing the user object
 * array.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
#define PANTHOR_UOBJ_GET_ARRAY(_dest_array, _uobj_array) \
	({ \
		typeof(_dest_array) _tmp; \
		_tmp = panthor_get_uobj_array(_uobj_array, \
					      PANTHOR_UOBJ_MIN_SIZE((_dest_array)[0]), \
					      sizeof((_dest_array)[0])); \
		if (!IS_ERR(_tmp)) \
			_dest_array = _tmp; \
		PTR_ERR_OR_ZERO(_tmp); \
	})

/**
 * struct panthor_sync_signal - Represent a synchronization object point to attach
 * our job fence to.
 *
 * This structure is here to keep track of fences that are currently bound to
 * a specific syncobj point.
 *
 * At the beginning of a job submission, the fence
 * is retrieved from the syncobj itself, and can be NULL if no fence was attached
 * to this point.
 *
 * At the end, it points to the fence of the last job that had a
 * %DRM_PANTHOR_SYNC_OP_SIGNAL on this syncobj.
 *
 * With jobs being submitted in batches, the fence might change several times during
 * the process, allowing one job to wait on a job that's part of the same submission
 * but appears earlier in the drm_panthor_group_submit::queue_submits array.
 */
struct panthor_sync_signal {
	/** @node: list_head to track signal ops within a submit operation */
	struct list_head node;

	/** @handle: The syncobj handle. */
	u32 handle;

	/**
	 * @point: The syncobj point.
	 *
	 * Zero for regular syncobjs, and non-zero for timeline syncobjs.
	 */
	u64 point;

	/**
	 * @syncobj: The sync object pointed by @handle.
	 */
	struct drm_syncobj *syncobj;

	/**
	 * @chain: Chain object used to link the new fence to an existing
	 * timeline syncobj.
	 *
	 * NULL for regular syncobj, non-NULL for timeline syncobjs.
	 */
	struct dma_fence_chain *chain;

	/**
	 * @fence: The fence to assign to the syncobj or syncobj-point.
	 */
	struct dma_fence *fence;
};

/**
 * struct panthor_job_ctx - Job context
 */
struct panthor_job_ctx {
	/** @job: The job that is about to be submitted to drm_sched. */
	struct drm_sched_job *job;

	/** @syncops: Array of sync operations. */
	struct drm_panthor_sync_op *syncops;

	/** @syncop_count: Number of sync operations. */
	u32 syncop_count;
};

/**
 * struct panthor_submit_ctx - Submission context
 *
 * Anything that's related to a submission (%DRM_IOCTL_PANTHOR_VM_BIND or
 * %DRM_IOCTL_PANTHOR_GROUP_SUBMIT) is kept here, so we can automate the
 * initialization and cleanup steps.
 */
struct panthor_submit_ctx {
	/** @file: DRM file this submission happens on. */
	struct drm_file *file;

	/**
	 * @signals: List of struct panthor_sync_signal.
	 *
	 * %DRM_PANTHOR_SYNC_OP_SIGNAL operations will be recorded here,
	 * and %DRM_PANTHOR_SYNC_OP_WAIT will first check if an entry
	 * matching the syncobj+point exists before calling
	 * drm_syncobj_find_fence(). This allows us to describe dependencies
	 * existing between jobs that are part of the same batch.
	 */
	struct list_head signals;

	/** @jobs: Array of jobs. */
	struct panthor_job_ctx *jobs;

	/** @job_count: Number of entries in the @jobs array. */
	u32 job_count;

	/** @exec: drm_exec context used to acquire and prepare resv objects. */
	struct drm_exec exec;
};

#define PANTHOR_SYNC_OP_FLAGS_MASK \
	(DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK | DRM_PANTHOR_SYNC_OP_SIGNAL)

static bool sync_op_is_signal(const struct drm_panthor_sync_op *sync_op)
{
	return !!(sync_op->flags & DRM_PANTHOR_SYNC_OP_SIGNAL);
}

static bool sync_op_is_wait(const struct drm_panthor_sync_op *sync_op)
{
	/* Note that DRM_PANTHOR_SYNC_OP_WAIT == 0 */
	return !(sync_op->flags & DRM_PANTHOR_SYNC_OP_SIGNAL);
}

/**
 * panthor_check_sync_op() - Check drm_panthor_sync_op fields
 * @sync_op: The sync operation to check.
 *
 * Return: 0 on success, -EINVAL otherwise.
 */
static int
panthor_check_sync_op(const struct drm_panthor_sync_op *sync_op)
{
	u8 handle_type;

	if (sync_op->flags & ~PANTHOR_SYNC_OP_FLAGS_MASK)
		return -EINVAL;

	handle_type = sync_op->flags & DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_MASK;
	if (handle_type != DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ &&
	    handle_type != DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ)
		return -EINVAL;

	if (handle_type == DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ &&
	    sync_op->timeline_value != 0)
		return -EINVAL;

	return 0;
}

/**
 * panthor_sync_signal_free() - Release resources and free a panthor_sync_signal object
 * @sig_sync: Signal object to free.
 */
static void
panthor_sync_signal_free(struct panthor_sync_signal *sig_sync)
{
	if (!sig_sync)
		return;

	drm_syncobj_put(sig_sync->syncobj);
	dma_fence_chain_free(sig_sync->chain);
	dma_fence_put(sig_sync->fence);
	kfree(sig_sync);
}

/**
 * panthor_submit_ctx_add_sync_signal() - Add a signal operation to a submit context
 * @ctx: Context to add the signal operation to.
 * @handle: Syncobj handle.
 * @point: Syncobj point.
 *
 * Return: 0 on success, otherwise negative error value.
 */
static int
panthor_submit_ctx_add_sync_signal(struct panthor_submit_ctx *ctx, u32 handle, u64 point)
{
	struct panthor_sync_signal *sig_sync;
	struct dma_fence *cur_fence;
	int ret;

	sig_sync = kzalloc(sizeof(*sig_sync), GFP_KERNEL);
	if (!sig_sync)
		return -ENOMEM;

	sig_sync->handle = handle;
	sig_sync->point = point;

	if (point > 0) {
		sig_sync->chain = dma_fence_chain_alloc();
		if (!sig_sync->chain) {
			ret = -ENOMEM;
			goto err_free_sig_sync;
		}
	}

	sig_sync->syncobj = drm_syncobj_find(ctx->file, handle);
	if (!sig_sync->syncobj) {
		ret = -EINVAL;
		goto err_free_sig_sync;
	}

	/* Retrieve the current fence attached to that point. It's
	 * perfectly fine to get a NULL fence here, it just means there's
	 * no fence attached to that point yet.
	 */
	if (!drm_syncobj_find_fence(ctx->file, handle, point, 0, &cur_fence))
		sig_sync->fence = cur_fence;

	list_add_tail(&sig_sync->node, &ctx->signals);

	return 0;

err_free_sig_sync:
	panthor_sync_signal_free(sig_sync);
	return ret;
}

/**
 * panthor_submit_ctx_search_sync_signal() - Search an existing signal operation in a
 * submit context.
 * @ctx: Context to search the signal operation in.
 * @handle: Syncobj handle.
 * @point: Syncobj point.
 *
 * Return: A valid panthor_sync_signal object if found, NULL otherwise.
 */
static struct panthor_sync_signal *
panthor_submit_ctx_search_sync_signal(struct panthor_submit_ctx *ctx, u32 handle, u64 point)
{
	struct panthor_sync_signal *sig_sync;

	list_for_each_entry(sig_sync, &ctx->signals, node) {
		if (handle == sig_sync->handle && point == sig_sync->point)
			return sig_sync;
	}

	return NULL;
}

/**
 * panthor_submit_ctx_add_job() - Add a job to a submit context
 * @ctx: Context to search the signal operation in.
 * @idx: Index of the job in the context.
 * @job: Job to add.
 * @syncs: Sync operations provided by userspace.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_add_job(struct panthor_submit_ctx *ctx, u32 idx,
			   struct drm_sched_job *job,
			   const struct drm_panthor_obj_array *syncs)
{
	int ret;

	ctx->jobs[idx].job = job;

	ret = PANTHOR_UOBJ_GET_ARRAY(ctx->jobs[idx].syncops, syncs);
	if (ret)
		return ret;

	ctx->jobs[idx].syncop_count = syncs->count;
	return 0;
}

/**
 * panthor_submit_ctx_get_sync_signal() - Search signal operation and add one if none was found.
 * @ctx: Context to search the signal operation in.
 * @handle: Syncobj handle.
 * @point: Syncobj point.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_get_sync_signal(struct panthor_submit_ctx *ctx, u32 handle, u64 point)
{
	struct panthor_sync_signal *sig_sync;

	sig_sync = panthor_submit_ctx_search_sync_signal(ctx, handle, point);
	if (sig_sync)
		return 0;

	return panthor_submit_ctx_add_sync_signal(ctx, handle, point);
}

/**
 * panthor_submit_ctx_update_job_sync_signal_fences() - Update fences
 * on the signal operations specified by a job.
 * @ctx: Context to search the signal operation in.
 * @job_idx: Index of the job to operate on.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_update_job_sync_signal_fences(struct panthor_submit_ctx *ctx,
						 u32 job_idx)
{
	struct panthor_device *ptdev = container_of(ctx->file->minor->dev,
						    struct panthor_device,
						    base);
	struct dma_fence *done_fence = &ctx->jobs[job_idx].job->s_fence->finished;
	const struct drm_panthor_sync_op *sync_ops = ctx->jobs[job_idx].syncops;
	u32 sync_op_count = ctx->jobs[job_idx].syncop_count;

	for (u32 i = 0; i < sync_op_count; i++) {
		struct dma_fence *old_fence;
		struct panthor_sync_signal *sig_sync;

		if (!sync_op_is_signal(&sync_ops[i]))
			continue;

		sig_sync = panthor_submit_ctx_search_sync_signal(ctx, sync_ops[i].handle,
								 sync_ops[i].timeline_value);
		if (drm_WARN_ON(&ptdev->base, !sig_sync))
			return -EINVAL;

		old_fence = sig_sync->fence;
		sig_sync->fence = dma_fence_get(done_fence);
		dma_fence_put(old_fence);

		if (drm_WARN_ON(&ptdev->base, !sig_sync->fence))
			return -EINVAL;
	}

	return 0;
}

/**
 * panthor_submit_ctx_collect_job_signal_ops() - Iterate over all job signal operations
 * and add them to the context.
 * @ctx: Context to search the signal operation in.
 * @job_idx: Index of the job to operate on.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_collect_job_signal_ops(struct panthor_submit_ctx *ctx,
					  u32 job_idx)
{
	const struct drm_panthor_sync_op *sync_ops = ctx->jobs[job_idx].syncops;
	u32 sync_op_count = ctx->jobs[job_idx].syncop_count;

	for (u32 i = 0; i < sync_op_count; i++) {
		int ret;

		if (!sync_op_is_signal(&sync_ops[i]))
			continue;

		ret = panthor_check_sync_op(&sync_ops[i]);
		if (ret)
			return ret;

		ret = panthor_submit_ctx_get_sync_signal(ctx,
							 sync_ops[i].handle,
							 sync_ops[i].timeline_value);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * panthor_submit_ctx_push_fences() - Iterate over the signal array, and for each entry, push
 * the currently assigned fence to the associated syncobj.
 * @ctx: Context to push fences on.
 *
 * This is the last step of a submission procedure, and is done once we know the submission
 * is effective and job fences are guaranteed to be signaled in finite time.
 */
static void
panthor_submit_ctx_push_fences(struct panthor_submit_ctx *ctx)
{
	struct panthor_sync_signal *sig_sync;

	list_for_each_entry(sig_sync, &ctx->signals, node) {
		if (sig_sync->chain) {
			drm_syncobj_add_point(sig_sync->syncobj, sig_sync->chain,
					      sig_sync->fence, sig_sync->point);
			sig_sync->chain = NULL;
		} else {
			drm_syncobj_replace_fence(sig_sync->syncobj, sig_sync->fence);
		}
	}
}

/**
 * panthor_submit_ctx_add_sync_deps_to_job() - Add sync wait operations as
 * job dependencies.
 * @ctx: Submit context.
 * @job_idx: Index of the job to operate on.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_add_sync_deps_to_job(struct panthor_submit_ctx *ctx,
					u32 job_idx)
{
	struct panthor_device *ptdev = container_of(ctx->file->minor->dev,
						    struct panthor_device,
						    base);
	const struct drm_panthor_sync_op *sync_ops = ctx->jobs[job_idx].syncops;
	struct drm_sched_job *job = ctx->jobs[job_idx].job;
	u32 sync_op_count = ctx->jobs[job_idx].syncop_count;
	int ret = 0;

	for (u32 i = 0; i < sync_op_count; i++) {
		struct panthor_sync_signal *sig_sync;
		struct dma_fence *fence;

		if (!sync_op_is_wait(&sync_ops[i]))
			continue;

		ret = panthor_check_sync_op(&sync_ops[i]);
		if (ret)
			return ret;

		sig_sync = panthor_submit_ctx_search_sync_signal(ctx, sync_ops[i].handle,
								 sync_ops[i].timeline_value);
		if (sig_sync) {
			if (drm_WARN_ON(&ptdev->base, !sig_sync->fence))
				return -EINVAL;

			fence = dma_fence_get(sig_sync->fence);
		} else {
			ret = drm_syncobj_find_fence(ctx->file, sync_ops[i].handle,
						     sync_ops[i].timeline_value,
						     0, &fence);
			if (ret)
				return ret;
		}

		ret = drm_sched_job_add_dependency(job, fence);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * panthor_submit_ctx_collect_jobs_signal_ops() - Collect all signal operations
 * and add them to the submit context.
 * @ctx: Submit context.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_collect_jobs_signal_ops(struct panthor_submit_ctx *ctx)
{
	for (u32 i = 0; i < ctx->job_count; i++) {
		int ret;

		ret = panthor_submit_ctx_collect_job_signal_ops(ctx, i);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * panthor_submit_ctx_add_deps_and_arm_jobs() - Add jobs dependencies and arm jobs
 * @ctx: Submit context.
 *
 * Must be called after the resv preparation has been taken care of.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
panthor_submit_ctx_add_deps_and_arm_jobs(struct panthor_submit_ctx *ctx)
{
	for (u32 i = 0; i < ctx->job_count; i++) {
		int ret;

		ret = panthor_submit_ctx_add_sync_deps_to_job(ctx, i);
		if (ret)
			return ret;

		drm_sched_job_arm(ctx->jobs[i].job);

		ret = panthor_submit_ctx_update_job_sync_signal_fences(ctx, i);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * panthor_submit_ctx_push_jobs() - Push jobs to their scheduling entities.
 * @ctx: Submit context.
 * @upd_resvs: Callback used to update reservation objects that were previously
 * preapred.
 */
static void
panthor_submit_ctx_push_jobs(struct panthor_submit_ctx *ctx,
			     void (*upd_resvs)(struct drm_exec *, struct drm_sched_job *))
{
	for (u32 i = 0; i < ctx->job_count; i++) {
		upd_resvs(&ctx->exec, ctx->jobs[i].job);
		drm_sched_entity_push_job(ctx->jobs[i].job);

		/* Job is owned by the scheduler now. */
		ctx->jobs[i].job = NULL;
	}

	panthor_submit_ctx_push_fences(ctx);
}

/**
 * panthor_submit_ctx_init() - Initializes a submission context
 * @ctx: Submit context to initialize.
 * @file: drm_file this submission happens on.
 * @job_count: Number of jobs that will be submitted.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int panthor_submit_ctx_init(struct panthor_submit_ctx *ctx,
				   struct drm_file *file, u32 job_count)
{
	ctx->jobs = kvmalloc_array(job_count, sizeof(*ctx->jobs),
				   GFP_KERNEL | __GFP_ZERO);
	if (!ctx->jobs)
		return -ENOMEM;

	ctx->file = file;
	ctx->job_count = job_count;
	INIT_LIST_HEAD(&ctx->signals);
	drm_exec_init(&ctx->exec,
		      DRM_EXEC_INTERRUPTIBLE_WAIT | DRM_EXEC_IGNORE_DUPLICATES,
		      0);
	return 0;
}

/**
 * panthor_submit_ctx_cleanup() - Cleanup a submission context
 * @ctx: Submit context to cleanup.
 * @job_put: Job put callback.
 */
static void panthor_submit_ctx_cleanup(struct panthor_submit_ctx *ctx,
				       void (*job_put)(struct drm_sched_job *))
{
	struct panthor_sync_signal *sig_sync, *tmp;
	unsigned long i;

	drm_exec_fini(&ctx->exec);

	list_for_each_entry_safe(sig_sync, tmp, &ctx->signals, node)
		panthor_sync_signal_free(sig_sync);

	for (i = 0; i < ctx->job_count; i++) {
		job_put(ctx->jobs[i].job);
		kvfree(ctx->jobs[i].syncops);
	}

	kvfree(ctx->jobs);
}

static int panthor_ioctl_dev_query(struct drm_device *ddev, void *data, struct drm_file *file)
{
	struct panthor_device *ptdev = container_of(ddev, struct panthor_device, base);
	struct drm_panthor_dev_query *args = data;

	if (!args->pointer) {
		switch (args->type) {
		case DRM_PANTHOR_DEV_QUERY_GPU_INFO:
			args->size = sizeof(ptdev->gpu_info);
			return 0;

		case DRM_PANTHOR_DEV_QUERY_CSIF_INFO:
			args->size = sizeof(ptdev->csif_info);
			return 0;

		default:
			return -EINVAL;
		}
	}

	switch (args->type) {
	case DRM_PANTHOR_DEV_QUERY_GPU_INFO:
		return PANTHOR_UOBJ_SET(args->pointer, args->size, ptdev->gpu_info);

	case DRM_PANTHOR_DEV_QUERY_CSIF_INFO:
		return PANTHOR_UOBJ_SET(args->pointer, args->size, ptdev->csif_info);

	default:
		return -EINVAL;
	}
}

#define PANTHOR_VM_CREATE_FLAGS			0

static int panthor_ioctl_vm_create(struct drm_device *ddev, void *data,
				   struct drm_file *file)
{
	struct panthor_device *ptdev = container_of(ddev, struct panthor_device, base);
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_vm_create *args = data;
	int cookie, ret;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	ret = panthor_vm_pool_create_vm(ptdev, pfile->vms,  args);
	if (ret >= 0) {
		args->id = ret;
		ret = 0;
	}

	drm_dev_exit(cookie);
	return ret;
}

static int panthor_ioctl_vm_destroy(struct drm_device *ddev, void *data,
				    struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_vm_destroy *args = data;

	if (args->pad)
		return -EINVAL;

	return panthor_vm_pool_destroy_vm(pfile->vms, args->id);
}

#define PANTHOR_BO_FLAGS		DRM_PANTHOR_BO_NO_MMAP

static int panthor_ioctl_bo_create(struct drm_device *ddev, void *data,
				   struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_bo_create *args = data;
	struct panthor_vm *vm = NULL;
	int cookie, ret;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	if (!args->size || args->pad ||
	    (args->flags & ~PANTHOR_BO_FLAGS)) {
		ret = -EINVAL;
		goto out_dev_exit;
	}

	if (args->exclusive_vm_id) {
		vm = panthor_vm_pool_get_vm(pfile->vms, args->exclusive_vm_id);
		if (!vm) {
			ret = -EINVAL;
			goto out_dev_exit;
		}
	}

	ret = panthor_gem_create_with_handle(file, ddev, vm, &args->size,
					     args->flags, &args->handle);

	panthor_vm_put(vm);

out_dev_exit:
	drm_dev_exit(cookie);
	return ret;
}

static int panthor_ioctl_bo_mmap_offset(struct drm_device *ddev, void *data,
					struct drm_file *file)
{
	struct drm_panthor_bo_mmap_offset *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (args->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto out;

	args->offset = drm_vma_node_offset_addr(&obj->vma_node);

out:
	drm_gem_object_put(obj);
	return ret;
}

static int panthor_ioctl_group_submit(struct drm_device *ddev, void *data,
				      struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_group_submit *args = data;
	struct drm_panthor_queue_submit *jobs_args;
	struct panthor_submit_ctx ctx;
	int ret = 0, cookie;

	if (args->pad)
		return -EINVAL;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	ret = PANTHOR_UOBJ_GET_ARRAY(jobs_args, &args->queue_submits);
	if (ret)
		goto out_dev_exit;

	ret = panthor_submit_ctx_init(&ctx, file, args->queue_submits.count);
	if (ret)
		goto out_free_jobs_args;

	/* Create jobs and attach sync operations */
	for (u32 i = 0; i < args->queue_submits.count; i++) {
		const struct drm_panthor_queue_submit *qsubmit = &jobs_args[i];
		struct drm_sched_job *job;

		job = panthor_job_create(pfile, args->group_handle, qsubmit);
		if (IS_ERR(job)) {
			ret = PTR_ERR(job);
			goto out_cleanup_submit_ctx;
		}

		ret = panthor_submit_ctx_add_job(&ctx, i, job, &qsubmit->syncs);
		if (ret)
			goto out_cleanup_submit_ctx;
	}

	/*
	 * Collect signal operations on all jobs, such that each job can pick
	 * from it for its dependencies and update the fence to signal when the
	 * job is submitted.
	 */
	ret = panthor_submit_ctx_collect_jobs_signal_ops(&ctx);
	if (ret)
		goto out_cleanup_submit_ctx;

	/*
	 * We acquire/prepare revs on all jobs before proceeding with the
	 * dependency registration.
	 *
	 * This is solving two problems:
	 * 1. drm_sched_job_arm() and drm_sched_entity_push_job() must be
	 *    protected by a lock to make sure no concurrent access to the same
	 *    entity get interleaved, which would mess up with the fence seqno
	 *    ordering. Luckily, one of the resv being acquired is the VM resv,
	 *    and a scheduling entity is only bound to a single VM. As soon as
	 *    we acquire the VM resv, we should be safe.
	 * 2. Jobs might depend on fences that were issued by previous jobs in
	 *    the same batch, so we can't add dependencies on all jobs before
	 *    arming previous jobs and registering the fence to the signal
	 *    array, otherwise we might miss dependencies, or point to an
	 *    outdated fence.
	 */
	if (args->queue_submits.count > 0) {
		/* All jobs target the same group, so they also point to the same VM. */
		struct panthor_vm *vm = panthor_job_vm(ctx.jobs[0].job);

		drm_exec_until_all_locked(&ctx.exec) {
			ret = panthor_vm_prepare_mapped_bos_resvs(&ctx.exec, vm,
								  args->queue_submits.count);
		}

		if (ret)
			goto out_cleanup_submit_ctx;
	}

	/*
	 * Now that resvs are locked/prepared, we can iterate over each job to
	 * add the dependencies, arm the job fence, register the job fence to
	 * the signal array.
	 */
	ret = panthor_submit_ctx_add_deps_and_arm_jobs(&ctx);
	if (ret)
		goto out_cleanup_submit_ctx;

	/* Nothing can fail after that point, so we can make our job fences
	 * visible to the outside world. Push jobs and set the job fences to
	 * the resv slots we reserved.  This also pushes the fences to the
	 * syncobjs that are part of the signal array.
	 */
	panthor_submit_ctx_push_jobs(&ctx, panthor_job_update_resvs);

out_cleanup_submit_ctx:
	panthor_submit_ctx_cleanup(&ctx, panthor_job_put);

out_free_jobs_args:
	kvfree(jobs_args);

out_dev_exit:
	drm_dev_exit(cookie);
	return ret;
}

static int panthor_ioctl_group_destroy(struct drm_device *ddev, void *data,
				       struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_group_destroy *args = data;

	if (args->pad)
		return -EINVAL;

	return panthor_group_destroy(pfile, args->group_handle);
}

static int panthor_ioctl_group_create(struct drm_device *ddev, void *data,
				      struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_group_create *args = data;
	struct drm_panthor_queue_create *queue_args;
	int ret;

	if (!args->queues.count)
		return -EINVAL;

	ret = PANTHOR_UOBJ_GET_ARRAY(queue_args, &args->queues);
	if (ret)
		return ret;

	ret = panthor_group_create(pfile, args, queue_args);
	if (ret >= 0) {
		args->group_handle = ret;
		ret = 0;
	}

	kvfree(queue_args);
	return ret;
}

static int panthor_ioctl_group_get_state(struct drm_device *ddev, void *data,
					 struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_group_get_state *args = data;

	return panthor_group_get_state(pfile, args);
}

static int panthor_ioctl_tiler_heap_create(struct drm_device *ddev, void *data,
					   struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_tiler_heap_create *args = data;
	struct panthor_heap_pool *pool;
	struct panthor_vm *vm;
	int ret;

	vm = panthor_vm_pool_get_vm(pfile->vms, args->vm_id);
	if (!vm)
		return -EINVAL;

	pool = panthor_vm_get_heap_pool(vm, true);
	if (IS_ERR(pool)) {
		ret = PTR_ERR(pool);
		goto out_put_vm;
	}

	ret = panthor_heap_create(pool,
				  args->initial_chunk_count,
				  args->chunk_size,
				  args->max_chunks,
				  args->target_in_flight,
				  &args->tiler_heap_ctx_gpu_va,
				  &args->first_heap_chunk_gpu_va);
	if (ret < 0)
		goto out_put_heap_pool;

	/* Heap pools are per-VM. We combine the VM and HEAP id to make
	 * a unique heap handle.
	 */
	args->handle = (args->vm_id << 16) | ret;
	ret = 0;

out_put_heap_pool:
	panthor_heap_pool_put(pool);

out_put_vm:
	panthor_vm_put(vm);
	return ret;
}

static int panthor_ioctl_tiler_heap_destroy(struct drm_device *ddev, void *data,
					    struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_tiler_heap_destroy *args = data;
	struct panthor_heap_pool *pool;
	struct panthor_vm *vm;
	int ret;

	if (args->pad)
		return -EINVAL;

	vm = panthor_vm_pool_get_vm(pfile->vms, args->handle >> 16);
	if (!vm)
		return -EINVAL;

	pool = panthor_vm_get_heap_pool(vm, false);
	if (IS_ERR(pool)) {
		ret = PTR_ERR(pool);
		goto out_put_vm;
	}

	ret = panthor_heap_destroy(pool, args->handle & GENMASK(15, 0));
	panthor_heap_pool_put(pool);

out_put_vm:
	panthor_vm_put(vm);
	return ret;
}

static int panthor_ioctl_vm_bind_async(struct drm_device *ddev,
				       struct drm_panthor_vm_bind *args,
				       struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_vm_bind_op *jobs_args;
	struct panthor_submit_ctx ctx;
	struct panthor_vm *vm;
	int ret = 0;

	vm = panthor_vm_pool_get_vm(pfile->vms, args->vm_id);
	if (!vm)
		return -EINVAL;

	ret = PANTHOR_UOBJ_GET_ARRAY(jobs_args, &args->ops);
	if (ret)
		goto out_put_vm;

	ret = panthor_submit_ctx_init(&ctx, file, args->ops.count);
	if (ret)
		goto out_free_jobs_args;

	for (u32 i = 0; i < args->ops.count; i++) {
		struct drm_panthor_vm_bind_op *op = &jobs_args[i];
		struct drm_sched_job *job;

		job = panthor_vm_bind_job_create(file, vm, op);
		if (IS_ERR(job)) {
			ret = PTR_ERR(job);
			goto out_cleanup_submit_ctx;
		}

		ret = panthor_submit_ctx_add_job(&ctx, i, job, &op->syncs);
		if (ret)
			goto out_cleanup_submit_ctx;
	}

	ret = panthor_submit_ctx_collect_jobs_signal_ops(&ctx);
	if (ret)
		goto out_cleanup_submit_ctx;

	/* Prepare reservation objects for each VM_BIND job. */
	drm_exec_until_all_locked(&ctx.exec) {
		for (u32 i = 0; i < ctx.job_count; i++) {
			ret = panthor_vm_bind_job_prepare_resvs(&ctx.exec, ctx.jobs[i].job);
			drm_exec_retry_on_contention(&ctx.exec);
			if (ret)
				goto out_cleanup_submit_ctx;
		}
	}

	ret = panthor_submit_ctx_add_deps_and_arm_jobs(&ctx);
	if (ret)
		goto out_cleanup_submit_ctx;

	/* Nothing can fail after that point. */
	panthor_submit_ctx_push_jobs(&ctx, panthor_vm_bind_job_update_resvs);

out_cleanup_submit_ctx:
	panthor_submit_ctx_cleanup(&ctx, panthor_vm_bind_job_put);

out_free_jobs_args:
	kvfree(jobs_args);

out_put_vm:
	panthor_vm_put(vm);
	return ret;
}

static int panthor_ioctl_vm_bind_sync(struct drm_device *ddev,
				      struct drm_panthor_vm_bind *args,
				      struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_vm_bind_op *jobs_args;
	struct panthor_vm *vm;
	int ret;

	vm = panthor_vm_pool_get_vm(pfile->vms, args->vm_id);
	if (!vm)
		return -EINVAL;

	ret = PANTHOR_UOBJ_GET_ARRAY(jobs_args, &args->ops);
	if (ret)
		goto out_put_vm;

	for (u32 i = 0; i < args->ops.count; i++) {
		ret = panthor_vm_bind_exec_sync_op(file, vm, &jobs_args[i]);
		if (ret) {
			/* Update ops.count so the user knows where things failed. */
			args->ops.count = i;
			break;
		}
	}

	kvfree(jobs_args);

out_put_vm:
	panthor_vm_put(vm);
	return ret;
}

#define PANTHOR_VM_BIND_FLAGS DRM_PANTHOR_VM_BIND_ASYNC

static int panthor_ioctl_vm_bind(struct drm_device *ddev, void *data,
				 struct drm_file *file)
{
	struct drm_panthor_vm_bind *args = data;
	int cookie, ret;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	if (args->flags & DRM_PANTHOR_VM_BIND_ASYNC)
		ret = panthor_ioctl_vm_bind_async(ddev, args, file);
	else
		ret = panthor_ioctl_vm_bind_sync(ddev, args, file);

	drm_dev_exit(cookie);
	return ret;
}

static int panthor_ioctl_vm_get_state(struct drm_device *ddev, void *data,
				      struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;
	struct drm_panthor_vm_get_state *args = data;
	struct panthor_vm *vm;

	vm = panthor_vm_pool_get_vm(pfile->vms, args->vm_id);
	if (!vm)
		return -EINVAL;

	if (panthor_vm_is_unusable(vm))
		args->state = DRM_PANTHOR_VM_STATE_UNUSABLE;
	else
		args->state = DRM_PANTHOR_VM_STATE_USABLE;

	panthor_vm_put(vm);
	return 0;
}

static int
panthor_open(struct drm_device *ddev, struct drm_file *file)
{
	struct panthor_device *ptdev = container_of(ddev, struct panthor_device, base);
	struct panthor_file *pfile;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	pfile = kzalloc(sizeof(*pfile), GFP_KERNEL);
	if (!pfile) {
		ret = -ENOMEM;
		goto err_put_mod;
	}

	pfile->ptdev = ptdev;

	ret = panthor_vm_pool_create(pfile);
	if (ret)
		goto err_free_file;

	ret = panthor_group_pool_create(pfile);
	if (ret)
		goto err_destroy_vm_pool;

	file->driver_priv = pfile;
	return 0;

err_destroy_vm_pool:
	panthor_vm_pool_destroy(pfile);

err_free_file:
	kfree(pfile);

err_put_mod:
	module_put(THIS_MODULE);
	return ret;
}

static void
panthor_postclose(struct drm_device *ddev, struct drm_file *file)
{
	struct panthor_file *pfile = file->driver_priv;

	panthor_group_pool_destroy(pfile);
	panthor_vm_pool_destroy(pfile);

	kfree(pfile);
	module_put(THIS_MODULE);
}

static const struct drm_ioctl_desc panthor_drm_driver_ioctls[] = {
#define PANTHOR_IOCTL(n, func, flags) \
	DRM_IOCTL_DEF_DRV(PANTHOR_##n, panthor_ioctl_##func, flags)

	PANTHOR_IOCTL(DEV_QUERY, dev_query, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(VM_CREATE, vm_create, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(VM_DESTROY, vm_destroy, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(VM_BIND, vm_bind, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(VM_GET_STATE, vm_get_state, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(BO_CREATE, bo_create, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(BO_MMAP_OFFSET, bo_mmap_offset, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(GROUP_CREATE, group_create, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(GROUP_DESTROY, group_destroy, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(GROUP_GET_STATE, group_get_state, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(TILER_HEAP_CREATE, tiler_heap_create, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(TILER_HEAP_DESTROY, tiler_heap_destroy, DRM_RENDER_ALLOW),
	PANTHOR_IOCTL(GROUP_SUBMIT, group_submit, DRM_RENDER_ALLOW),
};

static int panthor_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file = filp->private_data;
	struct panthor_file *pfile = file->driver_priv;
	struct panthor_device *ptdev = pfile->ptdev;
	u64 offset = (u64)vma->vm_pgoff << PAGE_SHIFT;
	int ret, cookie;

	if (!drm_dev_enter(file->minor->dev, &cookie))
		return -ENODEV;

#ifdef CONFIG_ARM64
	/*
	 * With 32-bit systems being limited by the 32-bit representation of
	 * mmap2's pgoffset field, we need to make the MMIO offset arch
	 * specific. This converts a user MMIO offset into something the kernel
	 * driver understands.
	 */
	if (test_tsk_thread_flag(current, TIF_32BIT) &&
	    offset >= DRM_PANTHOR_USER_MMIO_OFFSET_32BIT) {
		offset += DRM_PANTHOR_USER_MMIO_OFFSET_64BIT -
			  DRM_PANTHOR_USER_MMIO_OFFSET_32BIT;
		vma->vm_pgoff = offset >> PAGE_SHIFT;
	}
#endif

	if (offset >= DRM_PANTHOR_USER_MMIO_OFFSET)
		ret = panthor_device_mmap_io(ptdev, vma);
	else
		ret = drm_gem_mmap(filp, vma);

	drm_dev_exit(cookie);
	return ret;
}

static const struct file_operations panthor_drm_driver_fops = {
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
	.mmap = panthor_mmap,
};

#ifdef CONFIG_DEBUG_FS
static void panthor_debugfs_init(struct drm_minor *minor)
{
	panthor_mmu_debugfs_init(minor);
}
#endif

/*
 * PanCSF driver version:
 * - 1.0 - initial interface
 */
static const struct drm_driver panthor_drm_driver = {
	.driver_features = DRIVER_RENDER | DRIVER_GEM | DRIVER_SYNCOBJ |
			   DRIVER_SYNCOBJ_TIMELINE | DRIVER_GEM_GPUVA,
	.open = panthor_open,
	.postclose = panthor_postclose,
	.ioctls = panthor_drm_driver_ioctls,
	.num_ioctls = ARRAY_SIZE(panthor_drm_driver_ioctls),
	.fops = &panthor_drm_driver_fops,
	.name = "panthor",
	.desc = "Panthor DRM driver",
	.date = "20230801",
	.major = 1,
	.minor = 0,

	.gem_create_object = panthor_gem_create_object,
	.gem_prime_import_sg_table = drm_gem_shmem_prime_import_sg_table,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = panthor_debugfs_init,
#endif
};

static int panthor_probe(struct platform_device *pdev)
{
	struct panthor_device *ptdev;

	ptdev = devm_drm_dev_alloc(&pdev->dev, &panthor_drm_driver,
				   struct panthor_device, base);
	if (IS_ERR(ptdev))
		return -ENOMEM;

	platform_set_drvdata(pdev, ptdev);

	return panthor_device_init(ptdev);
}

static void panthor_remove(struct platform_device *pdev)
{
	struct panthor_device *ptdev = platform_get_drvdata(pdev);

	panthor_device_unplug(ptdev);
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "rockchip,rk3588-mali" },
	{ .compatible = "arm,mali-valhall-csf" },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static DEFINE_RUNTIME_DEV_PM_OPS(panthor_pm_ops,
				 panthor_device_suspend,
				 panthor_device_resume,
				 NULL);

static struct platform_driver panthor_driver = {
	.probe = panthor_probe,
	.remove_new = panthor_remove,
	.driver = {
		.name = "panthor",
		.pm = pm_ptr(&panthor_pm_ops),
		.of_match_table = dt_match,
	},
};

/*
 * Workqueue used to cleanup stuff.
 *
 * We create a dedicated workqueue so we can drain on unplug and
 * make sure all resources are freed before the module is unloaded.
 */
struct workqueue_struct *panthor_cleanup_wq;

static int __init panthor_init(void)
{
	int ret;

	ret = panthor_mmu_pt_cache_init();
	if (ret)
		return ret;

	panthor_cleanup_wq = alloc_workqueue("panthor-cleanup", WQ_UNBOUND, 0);
	if (!panthor_cleanup_wq) {
		pr_err("panthor: Failed to allocate the workqueues");
		ret = -ENOMEM;
		goto err_mmu_pt_cache_fini;
	}

	ret = platform_driver_register(&panthor_driver);
	if (ret)
		goto err_destroy_cleanup_wq;

	return 0;

err_destroy_cleanup_wq:
	destroy_workqueue(panthor_cleanup_wq);

err_mmu_pt_cache_fini:
	panthor_mmu_pt_cache_fini();
	return ret;
}
module_init(panthor_init);

static void __exit panthor_exit(void)
{
	platform_driver_unregister(&panthor_driver);
	destroy_workqueue(panthor_cleanup_wq);
	panthor_mmu_pt_cache_fini();
}
module_exit(panthor_exit);

MODULE_AUTHOR("Panthor Project Developers");
MODULE_DESCRIPTION("Panthor DRM Driver");
MODULE_LICENSE("Dual MIT/GPL");
