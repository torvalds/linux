// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <drm/drm_managed.h>
#include <linux/mutex.h>

#include "xe_cpu_bind.h"
#include "xe_device_types.h"
#include "xe_exec_queue.h"
#include "xe_pt.h"
#include "xe_sched_job.h"
#include "xe_trace_bo.h"
#include "xe_vm.h"

/**
 * struct xe_cpu_bind - cpu_bind context.
 */
struct xe_cpu_bind {
	/** @xe: Xe device */
	struct xe_device *xe;
	/** @q: Default exec queue used for kernel binds */
	struct xe_exec_queue *q;
	/** @job_mutex: Timeline mutex for @q. */
	struct mutex job_mutex;
};

static bool is_cpu_bind_queue(struct xe_cpu_bind *cpu_bind,
			      struct xe_exec_queue *q)
{
	return cpu_bind->q == q;
}

static void xe_cpu_bind_fini(void *arg)
{
	struct xe_cpu_bind *cpu_bind = arg;

	mutex_destroy(&cpu_bind->job_mutex);
	xe_exec_queue_put(cpu_bind->q);
}

/**
 * xe_cpu_bind_init() - Initialize a cpu_bind context
 * @xe: &struct xe_device
 *
 * Return: 0 if successful, negative error code on failure
 */
int xe_cpu_bind_init(struct xe_device *xe)
{
	struct xe_cpu_bind *cpu_bind =
		drmm_kzalloc(&xe->drm, sizeof(*cpu_bind), GFP_KERNEL);
	struct xe_exec_queue *q;

	if (!cpu_bind)
		return -ENOMEM;

	q = xe_exec_queue_create_bind(xe, xe_device_get_root_tile(xe), NULL,
				      EXEC_QUEUE_FLAG_KERNEL |
				      EXEC_QUEUE_FLAG_MIGRATE, 0);
	if (IS_ERR(q))
		return PTR_ERR(q);

	cpu_bind->xe = xe;
	cpu_bind->q = q;
	xe->cpu_bind = cpu_bind;

	mutex_init(&cpu_bind->job_mutex);

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&cpu_bind->job_mutex);
	fs_reclaim_release(GFP_KERNEL);

	return devm_add_action_or_reset(cpu_bind->xe->drm.dev, xe_cpu_bind_fini,
					cpu_bind);
}

/**
 * xe_cpu_bind_queue() - Get the bind queue from cpu_bind context.
 * @cpu_bind: The cpu bind context.
 *
 * Return: Pointer to bind queue.
 */
struct xe_exec_queue *xe_cpu_bind_queue(struct xe_cpu_bind *cpu_bind)
{
	return cpu_bind->q;
}

/**
 * xe_cpu_bind_update_pgtables_execute() - Update a VM's PTEs via the CPU
 * @vm: The VM being updated
 * @tile: The tile being updated
 * @ops: The CPU bind PT update ops
 * @pt_op: The VM PT update op
 * @num_ops: The number of The VM PT update ops
 * @force_clear: Force clear operation
 *
 * Execute the VM PT update ops array which results in a VM's PTEs being updated
 * via the CPU.
 */
void
xe_cpu_bind_update_pgtables_execute(struct xe_vm *vm, struct xe_tile *tile,
				    const struct xe_cpu_bind_pt_update_ops *ops,
				    struct xe_vm_pgtable_update_op *pt_op,
				    u32 num_ops, bool force_clear)
{
	u32 j, i;

	for (j = 0; j < num_ops; ++j, ++pt_op) {
		for (i = 0; i < pt_op->num_entries; i++) {
			const struct xe_vm_pgtable_update *update =
				&pt_op->entries[i];

			xe_assert(vm->xe, !iosys_map_is_null(&update->pt_bo->vmap));

			if (pt_op->bind && !force_clear)
				ops->populate(tile, &update->pt_bo->vmap,
					      update);
			else
				ops->clear(vm, tile, &update->pt_bo->vmap,
					   update);
		}
	}

	trace_xe_vm_cpu_bind(vm);
	xe_device_wmb(vm->xe);
}

static struct dma_fence *
xe_cpu_bind_update_pgtables_no_job(struct xe_cpu_bind *cpu_bind,
				   struct xe_cpu_bind_pt_update *pt_update)
{
	const struct xe_cpu_bind_pt_update_ops *ops = pt_update->ops;
	struct xe_vm *vm = pt_update->vops->vm;
	struct xe_tile *tile;
	int err, id;

	if (ops->pre_commit) {
		pt_update->job = NULL;
		err = ops->pre_commit(pt_update);
		if (err)
			return ERR_PTR(err);
	}

	for_each_tile(tile, vm->xe, id) {
		struct xe_vm_pgtable_update_ops *pt_update_ops =
			&pt_update->vops->pt_update_ops[tile->id];

		if (!pt_update_ops->pt_job_ops)
			continue;

		xe_cpu_bind_update_pgtables_execute(vm, tile, ops,
						    pt_update_ops->pt_job_ops->ops,
						    pt_update_ops->pt_job_ops->current_op,
						    false);
	}

	return dma_fence_get_stub();
}

static struct dma_fence *
xe_cpu_bind_update_pgtables_job(struct xe_cpu_bind *cpu_bind,
				struct xe_cpu_bind_pt_update *pt_update)
{
	const struct xe_cpu_bind_pt_update_ops *ops = pt_update->ops;
	struct xe_exec_queue *q = pt_update->vops->q;
	struct xe_device *xe = cpu_bind->xe;
	struct xe_sched_job *job;
	struct dma_fence *fence;
	struct xe_tile *tile;
	int err, id;
	bool is_cpu_bind = is_cpu_bind_queue(cpu_bind, q);

	job = xe_sched_job_create(q, NULL);
	if (IS_ERR(job))
		return ERR_CAST(job);

	xe_assert(xe, job->is_pt_job);

	if (ops->pre_commit) {
		pt_update->job = job;
		err = ops->pre_commit(pt_update);
		if (err)
			goto err_job;
	}

	if (is_cpu_bind)
		mutex_lock(&cpu_bind->job_mutex);

	job->pt_update[0].vm = pt_update->vops->vm;
	job->pt_update[0].ops = ops;
	for_each_tile(tile, xe, id) {
		struct xe_vm_pgtable_update_ops *pt_update_ops =
			&pt_update->vops->pt_update_ops[tile->id];

		job->pt_update[0].pt_job_ops[tile->id] =
			xe_pt_job_ops_get(pt_update_ops->pt_job_ops);
	}

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	if (is_cpu_bind)
		mutex_unlock(&cpu_bind->job_mutex);

	return fence;

err_job:
	xe_sched_job_put(job);
	return ERR_PTR(err);
}

/**
 * xe_cpu_bind_update_pgtables() - Pipelined page-table update
 * @cpu_bind: The cpu bind context.
 * @pt_update: PT update arguments
 *
 * Perform a pipelined page-table update. The update descriptors are typically
 * built under the same lock critical section as a call to this function. If
 * using the default engine for the updates, they will be performed in the
 * order they grab the job_mutex. If different engines are used, external
 * synchronization is needed for overlapping updates to maintain page-table
 * consistency. Note that the meaning of "overlapping" is that the updates
 * touch the same page-table, which might be a higher-level page-directory.
 * If no pipelining is needed, then updates may be performed by the cpu.
 *
 * Return: A dma_fence that, when signaled, indicates the update completion.
 */
struct dma_fence *
xe_cpu_bind_update_pgtables(struct xe_cpu_bind *cpu_bind,
			    struct xe_cpu_bind_pt_update *pt_update)
{
	struct dma_fence *fence;

	fence = xe_cpu_bind_update_pgtables_no_job(cpu_bind, pt_update);

	/* -ETIME indicates a job is needed, anything else is legit error */
	if (!IS_ERR(fence) || PTR_ERR(fence) != -ETIME)
		return fence;

	return xe_cpu_bind_update_pgtables_job(cpu_bind, pt_update);
}

/**
 * xe_cpu_bind_job_lock() - Lock cpu_bind job lock
 * @cpu_bind: The cpu bind context.
 * @q: Queue associated with the operation which requires a lock
 *
 * Lock the cpu_bind job lock if the queue is a cpu bind queue, otherwise
 * assert the VM's dma-resv is held (user queue's have own locking).
 */
void xe_cpu_bind_job_lock(struct xe_cpu_bind *cpu_bind,
			  struct xe_exec_queue *q)
{
	bool is_cpu_bind = is_cpu_bind_queue(cpu_bind, q);

	if (is_cpu_bind)
		mutex_lock(&cpu_bind->job_mutex);
	else
		xe_vm_assert_held(q->user_vm);	/* User queues VM's should be locked */
}

/**
 * xe_cpu_bind_job_unlock() - Unlock cpu_bind job lock
 * @cpu_bind: The cpu bind context.
 * @q: Queue associated with the operation which requires a lock
 *
 * Unlock the cpu_bind job lock if the queue is a cpu bind queue, otherwise
 * assert the VM's dma-resv is held (user queue's have own locking).
 */
void xe_cpu_bind_job_unlock(struct xe_cpu_bind *cpu_bind,
			    struct xe_exec_queue *q)
{
	bool is_cpu_bind = is_cpu_bind_queue(cpu_bind, q);

	if (is_cpu_bind)
		mutex_unlock(&cpu_bind->job_mutex);
	else
		xe_vm_assert_held(q->user_vm);	/* User queues VM's should be locked */
}

#if IS_ENABLED(CONFIG_PROVE_LOCKING)
/**
 * xe_cpu_bind_job_lock_assert() - Assert cpu_bind job lock held of queue
 * @q: cpu bind queue
 */
void xe_cpu_bind_job_lock_assert(struct xe_exec_queue *q)
{
	struct xe_device *xe = gt_to_xe(q->gt);
	struct xe_cpu_bind *cpu_bind = xe->cpu_bind;

	xe_assert(xe, q == cpu_bind->q);
	lockdep_assert_held(&cpu_bind->job_mutex);
}
#endif
