// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_exec.h"

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_macros.h"
#include "xe_sched_job.h"
#include "xe_sync.h"
#include "xe_vm.h"

/**
 * DOC: Execbuf (User GPU command submission)
 *
 * Execs have historically been rather complicated in DRM drivers (at least in
 * the i915) because a few things:
 *
 * - Passing in a list BO which are read / written to creating implicit syncs
 * - Binding at exec time
 * - Flow controlling the ring at exec time
 *
 * In XE we avoid all of this complication by not allowing a BO list to be
 * passed into an exec, using the dma-buf implicit sync uAPI, have binds as
 * seperate operations, and using the DRM scheduler to flow control the ring.
 * Let's deep dive on each of these.
 *
 * We can get away from a BO list by forcing the user to use in / out fences on
 * every exec rather than the kernel tracking dependencies of BO (e.g. if the
 * user knows an exec writes to a BO and reads from the BO in the next exec, it
 * is the user's responsibility to pass in / out fence between the two execs).
 *
 * Implicit dependencies for external BOs are handled by using the dma-buf
 * implicit dependency uAPI (TODO: add link). To make this works each exec must
 * install the job's fence into the DMA_RESV_USAGE_WRITE slot of every external
 * BO mapped in the VM.
 *
 * We do not allow a user to trigger a bind at exec time rather we have a VM
 * bind IOCTL which uses the same in / out fence interface as exec. In that
 * sense, a VM bind is basically the same operation as an exec from the user
 * perspective. e.g. If an exec depends on a VM bind use the in / out fence
 * interface (struct drm_xe_sync) to synchronize like syncing between two
 * dependent execs.
 *
 * Although a user cannot trigger a bind, we still have to rebind userptrs in
 * the VM that have been invalidated since the last exec, likewise we also have
 * to rebind BOs that have been evicted by the kernel. We schedule these rebinds
 * behind any pending kernel operations on any external BOs in VM or any BOs
 * private to the VM. This is accomplished by the rebinds waiting on BOs
 * DMA_RESV_USAGE_KERNEL slot (kernel ops) and kernel ops waiting on all BOs
 * slots (inflight execs are in the DMA_RESV_USAGE_BOOKING for private BOs and
 * in DMA_RESV_USAGE_WRITE for external BOs).
 *
 * Rebinds / dma-resv usage applies to non-compute mode VMs only as for compute
 * mode VMs we use preempt fences and a rebind worker (TODO: add link).
 *
 * There is no need to flow control the ring in the exec as we write the ring at
 * submission time and set the DRM scheduler max job limit SIZE_OF_RING /
 * MAX_JOB_SIZE. The DRM scheduler will then hold all jobs until space in the
 * ring is available.
 *
 * All of this results in a rather simple exec implementation.
 *
 * Flow
 * ~~~~
 *
 * .. code-block::
 *
 *	Parse input arguments
 *	Wait for any async VM bind passed as in-fences to start
 *	<----------------------------------------------------------------------|
 *	Lock global VM lock in read mode                                       |
 *	Pin userptrs (also finds userptr invalidated since last exec)          |
 *	Lock exec (VM dma-resv lock, external BOs dma-resv locks)              |
 *	Validate BOs that have been evicted                                    |
 *	Create job                                                             |
 *	Rebind invalidated userptrs + evicted BOs (non-compute-mode)           |
 *	Add rebind fence dependency to job                                     |
 *	Add job VM dma-resv bookkeeping slot (non-compute mode)                |
 *	Add job to external BOs dma-resv write slots (non-compute mode)        |
 *	Check if any userptrs invalidated since pin ------ Drop locks ---------|
 *	Install in / out fences for job
 *	Submit job
 *	Unlock all
 */

static int xe_exec_begin(struct xe_engine *e, struct ww_acquire_ctx *ww,
			 struct ttm_validate_buffer tv_onstack[],
			 struct ttm_validate_buffer **tv,
			 struct list_head *objs)
{
	struct xe_vm *vm = e->vm;
	struct xe_vma *vma;
	LIST_HEAD(dups);
	int err;

	*tv = NULL;
	if (xe_vm_no_dma_fences(e->vm))
		return 0;

	err = xe_vm_lock_dma_resv(vm, ww, tv_onstack, tv, objs, true, 1);
	if (err)
		return err;

	/*
	 * Validate BOs that have been evicted (i.e. make sure the
	 * BOs have valid placements possibly moving an evicted BO back
	 * to a location where the GPU can access it).
	 */
	list_for_each_entry(vma, &vm->rebind_list, rebind_link) {
		if (xe_vma_is_userptr(vma))
			continue;

		err = xe_bo_validate(vma->bo, vm, false);
		if (err) {
			xe_vm_unlock_dma_resv(vm, tv_onstack, *tv, ww, objs);
			*tv = NULL;
			return err;
		}
	}

	return 0;
}

static void xe_exec_end(struct xe_engine *e,
			struct ttm_validate_buffer *tv_onstack,
			struct ttm_validate_buffer *tv,
			struct ww_acquire_ctx *ww,
			struct list_head *objs)
{
	if (!xe_vm_no_dma_fences(e->vm))
		xe_vm_unlock_dma_resv(e->vm, tv_onstack, tv, ww, objs);
}

int xe_exec_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_exec *args = data;
	struct drm_xe_sync __user *syncs_user = u64_to_user_ptr(args->syncs);
	u64 __user *addresses_user = u64_to_user_ptr(args->address);
	struct xe_engine *engine;
	struct xe_sync_entry *syncs = NULL;
	u64 addresses[XE_HW_ENGINE_MAX_INSTANCE];
	struct ttm_validate_buffer tv_onstack[XE_ONSTACK_TV];
	struct ttm_validate_buffer *tv = NULL;
	u32 i, num_syncs = 0;
	struct xe_sched_job *job;
	struct dma_fence *rebind_fence;
	struct xe_vm *vm;
	struct ww_acquire_ctx ww;
	struct list_head objs;
	bool write_locked;
	int err = 0;

	if (XE_IOCTL_ERR(xe, args->extensions))
		return -EINVAL;

	engine = xe_engine_lookup(xef, args->engine_id);
	if (XE_IOCTL_ERR(xe, !engine))
		return -ENOENT;

	if (XE_IOCTL_ERR(xe, engine->flags & ENGINE_FLAG_VM))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, engine->width != args->num_batch_buffer))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, engine->flags & ENGINE_FLAG_BANNED)) {
		err = -ECANCELED;
		goto err_engine;
	}

	if (args->num_syncs) {
		syncs = kcalloc(args->num_syncs, sizeof(*syncs), GFP_KERNEL);
		if (!syncs) {
			err = -ENOMEM;
			goto err_engine;
		}
	}

	vm = engine->vm;

	for (i = 0; i < args->num_syncs; i++) {
		err = xe_sync_entry_parse(xe, xef, &syncs[num_syncs++],
					  &syncs_user[i], true,
					  xe_vm_no_dma_fences(vm));
		if (err)
			goto err_syncs;
	}

	if (xe_engine_is_parallel(engine)) {
		err = __copy_from_user(addresses, addresses_user, sizeof(u64) *
				       engine->width);
		if (err) {
			err = -EFAULT;
			goto err_syncs;
		}
	}

	/*
	 * We can't install a job into the VM dma-resv shared slot before an
	 * async VM bind passed in as a fence without the risk of deadlocking as
	 * the bind can trigger an eviction which in turn depends on anything in
	 * the VM dma-resv shared slots. Not an ideal solution, but we wait for
	 * all dependent async VM binds to start (install correct fences into
	 * dma-resv slots) before moving forward.
	 */
	if (!xe_vm_no_dma_fences(vm) &&
	    vm->flags & XE_VM_FLAG_ASYNC_BIND_OPS) {
		for (i = 0; i < args->num_syncs; i++) {
			struct dma_fence *fence = syncs[i].fence;
			if (fence) {
				err = xe_vm_async_fence_wait_start(fence);
				if (err)
					goto err_syncs;
			}
		}
	}

retry:
	if (!xe_vm_no_dma_fences(vm) && xe_vm_userptr_check_repin(vm)) {
		err = down_write_killable(&vm->lock);
		write_locked = true;
	} else {
		/* We don't allow execs while the VM is in error state */
		err = down_read_interruptible(&vm->lock);
		write_locked = false;
	}
	if (err)
		goto err_syncs;

	/* We don't allow execs while the VM is in error state */
	if (vm->async_ops.error) {
		err = vm->async_ops.error;
		goto err_unlock_list;
	}

	/*
	 * Extreme corner where we exit a VM error state with a munmap style VM
	 * unbind inflight which requires a rebind. In this case the rebind
	 * needs to install some fences into the dma-resv slots. The worker to
	 * do this queued, let that worker make progress by dropping vm->lock,
	 * flushing the worker and retrying the exec.
	 */
	if (vm->async_ops.munmap_rebind_inflight) {
		if (write_locked)
			up_write(&vm->lock);
		else
			up_read(&vm->lock);
		flush_work(&vm->async_ops.work);
		goto retry;
	}

	if (write_locked) {
		err = xe_vm_userptr_pin(vm);
		downgrade_write(&vm->lock);
		write_locked = false;
		if (err)
			goto err_unlock_list;
	}

	err = xe_exec_begin(engine, &ww, tv_onstack, &tv, &objs);
	if (err)
		goto err_unlock_list;

	if (xe_vm_is_closed(engine->vm)) {
		drm_warn(&xe->drm, "Trying to schedule after vm is closed\n");
		err = -EIO;
		goto err_engine_end;
	}

	job = xe_sched_job_create(engine, xe_engine_is_parallel(engine) ?
				  addresses : &args->address);
	if (IS_ERR(job)) {
		err = PTR_ERR(job);
		goto err_engine_end;
	}

	/*
	 * Rebind any invalidated userptr or evicted BOs in the VM, non-compute
	 * VM mode only.
	 */
	rebind_fence = xe_vm_rebind(vm, false);
	if (IS_ERR(rebind_fence)) {
		err = PTR_ERR(rebind_fence);
		goto err_put_job;
	}

	/*
	 * We store the rebind_fence in the VM so subsequent execs don't get
	 * scheduled before the rebinds of userptrs / evicted BOs is complete.
	 */
	if (rebind_fence) {
		dma_fence_put(vm->rebind_fence);
		vm->rebind_fence = rebind_fence;
	}
	if (vm->rebind_fence) {
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			     &vm->rebind_fence->flags)) {
			dma_fence_put(vm->rebind_fence);
			vm->rebind_fence = NULL;
		} else {
			dma_fence_get(vm->rebind_fence);
			err = drm_sched_job_add_dependency(&job->drm,
							   vm->rebind_fence);
			if (err)
				goto err_put_job;
		}
	}

	/* Wait behind munmap style rebinds */
	if (!xe_vm_no_dma_fences(vm)) {
		err = drm_sched_job_add_resv_dependencies(&job->drm,
							  &vm->resv,
							  DMA_RESV_USAGE_KERNEL);
		if (err)
			goto err_put_job;
	}

	for (i = 0; i < num_syncs && !err; i++)
		err = xe_sync_entry_add_deps(&syncs[i], job);
	if (err)
		goto err_put_job;

	if (!xe_vm_no_dma_fences(vm)) {
		err = down_read_interruptible(&vm->userptr.notifier_lock);
		if (err)
			goto err_put_job;

		err = __xe_vm_userptr_needs_repin(vm);
		if (err)
			goto err_repin;
	}

	/*
	 * Point of no return, if we error after this point just set an error on
	 * the job and let the DRM scheduler / backend clean up the job.
	 */
	xe_sched_job_arm(job);
	if (!xe_vm_no_dma_fences(vm)) {
		/* Block userptr invalidations / BO eviction */
		dma_resv_add_fence(&vm->resv,
				   &job->drm.s_fence->finished,
				   DMA_RESV_USAGE_BOOKKEEP);

		/*
		 * Make implicit sync work across drivers, assuming all external
		 * BOs are written as we don't pass in a read / write list.
		 */
		xe_vm_fence_all_extobjs(vm, &job->drm.s_fence->finished,
					DMA_RESV_USAGE_WRITE);
	}

	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], job,
				     &job->drm.s_fence->finished);

	xe_sched_job_push(job);
	xe_vm_reactivate_rebind(vm);

err_repin:
	if (!xe_vm_no_dma_fences(vm))
		up_read(&vm->userptr.notifier_lock);
err_put_job:
	if (err)
		xe_sched_job_put(job);
err_engine_end:
	xe_exec_end(engine, tv_onstack, tv, &ww, &objs);
err_unlock_list:
	if (write_locked)
		up_write(&vm->lock);
	else
		up_read(&vm->lock);
	if (err == -EAGAIN)
		goto retry;
err_syncs:
	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_cleanup(&syncs[i]);
	kfree(syncs);
err_engine:
	xe_engine_put(engine);

	return err;
}
