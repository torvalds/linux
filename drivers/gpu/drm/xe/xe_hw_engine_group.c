// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_hw_engine_group.h"
#include "xe_vm.h"

static void
hw_engine_group_free(struct drm_device *drm, void *arg)
{
	struct xe_hw_engine_group *group = arg;

	destroy_workqueue(group->resume_wq);
	kfree(group);
}

static void
hw_engine_group_resume_lr_jobs_func(struct work_struct *w)
{
	struct xe_exec_queue *q;
	struct xe_hw_engine_group *group = container_of(w, struct xe_hw_engine_group, resume_work);
	int err;
	enum xe_hw_engine_group_execution_mode previous_mode;

	err = xe_hw_engine_group_get_mode(group, EXEC_MODE_LR, &previous_mode);
	if (err)
		return;

	if (previous_mode == EXEC_MODE_LR)
		goto put;

	list_for_each_entry(q, &group->exec_queue_list, hw_engine_group_link) {
		if (!xe_vm_in_fault_mode(q->vm))
			continue;

		q->ops->resume(q);
	}

put:
	xe_hw_engine_group_put(group);
}

static struct xe_hw_engine_group *
hw_engine_group_alloc(struct xe_device *xe)
{
	struct xe_hw_engine_group *group;
	int err;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	group->resume_wq = alloc_workqueue("xe-resume-lr-jobs-wq", 0, 0);
	if (!group->resume_wq)
		return ERR_PTR(-ENOMEM);

	init_rwsem(&group->mode_sem);
	INIT_WORK(&group->resume_work, hw_engine_group_resume_lr_jobs_func);
	INIT_LIST_HEAD(&group->exec_queue_list);

	err = drmm_add_action_or_reset(&xe->drm, hw_engine_group_free, group);
	if (err)
		return ERR_PTR(err);

	return group;
}

/**
 * xe_hw_engine_setup_groups() - Setup the hw engine groups for the gt
 * @gt: The gt for which groups are setup
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_hw_engine_setup_groups(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_hw_engine_group *group_rcs_ccs, *group_bcs, *group_vcs_vecs;
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	group_rcs_ccs = hw_engine_group_alloc(xe);
	if (IS_ERR(group_rcs_ccs)) {
		err = PTR_ERR(group_rcs_ccs);
		goto err_group_rcs_ccs;
	}

	group_bcs = hw_engine_group_alloc(xe);
	if (IS_ERR(group_bcs)) {
		err = PTR_ERR(group_bcs);
		goto err_group_bcs;
	}

	group_vcs_vecs = hw_engine_group_alloc(xe);
	if (IS_ERR(group_vcs_vecs)) {
		err = PTR_ERR(group_vcs_vecs);
		goto err_group_vcs_vecs;
	}

	for_each_hw_engine(hwe, gt, id) {
		switch (hwe->class) {
		case XE_ENGINE_CLASS_COPY:
			hwe->hw_engine_group = group_bcs;
			break;
		case XE_ENGINE_CLASS_RENDER:
		case XE_ENGINE_CLASS_COMPUTE:
			hwe->hw_engine_group = group_rcs_ccs;
			break;
		case XE_ENGINE_CLASS_VIDEO_DECODE:
		case XE_ENGINE_CLASS_VIDEO_ENHANCE:
			hwe->hw_engine_group = group_vcs_vecs;
			break;
		case XE_ENGINE_CLASS_OTHER:
			break;
		default:
			drm_warn(&xe->drm, "NOT POSSIBLE");
		}
	}

	return 0;

err_group_vcs_vecs:
	kfree(group_vcs_vecs);
err_group_bcs:
	kfree(group_bcs);
err_group_rcs_ccs:
	kfree(group_rcs_ccs);

	return err;
}

/**
 * xe_hw_engine_group_add_exec_queue() - Add an exec queue to a hw engine group
 * @group: The hw engine group
 * @q: The exec_queue
 *
 * Return: 0 on success,
 *	    -EINTR if the lock could not be acquired
 */
int xe_hw_engine_group_add_exec_queue(struct xe_hw_engine_group *group, struct xe_exec_queue *q)
{
	int err;
	struct xe_device *xe = gt_to_xe(q->gt);

	xe_assert(xe, group);
	xe_assert(xe, !(q->flags & EXEC_QUEUE_FLAG_VM));
	xe_assert(xe, q->vm);

	if (xe_vm_in_preempt_fence_mode(q->vm))
		return 0;

	err = down_write_killable(&group->mode_sem);
	if (err)
		return err;

	if (xe_vm_in_fault_mode(q->vm) && group->cur_mode == EXEC_MODE_DMA_FENCE) {
		q->ops->suspend(q);
		err = q->ops->suspend_wait(q);
		if (err)
			goto err_suspend;

		xe_hw_engine_group_resume_faulting_lr_jobs(group);
	}

	list_add(&q->hw_engine_group_link, &group->exec_queue_list);
	up_write(&group->mode_sem);

	return 0;

err_suspend:
	up_write(&group->mode_sem);
	return err;
}

/**
 * xe_hw_engine_group_del_exec_queue() - Delete an exec queue from a hw engine group
 * @group: The hw engine group
 * @q: The exec_queue
 */
void xe_hw_engine_group_del_exec_queue(struct xe_hw_engine_group *group, struct xe_exec_queue *q)
{
	struct xe_device *xe = gt_to_xe(q->gt);

	xe_assert(xe, group);
	xe_assert(xe, q->vm);

	down_write(&group->mode_sem);

	if (!list_empty(&q->hw_engine_group_link))
		list_del(&q->hw_engine_group_link);

	up_write(&group->mode_sem);
}

/**
 * xe_hw_engine_group_resume_faulting_lr_jobs() - Asynchronously resume the hw engine group's
 * faulting LR jobs
 * @group: The hw engine group
 */
void xe_hw_engine_group_resume_faulting_lr_jobs(struct xe_hw_engine_group *group)
{
	queue_work(group->resume_wq, &group->resume_work);
}

/**
 * xe_hw_engine_group_suspend_faulting_lr_jobs() - Suspend the faulting LR jobs of this group
 * @group: The hw engine group
 *
 * Return: 0 on success, negative error code on error.
 */
static int xe_hw_engine_group_suspend_faulting_lr_jobs(struct xe_hw_engine_group *group)
{
	int err;
	struct xe_exec_queue *q;
	bool need_resume = false;

	lockdep_assert_held_write(&group->mode_sem);

	list_for_each_entry(q, &group->exec_queue_list, hw_engine_group_link) {
		if (!xe_vm_in_fault_mode(q->vm))
			continue;

		need_resume = true;
		q->ops->suspend(q);
	}

	list_for_each_entry(q, &group->exec_queue_list, hw_engine_group_link) {
		if (!xe_vm_in_fault_mode(q->vm))
			continue;

		err = q->ops->suspend_wait(q);
		if (err)
			goto err_suspend;
	}

	if (need_resume)
		xe_hw_engine_group_resume_faulting_lr_jobs(group);

	return 0;

err_suspend:
	up_write(&group->mode_sem);
	return err;
}

/**
 * xe_hw_engine_group_wait_for_dma_fence_jobs() - Wait for dma fence jobs to complete
 * @group: The hw engine group
 *
 * This function is not meant to be called directly from a user IOCTL as dma_fence_wait()
 * is not interruptible.
 *
 * Return: 0 on success,
 *	   -ETIME if waiting for one job failed
 */
static int xe_hw_engine_group_wait_for_dma_fence_jobs(struct xe_hw_engine_group *group)
{
	long timeout;
	struct xe_exec_queue *q;
	struct dma_fence *fence;

	lockdep_assert_held_write(&group->mode_sem);

	list_for_each_entry(q, &group->exec_queue_list, hw_engine_group_link) {
		if (xe_vm_in_lr_mode(q->vm))
			continue;

		fence = xe_exec_queue_last_fence_get_for_resume(q, q->vm);
		timeout = dma_fence_wait(fence, false);
		dma_fence_put(fence);

		if (timeout < 0)
			return -ETIME;
	}

	return 0;
}

static int switch_mode(struct xe_hw_engine_group *group)
{
	int err = 0;
	enum xe_hw_engine_group_execution_mode new_mode;

	lockdep_assert_held_write(&group->mode_sem);

	switch (group->cur_mode) {
	case EXEC_MODE_LR:
		new_mode = EXEC_MODE_DMA_FENCE;
		err = xe_hw_engine_group_suspend_faulting_lr_jobs(group);
		break;
	case EXEC_MODE_DMA_FENCE:
		new_mode = EXEC_MODE_LR;
		err = xe_hw_engine_group_wait_for_dma_fence_jobs(group);
		break;
	}

	if (err)
		return err;

	group->cur_mode = new_mode;

	return 0;
}

/**
 * xe_hw_engine_group_get_mode() - Get the group to execute in the new mode
 * @group: The hw engine group
 * @new_mode: The new execution mode
 * @previous_mode: Pointer to the previous mode provided for use by caller
 *
 * Return: 0 if successful, -EINTR if locking failed.
 */
int xe_hw_engine_group_get_mode(struct xe_hw_engine_group *group,
				enum xe_hw_engine_group_execution_mode new_mode,
				enum xe_hw_engine_group_execution_mode *previous_mode)
__acquires(&group->mode_sem)
{
	int err = down_read_interruptible(&group->mode_sem);

	if (err)
		return err;

	*previous_mode = group->cur_mode;

	if (new_mode != group->cur_mode) {
		up_read(&group->mode_sem);
		err = down_write_killable(&group->mode_sem);
		if (err)
			return err;

		if (new_mode != group->cur_mode) {
			err = switch_mode(group);
			if (err) {
				up_write(&group->mode_sem);
				return err;
			}
		}
		downgrade_write(&group->mode_sem);
	}

	return err;
}

/**
 * xe_hw_engine_group_put() - Put the group
 * @group: The hw engine group
 */
void xe_hw_engine_group_put(struct xe_hw_engine_group *group)
__releases(&group->mode_sem)
{
	up_read(&group->mode_sem);
}

/**
 * xe_hw_engine_group_find_exec_mode() - Find the execution mode for this exec queue
 * @q: The exec_queue
 */
enum xe_hw_engine_group_execution_mode
xe_hw_engine_group_find_exec_mode(struct xe_exec_queue *q)
{
	if (xe_vm_in_fault_mode(q->vm))
		return EXEC_MODE_LR;
	else
		return EXEC_MODE_DMA_FENCE;
}
