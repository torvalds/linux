// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_exec.h"

#include <drm/drm_device.h>
#include <drm/drm_exec.h>
#include <drm/drm_file.h>
#include <drm/xe_drm.h>
#include <linux/delay.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_macros.h"
#include "xe_ring_ops_types.h"
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

static int xe_exec_fn(struct drm_gpuvm_exec *vm_exec)
{
	struct xe_vm *vm = container_of(vm_exec->vm, struct xe_vm, gpuvm);
	struct drm_gem_object *obj;
	unsigned long index;
	int num_fences;
	int ret;

	ret = drm_gpuvm_validate(vm_exec->vm, &vm_exec->exec);
	if (ret)
		return ret;

	/*
	 * 1 fence slot for the final submit, and 1 more for every per-tile for
	 * GPU bind and 1 extra for CPU bind. Note that there are potentially
	 * many vma per object/dma-resv, however the fence slot will just be
	 * re-used, since they are largely the same timeline and the seqno
	 * should be in order. In the case of CPU bind there is dummy fence used
	 * for all CPU binds, so no need to have a per-tile slot for that.
	 */
	num_fences = 1 + 1 + vm->xe->info.tile_count;

	/*
	 * We don't know upfront exactly how many fence slots we will need at
	 * the start of the exec, since the TTM bo_validate above can consume
	 * numerous fence slots. Also due to how the dma_resv_reserve_fences()
	 * works it only ensures that at least that many fence slots are
	 * available i.e if there are already 10 slots available and we reserve
	 * two more, it can just noop without reserving anything.  With this it
	 * is quite possible that TTM steals some of the fence slots and then
	 * when it comes time to do the vma binding and final exec stage we are
	 * lacking enough fence slots, leading to some nasty BUG_ON() when
	 * adding the fences. Hence just add our own fences here, after the
	 * validate stage.
	 */
	drm_exec_for_each_locked_object(&vm_exec->exec, index, obj) {
		ret = dma_resv_reserve_fences(obj->resv, num_fences);
		if (ret)
			return ret;
	}

	return 0;
}

int xe_exec_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_exec *args = data;
	struct drm_xe_sync __user *syncs_user = u64_to_user_ptr(args->syncs);
	u64 __user *addresses_user = u64_to_user_ptr(args->address);
	struct xe_exec_queue *q;
	struct xe_sync_entry *syncs = NULL;
	u64 addresses[XE_HW_ENGINE_MAX_INSTANCE];
	struct drm_gpuvm_exec vm_exec = {.extra.fn = xe_exec_fn};
	struct drm_exec *exec = &vm_exec.exec;
	u32 i, num_syncs = 0, num_ufence = 0;
	struct xe_sched_job *job;
	struct dma_fence *rebind_fence;
	struct xe_vm *vm;
	bool write_locked, skip_retry = false;
	ktime_t end = 0;
	int err = 0;

	if (XE_IOCTL_DBG(xe, args->extensions) ||
	    XE_IOCTL_DBG(xe, args->pad[0] || args->pad[1] || args->pad[2]) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	q = xe_exec_queue_lookup(xef, args->exec_queue_id);
	if (XE_IOCTL_DBG(xe, !q))
		return -ENOENT;

	if (XE_IOCTL_DBG(xe, q->flags & EXEC_QUEUE_FLAG_VM))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->num_batch_buffer &&
			 q->width != args->num_batch_buffer))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, q->flags & EXEC_QUEUE_FLAG_BANNED)) {
		err = -ECANCELED;
		goto err_exec_queue;
	}

	if (args->num_syncs) {
		syncs = kcalloc(args->num_syncs, sizeof(*syncs), GFP_KERNEL);
		if (!syncs) {
			err = -ENOMEM;
			goto err_exec_queue;
		}
	}

	vm = q->vm;

	for (i = 0; i < args->num_syncs; i++) {
		err = xe_sync_entry_parse(xe, xef, &syncs[num_syncs++],
					  &syncs_user[i], SYNC_PARSE_FLAG_EXEC |
					  (xe_vm_in_lr_mode(vm) ?
					   SYNC_PARSE_FLAG_LR_MODE : 0));
		if (err)
			goto err_syncs;

		if (xe_sync_is_ufence(&syncs[i]))
			num_ufence++;
	}

	if (XE_IOCTL_DBG(xe, num_ufence > 1)) {
		err = -EINVAL;
		goto err_syncs;
	}

	if (xe_exec_queue_is_parallel(q)) {
		err = __copy_from_user(addresses, addresses_user, sizeof(u64) *
				       q->width);
		if (err) {
			err = -EFAULT;
			goto err_syncs;
		}
	}

retry:
	if (!xe_vm_in_lr_mode(vm) && xe_vm_userptr_check_repin(vm)) {
		err = down_write_killable(&vm->lock);
		write_locked = true;
	} else {
		/* We don't allow execs while the VM is in error state */
		err = down_read_interruptible(&vm->lock);
		write_locked = false;
	}
	if (err)
		goto err_syncs;

	if (write_locked) {
		err = xe_vm_userptr_pin(vm);
		downgrade_write(&vm->lock);
		write_locked = false;
		if (err)
			goto err_unlock_list;
	}

	vm_exec.vm = &vm->gpuvm;
	vm_exec.flags = DRM_EXEC_INTERRUPTIBLE_WAIT;
	if (xe_vm_in_lr_mode(vm)) {
		drm_exec_init(exec, vm_exec.flags, 0);
	} else {
		err = drm_gpuvm_exec_lock(&vm_exec);
		if (err) {
			if (xe_vm_validate_should_retry(exec, err, &end))
				err = -EAGAIN;
			goto err_unlock_list;
		}
	}

	if (xe_vm_is_closed_or_banned(q->vm)) {
		drm_warn(&xe->drm, "Trying to schedule after vm is closed or banned\n");
		err = -ECANCELED;
		goto err_exec;
	}

	if (!args->num_batch_buffer) {
		if (!xe_vm_in_lr_mode(vm)) {
			struct dma_fence *fence;

			fence = xe_sync_in_fence_get(syncs, num_syncs, q, vm);
			if (IS_ERR(fence)) {
				err = PTR_ERR(fence);
				goto err_exec;
			}
			for (i = 0; i < num_syncs; i++)
				xe_sync_entry_signal(&syncs[i], NULL, fence);
			xe_exec_queue_last_fence_set(q, vm, fence);
			dma_fence_put(fence);
		}

		goto err_exec;
	}

	if (xe_exec_queue_is_lr(q) && xe_exec_queue_ring_full(q)) {
		err = -EWOULDBLOCK;	/* Aliased to -EAGAIN */
		skip_retry = true;
		goto err_exec;
	}

	job = xe_sched_job_create(q, xe_exec_queue_is_parallel(q) ?
				  addresses : &args->address);
	if (IS_ERR(job)) {
		err = PTR_ERR(job);
		goto err_exec;
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
	if (!xe_vm_in_lr_mode(vm)) {
		err = drm_sched_job_add_resv_dependencies(&job->drm,
							  xe_vm_resv(vm),
							  DMA_RESV_USAGE_KERNEL);
		if (err)
			goto err_put_job;
	}

	for (i = 0; i < num_syncs && !err; i++)
		err = xe_sync_entry_add_deps(&syncs[i], job);
	if (err)
		goto err_put_job;

	if (!xe_vm_in_lr_mode(vm)) {
		err = xe_sched_job_last_fence_add_dep(job, vm);
		if (err)
			goto err_put_job;

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
	if (!xe_vm_in_lr_mode(vm))
		drm_gpuvm_resv_add_fence(&vm->gpuvm, exec, &job->drm.s_fence->finished,
					 DMA_RESV_USAGE_BOOKKEEP, DMA_RESV_USAGE_WRITE);

	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], job,
				     &job->drm.s_fence->finished);

	if (xe_exec_queue_is_lr(q))
		q->ring_ops->emit_job(job);
	if (!xe_vm_in_lr_mode(vm))
		xe_exec_queue_last_fence_set(q, vm, &job->drm.s_fence->finished);
	xe_sched_job_push(job);
	xe_vm_reactivate_rebind(vm);

	if (!err && !xe_vm_in_lr_mode(vm)) {
		spin_lock(&xe->ttm.lru_lock);
		ttm_lru_bulk_move_tail(&vm->lru_bulk_move);
		spin_unlock(&xe->ttm.lru_lock);
	}

err_repin:
	if (!xe_vm_in_lr_mode(vm))
		up_read(&vm->userptr.notifier_lock);
err_put_job:
	if (err)
		xe_sched_job_put(job);
err_exec:
	drm_exec_fini(exec);
err_unlock_list:
	if (write_locked)
		up_write(&vm->lock);
	else
		up_read(&vm->lock);
	if (err == -EAGAIN && !skip_retry)
		goto retry;
err_syncs:
	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_cleanup(&syncs[i]);
	kfree(syncs);
err_exec_queue:
	xe_exec_queue_put(q);

	return err;
}
