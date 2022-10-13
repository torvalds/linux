// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2018-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include "mali_kbase_config_defaults.h"
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_reset_gpu.h>
#include <mali_kbase_as_fault_debugfs.h>
#include "mali_kbase_csf.h"
#include <tl/mali_kbase_tracepoints.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <linux/export.h>
#include <csf/mali_kbase_csf_registers.h>
#include <uapi/gpu/arm/bifrost/mali_base_kernel.h>
#include <mali_kbase_hwaccess_time.h>
#include "mali_kbase_csf_tiler_heap_reclaim.h"

/* Value to indicate that a queue group is not groups_to_schedule list */
#define KBASEP_GROUP_PREPARED_SEQ_NUM_INVALID (U32_MAX)

/* This decides the upper limit on the waiting time for the Scheduler
 * to exit the sleep state. Usually the value of autosuspend_delay is
 * expected to be around 100 milli seconds.
 */
#define MAX_AUTO_SUSPEND_DELAY_MS (5000)

/* Maximum number of endpoints which may run tiler jobs. */
#define CSG_TILER_MAX ((u8)1)

/* Maximum dynamic CSG slot priority value */
#define MAX_CSG_SLOT_PRIORITY ((u8)15)

/* CSF scheduler time slice value */
#define CSF_SCHEDULER_TIME_TICK_MS (100) /* 100 milliseconds */

/* A GPU address space slot is reserved for MCU. */
#define NUM_RESERVED_AS_SLOTS (1)

/* Time to wait for completion of PING req before considering MCU as hung */
#define FW_PING_AFTER_ERROR_TIMEOUT_MS (10)

static int scheduler_group_schedule(struct kbase_queue_group *group);
static void remove_group_from_idle_wait(struct kbase_queue_group *const group);
static
void insert_group_to_runnable(struct kbase_csf_scheduler *const scheduler,
		struct kbase_queue_group *const group,
		enum kbase_csf_group_state run_state);
static struct kbase_queue_group *scheduler_get_protm_enter_async_group(
		struct kbase_device *const kbdev,
		struct kbase_queue_group *const group);
static struct kbase_queue_group *get_tock_top_group(
	struct kbase_csf_scheduler *const scheduler);
static void scheduler_enable_tick_timer_nolock(struct kbase_device *kbdev);
static int suspend_active_queue_groups(struct kbase_device *kbdev,
				       unsigned long *slot_mask);
static int suspend_active_groups_on_powerdown(struct kbase_device *kbdev,
					      bool system_suspend);
static void schedule_in_cycle(struct kbase_queue_group *group, bool force);
static bool queue_group_scheduled_locked(struct kbase_queue_group *group);

#define kctx_as_enabled(kctx) (!kbase_ctx_flag(kctx, KCTX_AS_DISABLED_ON_FAULT))

/**
 * wait_for_dump_complete_on_group_deschedule() - Wait for dump on fault and
 *              scheduling tick/tock to complete before the group deschedule.
 *
 * @group: Pointer to the group that is being descheduled.
 *
 * This function blocks the descheduling of the group until the dump on fault is
 * completed and scheduling tick/tock has completed.
 * To deschedule an on slot group CSG termination request would be sent and that
 * might time out if the fault had occurred and also potentially affect the state
 * being dumped. Moreover the scheduler lock would be held, so the access to debugfs
 * files would get blocked.
 * Scheduler lock and 'kctx->csf.lock' are released before this function starts
 * to wait. When a request sent by the Scheduler to the FW times out, Scheduler
 * would also wait for the dumping to complete and release the Scheduler lock
 * before the wait. Meanwhile Userspace can try to delete the group, this function
 * would ensure that the group doesn't exit the Scheduler until scheduling
 * tick/tock has completed. Though very unlikely, group deschedule can be triggered
 * from multiple threads around the same time and after the wait Userspace thread
 * can win the race and get the group descheduled and free the memory for group
 * pointer before the other threads wake up and notice that group has already been
 * descheduled. To avoid the freeing in such a case, a sort of refcount is used
 * for the group which is incremented & decremented across the wait.
 */
static
void wait_for_dump_complete_on_group_deschedule(struct kbase_queue_group *group)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_context *kctx = group->kctx;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&kctx->csf.lock);
	lockdep_assert_held(&scheduler->lock);

	if (likely(!kbase_debug_csf_fault_dump_enabled(kbdev)))
		return;

	while ((!kbase_debug_csf_fault_dump_complete(kbdev) ||
	       (scheduler->state == SCHED_BUSY)) &&
	       queue_group_scheduled_locked(group)) {
		group->deschedule_deferred_cnt++;
		mutex_unlock(&scheduler->lock);
		mutex_unlock(&kctx->csf.lock);
		kbase_debug_csf_fault_wait_completion(kbdev);
		mutex_lock(&kctx->csf.lock);
		mutex_lock(&scheduler->lock);
		group->deschedule_deferred_cnt--;
	}
#endif
}

/**
 * schedule_actions_trigger_df() - Notify the client about the fault and
 *                                 wait for the dumping to complete.
 *
 * @kbdev: Pointer to the device
 * @kctx:  Pointer to the context associated with the CSG slot for which
 *         the timeout was seen.
 * @error: Error code indicating the type of timeout that occurred.
 *
 * This function notifies the Userspace client waiting for the faults and wait
 * for the Client to complete the dumping.
 * The function is called only from Scheduling tick/tock when a request sent by
 * the Scheduler to FW times out or from the protm event work item of the group
 * when the protected mode entry request times out.
 * In the latter case there is no wait done as scheduler lock would be released
 * immediately. In the former case the function waits and releases the scheduler
 * lock before the wait. It has been ensured that the Scheduler view of the groups
 * won't change meanwhile, so no group can enter/exit the Scheduler, become
 * runnable or go off slot.
 */
static void schedule_actions_trigger_df(struct kbase_device *kbdev,
	struct kbase_context *kctx, enum dumpfault_error_type error)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	if (!kbase_debug_csf_fault_notify(kbdev, kctx, error))
		return;

	if (unlikely(scheduler->state != SCHED_BUSY)) {
		WARN_ON(error != DF_PROTECTED_MODE_ENTRY_FAILURE);
		return;
	}

	mutex_unlock(&scheduler->lock);
	kbase_debug_csf_fault_wait_completion(kbdev);
	mutex_lock(&scheduler->lock);
	WARN_ON(scheduler->state != SCHED_BUSY);
#endif
}

#ifdef KBASE_PM_RUNTIME
/**
 * wait_for_scheduler_to_exit_sleep() - Wait for Scheduler to exit the
 *                                      sleeping state.
 *
 * @kbdev: Pointer to the device
 *
 * This function waits until the Scheduler has exited the sleep state and
 * it is called when an on-slot group is terminated or when the suspend
 * buffer of an on-slot group needs to be captured.
 *
 * Return: 0 when the wait is successful, otherwise an error code.
 */
static int wait_for_scheduler_to_exit_sleep(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	int autosuspend_delay = kbdev->dev->power.autosuspend_delay;
	unsigned int sleep_exit_wait_time;
	long remaining;
	int ret = 0;

	lockdep_assert_held(&scheduler->lock);
	WARN_ON(scheduler->state != SCHED_SLEEPING);

	/* No point in waiting if autosuspend_delay value is negative.
	 * For the negative value of autosuspend_delay Driver will directly
	 * go for the suspend of Scheduler, but the autosuspend_delay value
	 * could have been changed after the sleep was initiated.
	 */
	if (autosuspend_delay < 0)
		return -EINVAL;

	if (autosuspend_delay > MAX_AUTO_SUSPEND_DELAY_MS)
		autosuspend_delay = MAX_AUTO_SUSPEND_DELAY_MS;

	/* Usually Scheduler would remain in sleeping state until the
	 * auto-suspend timer expires and all active CSGs are suspended.
	 */
	sleep_exit_wait_time = autosuspend_delay + kbdev->reset_timeout_ms;

	remaining = kbase_csf_timeout_in_jiffies(sleep_exit_wait_time);

	while ((scheduler->state == SCHED_SLEEPING) && !ret) {
		mutex_unlock(&scheduler->lock);
		remaining = wait_event_timeout(
				kbdev->csf.event_wait,
				(scheduler->state != SCHED_SLEEPING),
				remaining);
		mutex_lock(&scheduler->lock);
		if (!remaining && (scheduler->state == SCHED_SLEEPING))
			ret = -ETIMEDOUT;
	}

	return ret;
}

/**
 * force_scheduler_to_exit_sleep() - Force scheduler to exit sleep state
 *
 * @kbdev: Pointer to the device
 *
 * This function will force the Scheduler to exit the sleep state by doing the
 * wake up of MCU and suspension of on-slot groups. It is called at the time of
 * system suspend.
 *
 * Return: 0 on success.
 */
static int force_scheduler_to_exit_sleep(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned long flags;
	int ret = 0;

	lockdep_assert_held(&scheduler->lock);
	WARN_ON(scheduler->state != SCHED_SLEEPING);
	WARN_ON(!kbdev->pm.backend.gpu_sleep_mode_active);

	kbase_pm_lock(kbdev);
	ret = kbase_pm_force_mcu_wakeup_after_sleep(kbdev);
	kbase_pm_unlock(kbdev);
	if (ret) {
		dev_warn(kbdev->dev,
			 "[%llu] Wait for MCU wake up failed on forced scheduler suspend",
			 kbase_backend_get_cycle_cnt(kbdev));
		goto out;
	}

	ret = suspend_active_groups_on_powerdown(kbdev, true);
	if (ret)
		goto out;

	kbase_pm_lock(kbdev);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.gpu_sleep_mode_active = false;
	kbdev->pm.backend.gpu_wakeup_override = false;
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	ret = kbase_pm_wait_for_desired_state(kbdev);
	kbase_pm_unlock(kbdev);
	if (ret) {
		dev_warn(kbdev->dev,
			 "[%llu] Wait for pm state change failed on forced scheduler suspend",
			 kbase_backend_get_cycle_cnt(kbdev));
		goto out;
	}

	scheduler->state = SCHED_SUSPENDED;
	KBASE_KTRACE_ADD(kbdev, SCHED_SUSPENDED, NULL, scheduler->state);

	return 0;

out:
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.exit_gpu_sleep_mode = true;
	kbdev->pm.backend.gpu_wakeup_override = false;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	kbase_csf_scheduler_invoke_tick(kbdev);

	return ret;
}
#endif

/**
 * tick_timer_callback() - Callback function for the scheduling tick hrtimer
 *
 * @timer: Pointer to the scheduling tick hrtimer
 *
 * This function will enqueue the scheduling tick work item for immediate
 * execution, if it has not been queued already.
 *
 * Return: enum value to indicate that timer should not be restarted.
 */
static enum hrtimer_restart tick_timer_callback(struct hrtimer *timer)
{
	struct kbase_device *kbdev = container_of(timer, struct kbase_device,
						  csf.scheduler.tick_timer);

	kbase_csf_scheduler_tick_advance(kbdev);
	return HRTIMER_NORESTART;
}

/**
 * start_tick_timer() - Start the scheduling tick hrtimer.
 *
 * @kbdev: Pointer to the device
 *
 * This function will start the scheduling tick hrtimer and is supposed to
 * be called only from the tick work item function. The tick hrtimer should
 * not be active already.
 */
static void start_tick_timer(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	unsigned long flags;

	lockdep_assert_held(&scheduler->lock);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	WARN_ON(scheduler->tick_timer_active);
	if (likely(!work_pending(&scheduler->tick_work))) {
		scheduler->tick_timer_active = true;

		hrtimer_start(&scheduler->tick_timer,
		    HR_TIMER_DELAY_MSEC(scheduler->csg_scheduling_period_ms),
		    HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
}

/**
 * cancel_tick_timer() - Cancel the scheduling tick hrtimer
 *
 * @kbdev: Pointer to the device
 */
static void cancel_tick_timer(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	unsigned long flags;

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	scheduler->tick_timer_active = false;
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
	hrtimer_cancel(&scheduler->tick_timer);
}

/**
 * enqueue_tick_work() - Enqueue the scheduling tick work item
 *
 * @kbdev: Pointer to the device
 *
 * This function will queue the scheduling tick work item for immediate
 * execution. This shall only be called when both the tick hrtimer and tick
 * work item are not active/pending.
 */
static void enqueue_tick_work(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	kbase_csf_scheduler_invoke_tick(kbdev);
}

static void release_doorbell(struct kbase_device *kbdev, int doorbell_nr)
{
	WARN_ON(doorbell_nr >= CSF_NUM_DOORBELL);

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	clear_bit(doorbell_nr, kbdev->csf.scheduler.doorbell_inuse_bitmap);
}

static int acquire_doorbell(struct kbase_device *kbdev)
{
	int doorbell_nr;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	doorbell_nr = find_first_zero_bit(
			kbdev->csf.scheduler.doorbell_inuse_bitmap,
			CSF_NUM_DOORBELL);

	if (doorbell_nr >= CSF_NUM_DOORBELL)
		return KBASEP_USER_DB_NR_INVALID;

	set_bit(doorbell_nr, kbdev->csf.scheduler.doorbell_inuse_bitmap);

	return doorbell_nr;
}

static void unassign_user_doorbell_from_group(struct kbase_device *kbdev,
		struct kbase_queue_group *group)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (group->doorbell_nr != KBASEP_USER_DB_NR_INVALID) {
		release_doorbell(kbdev, group->doorbell_nr);
		group->doorbell_nr = KBASEP_USER_DB_NR_INVALID;
	}
}

static void unassign_user_doorbell_from_queue(struct kbase_device *kbdev,
		struct kbase_queue *queue)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	mutex_lock(&kbdev->csf.reg_lock);

	if (queue->doorbell_nr != KBASEP_USER_DB_NR_INVALID) {
		queue->doorbell_nr = KBASEP_USER_DB_NR_INVALID;
		/* After this the dummy page would be mapped in */
		unmap_mapping_range(kbdev->csf.db_filp->f_inode->i_mapping,
			queue->db_file_offset << PAGE_SHIFT, PAGE_SIZE, 1);
	}

	mutex_unlock(&kbdev->csf.reg_lock);
}

static void assign_user_doorbell_to_group(struct kbase_device *kbdev,
		struct kbase_queue_group *group)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (group->doorbell_nr == KBASEP_USER_DB_NR_INVALID)
		group->doorbell_nr = acquire_doorbell(kbdev);
}

static void assign_user_doorbell_to_queue(struct kbase_device *kbdev,
		struct kbase_queue *const queue)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	mutex_lock(&kbdev->csf.reg_lock);

	/* If bind operation for the queue hasn't completed yet, then the
	 * CSI can't be programmed for the queue
	 * (even in stopped state) and so the doorbell also can't be assigned
	 * to it.
	 */
	if ((queue->bind_state == KBASE_CSF_QUEUE_BOUND) &&
	    (queue->doorbell_nr == KBASEP_USER_DB_NR_INVALID)) {
		WARN_ON(queue->group->doorbell_nr == KBASEP_USER_DB_NR_INVALID);
		queue->doorbell_nr = queue->group->doorbell_nr;

		/* After this the real Hw doorbell page would be mapped in */
		unmap_mapping_range(
				kbdev->csf.db_filp->f_inode->i_mapping,
				queue->db_file_offset << PAGE_SHIFT,
				PAGE_SIZE, 1);
	}

	mutex_unlock(&kbdev->csf.reg_lock);
}

static void scheduler_doorbell_init(struct kbase_device *kbdev)
{
	int doorbell_nr;

	bitmap_zero(kbdev->csf.scheduler.doorbell_inuse_bitmap,
		CSF_NUM_DOORBELL);

	mutex_lock(&kbdev->csf.scheduler.lock);
	/* Reserve doorbell 0 for use by kernel driver */
	doorbell_nr = acquire_doorbell(kbdev);
	mutex_unlock(&kbdev->csf.scheduler.lock);

	WARN_ON(doorbell_nr != CSF_KERNEL_DOORBELL_NR);
}

/**
 * update_on_slot_queues_offsets - Update active queues' INSERT & EXTRACT ofs
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function updates the EXTRACT offset for all queues which groups have
 * been assigned a physical slot. These values could be used to detect a
 * queue's true idleness status. This is intended to be an additional check
 * on top of the GPU idle notification to account for race conditions.
 * This function is supposed to be called only when GPU idle notification
 * interrupt is received.
 */
static void update_on_slot_queues_offsets(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	/* All CSGs have the same number of CSs */
	size_t const max_streams = kbdev->csf.global_iface.groups[0].stream_num;
	size_t i;

	lockdep_assert_held(&scheduler->interrupt_lock);

	/* csg_slots_idle_mask is not used here for the looping, as it could get
	 * updated concurrently when Scheduler re-evaluates the idle status of
	 * the CSGs for which idle notification was received previously.
	 */
	for_each_set_bit(i, scheduler->csg_inuse_bitmap, kbdev->csf.global_iface.group_num) {
		struct kbase_queue_group *const group = scheduler->csg_slots[i].resident_group;
		size_t j;

		if (WARN_ON(!group))
			continue;

		for (j = 0; j < max_streams; ++j) {
			struct kbase_queue *const queue = group->bound_queues[j];

			if (queue) {
				if (queue->user_io_addr) {
					u64 const *const output_addr =
						(u64 const *)(queue->user_io_addr + PAGE_SIZE);

					queue->extract_ofs =
						output_addr[CS_EXTRACT_LO / sizeof(u64)];
				} else {
					dev_warn(kbdev->dev,
						 "%s(): queue->user_io_addr is NULL, queue: %p",
						 __func__,
						 queue);
				}
			}
		}
	}
}

static void enqueue_gpu_idle_work(struct kbase_csf_scheduler *const scheduler)
{
	atomic_set(&scheduler->gpu_no_longer_idle, false);
	queue_work(scheduler->idle_wq, &scheduler->gpu_idle_work);
}

void kbase_csf_scheduler_process_gpu_idle_event(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	int non_idle_offslot_grps;
	bool can_suspend_on_idle;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&scheduler->interrupt_lock);

	non_idle_offslot_grps = atomic_read(&scheduler->non_idle_offslot_grps);
	can_suspend_on_idle = kbase_pm_idle_groups_sched_suspendable(kbdev);
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_GPU_IDLE_EVENT_CAN_SUSPEND, NULL,
			 ((u64)(u32)non_idle_offslot_grps) | (((u64)can_suspend_on_idle) << 32));

	if (!non_idle_offslot_grps) {
		if (can_suspend_on_idle) {
			/* fast_gpu_idle_handling is protected by the
			 * interrupt_lock, which would prevent this from being
			 * updated whilst gpu_idle_worker() is executing.
			 */
			scheduler->fast_gpu_idle_handling =
				(kbdev->csf.gpu_idle_hysteresis_ms == 0) ||
				!kbase_csf_scheduler_all_csgs_idle(kbdev);

			/* The GPU idle worker relies on update_on_slot_queues_offsets() to have
			 * finished. It's queued before to reduce the time it takes till execution
			 * but it'll eventually be blocked by the scheduler->interrupt_lock.
			 */
			enqueue_gpu_idle_work(scheduler);

			/* The extract offsets are unused in fast GPU idle handling */
			if (!scheduler->fast_gpu_idle_handling)
				update_on_slot_queues_offsets(kbdev);
		}
	} else {
		/* Advance the scheduling tick to get the non-idle suspended groups loaded soon */
		kbase_csf_scheduler_tick_advance_nolock(kbdev);
	}
}

u32 kbase_csf_scheduler_get_nr_active_csgs_locked(struct kbase_device *kbdev)
{
	u32 nr_active_csgs;

	lockdep_assert_held(&kbdev->csf.scheduler.interrupt_lock);

	nr_active_csgs = bitmap_weight(kbdev->csf.scheduler.csg_inuse_bitmap,
				kbdev->csf.global_iface.group_num);

	return nr_active_csgs;
}

u32 kbase_csf_scheduler_get_nr_active_csgs(struct kbase_device *kbdev)
{
	u32 nr_active_csgs;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	nr_active_csgs = kbase_csf_scheduler_get_nr_active_csgs_locked(kbdev);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	return nr_active_csgs;
}

/**
 * csg_slot_in_use - returns true if a queue group has been programmed on a
 *                   given CSG slot.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @slot:  Index/number of the CSG slot in question.
 *
 * Return: the interface is actively engaged flag.
 *
 * Note: Caller must hold the scheduler lock.
 */
static inline bool csg_slot_in_use(struct kbase_device *kbdev, int slot)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	return (kbdev->csf.scheduler.csg_slots[slot].resident_group != NULL);
}

static bool queue_group_suspended_locked(struct kbase_queue_group *group)
{
	lockdep_assert_held(&group->kctx->kbdev->csf.scheduler.lock);

	return (group->run_state == KBASE_CSF_GROUP_SUSPENDED ||
		group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_IDLE ||
		group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC);
}

static bool queue_group_idle_locked(struct kbase_queue_group *group)
{
	lockdep_assert_held(&group->kctx->kbdev->csf.scheduler.lock);

	return (group->run_state == KBASE_CSF_GROUP_IDLE ||
		group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_IDLE);
}

static bool on_slot_group_idle_locked(struct kbase_queue_group *group)
{
	lockdep_assert_held(&group->kctx->kbdev->csf.scheduler.lock);

	return (group->run_state == KBASE_CSF_GROUP_IDLE);
}

static bool can_schedule_idle_group(struct kbase_queue_group *group)
{
	return (on_slot_group_idle_locked(group) ||
		(group->priority == KBASE_QUEUE_GROUP_PRIORITY_REALTIME));
}

static bool queue_group_scheduled(struct kbase_queue_group *group)
{
	return (group->run_state != KBASE_CSF_GROUP_INACTIVE &&
		group->run_state != KBASE_CSF_GROUP_TERMINATED &&
		group->run_state != KBASE_CSF_GROUP_FAULT_EVICTED);
}

static bool queue_group_scheduled_locked(struct kbase_queue_group *group)
{
	lockdep_assert_held(&group->kctx->kbdev->csf.scheduler.lock);

	return queue_group_scheduled(group);
}

/**
 * scheduler_protm_wait_quit() - Wait for GPU to exit protected mode.
 *
 * @kbdev: Pointer to the GPU device
 *
 * This function waits for the GPU to exit protected mode which is confirmed
 * when active_protm_grp is set to NULL.
 *
 * Return: true on success, false otherwise.
 */
static bool scheduler_protm_wait_quit(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	long wt = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	long remaining;
	bool success = true;

	lockdep_assert_held(&scheduler->lock);

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_PROTM_WAIT_QUIT_START, NULL, jiffies_to_msecs(wt));

	remaining = wait_event_timeout(kbdev->csf.event_wait,
			!kbase_csf_scheduler_protected_mode_in_use(kbdev), wt);

	if (unlikely(!remaining)) {
		struct kbase_queue_group *group = kbdev->csf.scheduler.active_protm_grp;
		struct kbase_context *kctx = group ? group->kctx : NULL;

		dev_warn(kbdev->dev, "[%llu] Timeout (%d ms), protm_quit wait skipped",
			kbase_backend_get_cycle_cnt(kbdev),
			kbdev->csf.fw_timeout_ms);
		schedule_actions_trigger_df(kbdev, kctx, DF_PROTECTED_MODE_EXIT_TIMEOUT);
		success = false;
	}

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_PROTM_WAIT_QUIT_END, NULL, jiffies_to_msecs(remaining));

	return success;
}

/**
 * scheduler_force_protm_exit() - Force GPU to exit protected mode.
 *
 * @kbdev: Pointer to the GPU device
 *
 * This function sends a ping request to the firmware and waits for the GPU
 * to exit protected mode.
 *
 * If the GPU does not exit protected mode, it is considered as hang.
 * A GPU reset would then be triggered.
 */
static void scheduler_force_protm_exit(struct kbase_device *kbdev)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	kbase_csf_firmware_ping(kbdev);

	if (scheduler_protm_wait_quit(kbdev))
		return;

	dev_err(kbdev->dev, "Possible GPU hang in Protected mode");

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	if (kbdev->csf.scheduler.active_protm_grp) {
		dev_err(kbdev->dev,
			"Group-%d of context %d_%d ran in protected mode for too long on slot %d",
			kbdev->csf.scheduler.active_protm_grp->handle,
			kbdev->csf.scheduler.active_protm_grp->kctx->tgid,
			kbdev->csf.scheduler.active_protm_grp->kctx->id,
			kbdev->csf.scheduler.active_protm_grp->csg_nr);
	}
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	/* The GPU could be stuck in Protected mode. To prevent a hang,
	 * a GPU reset is performed.
	 */
	if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
		kbase_reset_gpu(kbdev);
}

/**
 * scheduler_timer_is_enabled_nolock() - Check if the scheduler wakes up
 * automatically for periodic tasks.
 *
 * @kbdev: Pointer to the device
 *
 * This is a variant of kbase_csf_scheduler_timer_is_enabled() that assumes the
 * CSF scheduler lock to already have been held.
 *
 * Return: true if the scheduler is configured to wake up periodically
 */
static bool scheduler_timer_is_enabled_nolock(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	return kbdev->csf.scheduler.timer_enabled;
}

/**
 * scheduler_pm_active_handle_suspend() - Acquire the PM reference count for
 *                                        Scheduler
 *
 * @kbdev: Pointer to the device
 * @suspend_handler: Handler code for how to handle a suspend that might occur.
 *
 * This function is usually called when Scheduler needs to be activated.
 * The PM reference count is acquired for the Scheduler and the power on
 * of GPU is initiated.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int scheduler_pm_active_handle_suspend(struct kbase_device *kbdev,
				enum kbase_pm_suspend_handler suspend_handler)
{
	unsigned long flags;
	u32 prev_count;
	int ret = 0;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	prev_count = kbdev->csf.scheduler.pm_active_count;
	if (!WARN_ON(prev_count == U32_MAX))
		kbdev->csf.scheduler.pm_active_count++;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* On 0 => 1, make a pm_ctx_active request */
	if (!prev_count) {
		ret = kbase_pm_context_active_handle_suspend(kbdev,
							suspend_handler);
		/* Invoke the PM state machines again as the change in MCU
		 * desired status, due to the update of scheduler.pm_active_count,
		 * may be missed by the thread that called pm_wait_for_desired_state()
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		if (ret)
			kbdev->csf.scheduler.pm_active_count--;
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

	return ret;
}

#ifdef KBASE_PM_RUNTIME
/**
 * scheduler_pm_active_after_sleep() - Acquire the PM reference count for
 *                                     Scheduler
 *
 * @kbdev: Pointer to the device
 * @flags: Pointer to the flags variable containing the interrupt state
 *         when hwaccess lock was acquired.
 *
 * This function is called when Scheduler needs to be activated from the
 * sleeping state.
 * The PM reference count is acquired for the Scheduler and the wake up of
 * MCU is initiated. It resets the flag that indicates to the MCU state
 * machine that MCU needs to be put in sleep state.
 *
 * Note: This function shall be called with hwaccess lock held and it may
 * release that lock and reacquire it.
 *
 * Return: zero when the PM reference was taken and non-zero when the
 * system is being suspending/suspended.
 */
static int scheduler_pm_active_after_sleep(struct kbase_device *kbdev,
					   unsigned long *flags)
{
	u32 prev_count;
	int ret = 0;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	prev_count = kbdev->csf.scheduler.pm_active_count;
	if (!WARN_ON(prev_count == U32_MAX))
		kbdev->csf.scheduler.pm_active_count++;

	/* On 0 => 1, make a pm_ctx_active request */
	if (!prev_count) {
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, *flags);

		ret = kbase_pm_context_active_handle_suspend(kbdev,
				KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE);

		spin_lock_irqsave(&kbdev->hwaccess_lock, *flags);
		if (ret)
			kbdev->csf.scheduler.pm_active_count--;
		else
			kbdev->pm.backend.gpu_sleep_mode_active = false;
		kbase_pm_update_state(kbdev);
	}

	return ret;
}
#endif

/**
 * scheduler_pm_idle() - Release the PM reference count held by Scheduler
 *
 * @kbdev: Pointer to the device
 *
 * This function is usually called after Scheduler is suspended.
 * The PM reference count held by the Scheduler is released to trigger the
 * power down of GPU.
 */
static void scheduler_pm_idle(struct kbase_device *kbdev)
{
	unsigned long flags;
	u32 prev_count;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	prev_count = kbdev->csf.scheduler.pm_active_count;
	if (!WARN_ON(prev_count == 0))
		kbdev->csf.scheduler.pm_active_count--;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (prev_count == 1) {
		kbase_pm_context_idle(kbdev);
		/* Invoke the PM state machines again as the change in MCU
		 * desired status, due to the update of scheduler.pm_active_count,
		 * may be missed by the thread that called pm_wait_for_desired_state()
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
}

#ifdef KBASE_PM_RUNTIME
/**
 * scheduler_pm_idle_before_sleep() - Release the PM reference count and
 *                                    trigger the tranistion to sleep state.
 *
 * @kbdev: Pointer to the device
 *
 * This function is called on the GPU idle notification. It releases the
 * Scheduler's PM reference count and sets the flag to indicate to the
 * MCU state machine that MCU needs to be put in sleep state.
 */
static void scheduler_pm_idle_before_sleep(struct kbase_device *kbdev)
{
	unsigned long flags;
	u32 prev_count;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	prev_count = kbdev->csf.scheduler.pm_active_count;
	if (!WARN_ON(prev_count == 0))
		kbdev->csf.scheduler.pm_active_count--;
	kbdev->pm.backend.gpu_sleep_mode_active = true;
	kbdev->pm.backend.exit_gpu_sleep_mode = false;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (prev_count == 1) {
		kbase_pm_context_idle(kbdev);
		/* Invoke the PM state machines again as the change in MCU
		 * desired status, due to the update of scheduler.pm_active_count,
		 * may be missed by the thread that called pm_wait_for_desired_state()
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
}
#endif

static void scheduler_wakeup(struct kbase_device *kbdev, bool kick)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	int ret;

	lockdep_assert_held(&scheduler->lock);

	if ((scheduler->state != SCHED_SUSPENDED) &&
	    (scheduler->state != SCHED_SLEEPING))
		return;

	if (scheduler->state == SCHED_SUSPENDED) {
		dev_dbg(kbdev->dev,
			"Re-activating the Scheduler after suspend");
		ret = scheduler_pm_active_handle_suspend(kbdev,
				KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE);
	} else {
#ifdef KBASE_PM_RUNTIME
		unsigned long flags;

		dev_dbg(kbdev->dev,
			"Re-activating the Scheduler out of sleep");

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		ret = scheduler_pm_active_after_sleep(kbdev, &flags);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#endif
	}

	if (ret) {
		/* GPUCORE-29850 would add the handling for the case where
		 * Scheduler could not be activated due to system suspend.
		 */
		dev_info(kbdev->dev,
			"Couldn't wakeup Scheduler due to system suspend");
		return;
	}

	scheduler->state = SCHED_INACTIVE;
	KBASE_KTRACE_ADD(kbdev, SCHED_INACTIVE, NULL, scheduler->state);

	if (kick)
		scheduler_enable_tick_timer_nolock(kbdev);
}

static void scheduler_suspend(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	if (!WARN_ON(scheduler->state == SCHED_SUSPENDED)) {
		dev_dbg(kbdev->dev, "Suspending the Scheduler");
		scheduler_pm_idle(kbdev);
		scheduler->state = SCHED_SUSPENDED;
		KBASE_KTRACE_ADD(kbdev, SCHED_SUSPENDED, NULL, scheduler->state);
	}
}

/**
 * update_idle_suspended_group_state() - Move the queue group to a non-idle
 *                                       suspended state.
 * @group: Pointer to the queue group.
 *
 * This function is called to change the state of queue group to non-idle
 * suspended state, if the group was suspended when all the queues bound to it
 * became empty or when some queues got blocked on a sync wait & others became
 * empty. The group is also moved to the runnable list from idle wait list in
 * the latter case.
 * So the function gets called when a queue is kicked or sync wait condition
 * gets satisfied.
 */
static void update_idle_suspended_group_state(struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *scheduler =
		&group->kctx->kbdev->csf.scheduler;
	int new_val;

	lockdep_assert_held(&scheduler->lock);

	if (group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC) {
		remove_group_from_idle_wait(group);
		insert_group_to_runnable(scheduler, group,
					 KBASE_CSF_GROUP_SUSPENDED);
	} else if (group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_IDLE) {
		group->run_state = KBASE_CSF_GROUP_SUSPENDED;
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_SUSPENDED, group,
					 group->run_state);

		/* If scheduler is not suspended and the given group's
		 * static priority (reflected by the scan_seq_num) is inside
		 * the current tick slot-range, or there are some on_slot
		 * idle groups, schedule an async tock.
		 */
		if (scheduler->state != SCHED_SUSPENDED) {
			unsigned long flags;
			int n_idle;
			int n_used;
			int n_slots =
				group->kctx->kbdev->csf.global_iface.group_num;

			spin_lock_irqsave(&scheduler->interrupt_lock, flags);
			n_idle = bitmap_weight(scheduler->csg_slots_idle_mask,
					       n_slots);
			n_used = bitmap_weight(scheduler->csg_inuse_bitmap,
					       n_slots);
			spin_unlock_irqrestore(&scheduler->interrupt_lock,
					       flags);

			if (n_idle ||
			    n_used < scheduler->num_csg_slots_for_tick ||
			    group->scan_seq_num <
				    scheduler->num_csg_slots_for_tick)
				schedule_in_cycle(group, true);
		}
	} else
		return;

	new_val = atomic_inc_return(&scheduler->non_idle_offslot_grps);
	KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_INC, group,
				 new_val);
}

int kbase_csf_scheduler_group_get_slot_locked(struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *scheduler =
			&group->kctx->kbdev->csf.scheduler;
	int slot_num = group->csg_nr;

	lockdep_assert_held(&scheduler->interrupt_lock);

	if (slot_num >= 0) {
		if (WARN_ON(scheduler->csg_slots[slot_num].resident_group !=
			    group))
			return -1;
	}

	return slot_num;
}

int kbase_csf_scheduler_group_get_slot(struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *scheduler =
			&group->kctx->kbdev->csf.scheduler;
	unsigned long flags;
	int slot_num;

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	slot_num = kbase_csf_scheduler_group_get_slot_locked(group);
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	return slot_num;
}

/* kbasep_csf_scheduler_group_is_on_slot_locked() - Check if CSG is on slot.
 *
 * @group: GPU queue group to be checked
 *
 * This function needs to be called with scheduler's lock held
 *
 * Return: true if @group is on slot.
 */
static bool kbasep_csf_scheduler_group_is_on_slot_locked(
				struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *scheduler =
			&group->kctx->kbdev->csf.scheduler;
	int slot_num = group->csg_nr;

	lockdep_assert_held(&scheduler->lock);

	if (slot_num >= 0) {
		if (!WARN_ON(scheduler->csg_slots[slot_num].resident_group !=
			     group))
			return true;
	}

	return false;
}

bool kbase_csf_scheduler_group_events_enabled(struct kbase_device *kbdev,
			struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *scheduler =
			&group->kctx->kbdev->csf.scheduler;
	int slot_num = group->csg_nr;

	lockdep_assert_held(&scheduler->interrupt_lock);

	if (WARN_ON(slot_num < 0))
		return false;

	return test_bit(slot_num, scheduler->csgs_events_enable_mask);
}

struct kbase_queue_group *kbase_csf_scheduler_get_group_on_slot(
			struct kbase_device *kbdev, int slot)
{
	lockdep_assert_held(&kbdev->csf.scheduler.interrupt_lock);

	return kbdev->csf.scheduler.csg_slots[slot].resident_group;
}

static int halt_stream_sync(struct kbase_queue *queue)
{
	struct kbase_queue_group *group = queue->group;
	struct kbase_device *kbdev = queue->kctx->kbdev;
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	struct kbase_csf_cmd_stream_group_info *ginfo;
	struct kbase_csf_cmd_stream_info *stream;
	int csi_index = queue->csi_index;
	long remaining = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	unsigned long flags;

	if (WARN_ON(!group) ||
	    WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(group)))
		return -EINVAL;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	ginfo = &global_iface->groups[group->csg_nr];
	stream = &ginfo->streams[csi_index];

	if (CS_REQ_STATE_GET(kbase_csf_firmware_cs_input_read(stream, CS_REQ)) ==
			CS_REQ_STATE_START) {

		remaining = wait_event_timeout(kbdev->csf.event_wait,
			(CS_ACK_STATE_GET(kbase_csf_firmware_cs_output(stream, CS_ACK))
			 == CS_ACK_STATE_START), remaining);

		if (!remaining) {
			dev_warn(kbdev->dev, "[%llu] Timeout (%d ms) waiting for queue to start on csi %d bound to group %d on slot %d",
				 kbase_backend_get_cycle_cnt(kbdev), kbdev->csf.fw_timeout_ms,
				 csi_index, group->handle, group->csg_nr);
			if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
				kbase_reset_gpu(kbdev);


			return -ETIMEDOUT;
		}

		remaining =
			kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	}

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	/* Set state to STOP */
	kbase_csf_firmware_cs_input_mask(stream, CS_REQ, CS_REQ_STATE_STOP,
					 CS_REQ_STATE_MASK);

	kbase_csf_ring_cs_kernel_doorbell(kbdev, csi_index, group->csg_nr, true);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_STOP_REQ, group, queue, 0u);

	/* Timed wait */
	remaining = wait_event_timeout(kbdev->csf.event_wait,
		(CS_ACK_STATE_GET(kbase_csf_firmware_cs_output(stream, CS_ACK))
		 == CS_ACK_STATE_STOP), remaining);

	if (!remaining) {
		dev_warn(kbdev->dev, "[%llu] Timeout (%d ms) waiting for queue to stop on csi %d bound to group %d on slot %d",
			 kbase_backend_get_cycle_cnt(kbdev), kbdev->csf.fw_timeout_ms,
			 queue->csi_index, group->handle, group->csg_nr);

		/* TODO GPUCORE-25328: The CSG can't be terminated, the GPU
		 * will be reset as a work-around.
		 */
		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu(kbdev);


	}
	return (remaining) ? 0 : -ETIMEDOUT;
}

static bool can_halt_stream(struct kbase_device *kbdev,
		struct kbase_queue_group *group)
{
	struct kbase_csf_csg_slot *const csg_slot =
			kbdev->csf.scheduler.csg_slots;
	unsigned long flags;
	bool can_halt;
	int slot;

	if (!queue_group_scheduled(group))
		return true;

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	slot = kbase_csf_scheduler_group_get_slot_locked(group);
	can_halt = (slot >= 0) &&
		   (atomic_read(&csg_slot[slot].state) == CSG_SLOT_RUNNING);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock,
				flags);

	return can_halt;
}

/**
 * sched_halt_stream() - Stop a GPU queue when its queue group is not running
 *                       on a CSG slot.
 * @queue: Pointer to the GPU queue to stop.
 *
 * This function handles stopping gpu queues for groups that are either not on
 * a CSG slot or are on the slot but undergoing transition to
 * resume or suspend states.
 * It waits until the queue group is scheduled on a slot and starts running,
 * which is needed as groups that were suspended may need to resume all queues
 * that were enabled and running at the time of suspension.
 *
 * Return: 0 on success, or negative on failure.
 */
static int sched_halt_stream(struct kbase_queue *queue)
{
	struct kbase_queue_group *group = queue->group;
	struct kbase_device *kbdev = queue->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler =
			&kbdev->csf.scheduler;
	struct kbase_csf_csg_slot *const csg_slot =
			kbdev->csf.scheduler.csg_slots;
	bool retry_needed = false;
	bool retried = false;
	long remaining;
	int slot;
	int err = 0;
	const u32 group_schedule_timeout = kbase_get_timeout_ms(kbdev, CSF_CSG_SUSPEND_TIMEOUT);

	if (WARN_ON(!group))
		return -EINVAL;

	lockdep_assert_held(&queue->kctx->csf.lock);
	lockdep_assert_held(&scheduler->lock);

	slot = kbase_csf_scheduler_group_get_slot(group);

	if (slot >= 0) {
		WARN_ON(atomic_read(&csg_slot[slot].state) == CSG_SLOT_RUNNING);

		if (atomic_read(&csg_slot[slot].state) == CSG_SLOT_READY2RUN) {
			dev_dbg(kbdev->dev, "Stopping a queue on csi %d when Group-%d is in under transition to running state",
				queue->csi_index, group->handle);
			retry_needed = true;
		}
	}
retry:
	/* Update the group state so that it can get scheduled soon */
	update_idle_suspended_group_state(group);

	mutex_unlock(&scheduler->lock);

	/* This function is called when the queue group is either not on a CSG
	 * slot or is on the slot but undergoing transition.
	 *
	 * To stop the queue, the function needs to wait either for the queue
	 * group to be assigned a CSG slot (and that slot has to reach the
	 * running state) or for the eviction of the queue group from the
	 * scheduler's list.
	 *
	 * In order to evaluate the latter condition, the function doesn't
	 * really need to lock the scheduler, as any update to the run_state
	 * of the queue group by sched_evict_group() would be visible due
	 * to implicit barriers provided by the kernel waitqueue macros.
	 *
	 * The group pointer cannot disappear meanwhile, as the high level
	 * CSF context is locked. Therefore, the scheduler would be
	 * the only one to update the run_state of the group.
	 */
	remaining = wait_event_timeout(
		kbdev->csf.event_wait, can_halt_stream(kbdev, group),
		kbase_csf_timeout_in_jiffies(group_schedule_timeout));

	mutex_lock(&scheduler->lock);

	if (remaining && queue_group_scheduled_locked(group)) {
		slot = kbase_csf_scheduler_group_get_slot(group);

		/* If the group is still on slot and slot is in running state
		 * then explicitly stop the CSI of the
		 * queue. Otherwise there are different cases to consider
		 *
		 * - If the queue group was already undergoing transition to
		 *   resume/start state when this function was entered then it
		 *   would not have disabled the CSI of the
		 *   queue being stopped and the previous wait would have ended
		 *   once the slot was in a running state with CS
		 *   interface still enabled.
		 *   Now the group is going through another transition either
		 *   to a suspend state or to a resume state (it could have
		 *   been suspended before the scheduler lock was grabbed).
		 *   In both scenarios need to wait again for the group to
		 *   come on a slot and that slot to reach the running state,
		 *   as that would guarantee that firmware will observe the
		 *   CSI as disabled.
		 *
		 * - If the queue group was either off the slot or was
		 *   undergoing transition to suspend state on entering this
		 *   function, then the group would have been resumed with the
		 *   queue's CSI in disabled state.
		 *   So now if the group is undergoing another transition
		 *   (after the resume) then just need to wait for the state
		 *   bits in the ACK register of CSI to be
		 *   set to STOP value. It is expected that firmware will
		 *   process the stop/disable request of the CS
		 *   interface after resuming the group before it processes
		 *   another state change request of the group.
		 */
		if ((slot >= 0) &&
		    (atomic_read(&csg_slot[slot].state) == CSG_SLOT_RUNNING)) {
			err = halt_stream_sync(queue);
		} else if (retry_needed && !retried) {
			retried = true;
			goto retry;
		} else if (slot >= 0) {
			struct kbase_csf_global_iface *global_iface =
					&kbdev->csf.global_iface;
			struct kbase_csf_cmd_stream_group_info *ginfo =
					&global_iface->groups[slot];
			struct kbase_csf_cmd_stream_info *stream =
					&ginfo->streams[queue->csi_index];
			u32 cs_req =
			    kbase_csf_firmware_cs_input_read(stream, CS_REQ);

			if (!WARN_ON(CS_REQ_STATE_GET(cs_req) !=
				     CS_REQ_STATE_STOP)) {
				/* Timed wait */
				remaining = wait_event_timeout(
					kbdev->csf.event_wait,
					(CS_ACK_STATE_GET(
						 kbase_csf_firmware_cs_output(
							 stream, CS_ACK)) ==
					 CS_ACK_STATE_STOP),
					kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms));

				if (!remaining) {
					dev_warn(kbdev->dev,
						 "[%llu] Timeout (%d ms) waiting for queue stop ack on csi %d bound to group %d on slot %d",
						 kbase_backend_get_cycle_cnt(kbdev), kbdev->csf.fw_timeout_ms,
						 queue->csi_index,
						 group->handle, group->csg_nr);


					err = -ETIMEDOUT;
				}
			}
		}
	} else if (!remaining) {
		dev_warn(kbdev->dev, "[%llu] Group-%d failed to get a slot for stopping the queue on csi %d (timeout %d ms)",
			 kbase_backend_get_cycle_cnt(kbdev),
			 group->handle, queue->csi_index,
			 group_schedule_timeout);


		err = -ETIMEDOUT;
	}

	return err;
}

/**
 * scheduler_activate_on_queue_stop() - Activate the Scheduler when the GPU
 *                                      queue needs to be stopped.
 *
 * @queue: Pointer the GPU command queue
 *
 * This function is called when the CSI to which GPU queue is bound needs to
 * be stopped. For that the corresponding queue group needs to be resident on
 * the CSG slot and MCU firmware should be running. So this function makes the
 * Scheduler exit the sleeping or suspended state.
 */
static void scheduler_activate_on_queue_stop(struct kbase_queue *queue)
{
	struct kbase_device *kbdev = queue->kctx->kbdev;

	scheduler_wakeup(kbdev, true);

	/* Wait for MCU firmware to start running */
	if (kbase_csf_scheduler_wait_mcu_active(kbdev)) {
		dev_warn(
			kbdev->dev,
			"[%llu] Wait for MCU active failed for stopping queue on csi %d bound to group %d of context %d_%d on slot %d",
			kbase_backend_get_cycle_cnt(kbdev),
			queue->csi_index, queue->group->handle,
			queue->kctx->tgid, queue->kctx->id,
			queue->group->csg_nr);
	}
}

int kbase_csf_scheduler_queue_stop(struct kbase_queue *queue)
{
	struct kbase_device *kbdev = queue->kctx->kbdev;
	struct kbase_queue_group *group = queue->group;
	bool const cs_enabled = queue->enabled;
	int err = 0;

	if (WARN_ON(!group))
		return -EINVAL;

	kbase_reset_gpu_assert_failed_or_prevented(kbdev);
	lockdep_assert_held(&queue->kctx->csf.lock);
	mutex_lock(&kbdev->csf.scheduler.lock);

	queue->enabled = false;
	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_STOP, group, queue, cs_enabled);

	if (cs_enabled && queue_group_scheduled_locked(group)) {
		struct kbase_csf_csg_slot *const csg_slot =
			kbdev->csf.scheduler.csg_slots;
		int slot = kbase_csf_scheduler_group_get_slot(group);

		/* Since the group needs to be resumed in order to stop the queue,
		 * check if GPU needs to be powered up.
		 */
		scheduler_activate_on_queue_stop(queue);

		if ((slot >= 0) &&
		    (atomic_read(&csg_slot[slot].state) == CSG_SLOT_RUNNING))
			err = halt_stream_sync(queue);
		else
			err = sched_halt_stream(queue);

		unassign_user_doorbell_from_queue(kbdev, queue);
	}

	mutex_unlock(&kbdev->csf.scheduler.lock);
	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_STOP, group, queue, group->run_state);
	return err;
}

static void update_hw_active(struct kbase_queue *queue, bool active)
{
#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	if (queue && queue->enabled) {
		u32 *output_addr = (u32 *)(queue->user_io_addr + PAGE_SIZE);

		output_addr[CS_ACTIVE / sizeof(u32)] = active;
	}
#else
	CSTD_UNUSED(queue);
	CSTD_UNUSED(active);
#endif
}

static void program_cs_extract_init(struct kbase_queue *queue)
{
	u64 *input_addr = (u64 *)queue->user_io_addr;
	u64 *output_addr = (u64 *)(queue->user_io_addr + PAGE_SIZE);

	input_addr[CS_EXTRACT_INIT_LO / sizeof(u64)] =
			output_addr[CS_EXTRACT_LO / sizeof(u64)];
}

static void program_cs_trace_cfg(struct kbase_csf_cmd_stream_info *stream,
				 struct kbase_queue *queue)
{
	struct kbase_device *kbdev = queue->kctx->kbdev;
	u32 const glb_version = kbdev->csf.global_iface.version;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	/* If cs_trace_command not supported, nothing to program */
	if (glb_version < kbase_csf_interface_version(1, 1, 0))
		return;

	/* Program for cs_trace if enabled. In the current arrangement, it is
	 * possible for the context to enable the cs_trace after some queues
	 * has been registered in cs_trace in disabled state. This is tracked by
	 * the queue's trace buffer base address, which had been validated at the
	 * queue's register_ex call.
	 */
	if (kbase_csf_scheduler_queue_has_trace(queue)) {
		u32 cs_cfg = CS_INSTR_CONFIG_JASID_SET(
			queue->trace_cfg, queue->kctx->as_nr);

		kbase_csf_firmware_cs_input(stream, CS_INSTR_CONFIG, cs_cfg);
		kbase_csf_firmware_cs_input(stream, CS_INSTR_BUFFER_SIZE,
				queue->trace_buffer_size);

		kbase_csf_firmware_cs_input(stream, CS_INSTR_BUFFER_BASE_LO,
				queue->trace_buffer_base & U32_MAX);
		kbase_csf_firmware_cs_input(stream, CS_INSTR_BUFFER_BASE_HI,
				queue->trace_buffer_base >> 32);

		kbase_csf_firmware_cs_input(
				stream, CS_INSTR_BUFFER_OFFSET_POINTER_LO,
				queue->trace_offset_ptr & U32_MAX);
		kbase_csf_firmware_cs_input(
				stream, CS_INSTR_BUFFER_OFFSET_POINTER_HI,
				queue->trace_offset_ptr >> 32);
	} else {
		/* Place the configuration to the disabled condition */
		kbase_csf_firmware_cs_input(stream, CS_INSTR_CONFIG, 0);
		kbase_csf_firmware_cs_input(stream, CS_INSTR_BUFFER_SIZE, 0);
	}
}

static void program_cs(struct kbase_device *kbdev,
		struct kbase_queue *queue, bool ring_csg_doorbell)
{
	struct kbase_queue_group *group = queue->group;
	struct kbase_csf_cmd_stream_group_info *ginfo;
	struct kbase_csf_cmd_stream_info *stream;
	int csi_index = queue->csi_index;
	unsigned long flags;
	u64 user_input;
	u64 user_output;

	if (WARN_ON(!group))
		return;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(group)))
		return;

	ginfo = &kbdev->csf.global_iface.groups[group->csg_nr];

	if (WARN_ON(csi_index < 0) ||
	    WARN_ON(csi_index >= ginfo->stream_num))
		return;

	assign_user_doorbell_to_queue(kbdev, queue);
	if (queue->doorbell_nr == KBASEP_USER_DB_NR_INVALID)
		return;

	WARN_ON(queue->doorbell_nr != queue->group->doorbell_nr);

	if (queue->enabled && queue_group_suspended_locked(group))
		program_cs_extract_init(queue);

	stream = &ginfo->streams[csi_index];

	kbase_csf_firmware_cs_input(stream, CS_BASE_LO,
				    queue->base_addr & 0xFFFFFFFF);
	kbase_csf_firmware_cs_input(stream, CS_BASE_HI,
				    queue->base_addr >> 32);
	kbase_csf_firmware_cs_input(stream, CS_SIZE,
				    queue->size);

	user_input = (queue->reg->start_pfn << PAGE_SHIFT);
	kbase_csf_firmware_cs_input(stream, CS_USER_INPUT_LO,
				    user_input & 0xFFFFFFFF);
	kbase_csf_firmware_cs_input(stream, CS_USER_INPUT_HI,
				    user_input >> 32);

	user_output = ((queue->reg->start_pfn + 1) << PAGE_SHIFT);
	kbase_csf_firmware_cs_input(stream, CS_USER_OUTPUT_LO,
				    user_output & 0xFFFFFFFF);
	kbase_csf_firmware_cs_input(stream, CS_USER_OUTPUT_HI,
				    user_output >> 32);

	kbase_csf_firmware_cs_input(stream, CS_CONFIG,
		(queue->doorbell_nr << 8) | (queue->priority & 0xF));

	/* Program the queue's cs_trace configuration */
	program_cs_trace_cfg(stream, queue);

	/* Enable all interrupts for now */
	kbase_csf_firmware_cs_input(stream, CS_ACK_IRQ_MASK, ~((u32)0));

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);

	/* The fault bit could be misaligned between CS_REQ and CS_ACK if the
	 * acknowledgment was deferred due to dump on fault and the group was
	 * removed from the CSG slot before the fault could be acknowledged.
	 */
	if (queue->enabled) {
		u32 const cs_ack =
			kbase_csf_firmware_cs_output(stream, CS_ACK);

		kbase_csf_firmware_cs_input_mask(stream, CS_REQ, cs_ack,
						 CS_REQ_FAULT_MASK);
	}

	/*
	 * Enable the CSG idle notification once the CS's ringbuffer
	 * becomes empty or the CS becomes sync_idle, waiting sync update
	 * or protected mode switch.
	 */
	kbase_csf_firmware_cs_input_mask(stream, CS_REQ,
			CS_REQ_IDLE_EMPTY_MASK | CS_REQ_IDLE_SYNC_WAIT_MASK,
			CS_REQ_IDLE_EMPTY_MASK | CS_REQ_IDLE_SYNC_WAIT_MASK);

	/* Set state to START/STOP */
	kbase_csf_firmware_cs_input_mask(stream, CS_REQ,
		queue->enabled ? CS_REQ_STATE_START : CS_REQ_STATE_STOP,
		CS_REQ_STATE_MASK);
	kbase_csf_ring_cs_kernel_doorbell(kbdev, csi_index, group->csg_nr,
					  ring_csg_doorbell);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_START, group, queue, queue->enabled);

	update_hw_active(queue, true);
}

int kbase_csf_scheduler_queue_start(struct kbase_queue *queue)
{
	struct kbase_queue_group *group = queue->group;
	struct kbase_device *kbdev = queue->kctx->kbdev;
	bool const cs_enabled = queue->enabled;
	int err = 0;
	bool evicted = false;

	kbase_reset_gpu_assert_prevented(kbdev);
	lockdep_assert_held(&queue->kctx->csf.lock);

	if (WARN_ON(!group || queue->bind_state != KBASE_CSF_QUEUE_BOUND))
		return -EINVAL;

	mutex_lock(&kbdev->csf.scheduler.lock);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(kbdev->csf.scheduler.state == SCHED_BUSY)) {
		mutex_unlock(&kbdev->csf.scheduler.lock);
		return -EBUSY;
	}
#endif

	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_START, group, queue,
				   group->run_state);
	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_WAIT_STATUS, queue->group, queue,
				   queue->status_wait);

	if (group->run_state == KBASE_CSF_GROUP_FAULT_EVICTED) {
		err = -EIO;
		evicted = true;
	} else if ((group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC)
		   && CS_STATUS_WAIT_SYNC_WAIT_GET(queue->status_wait)) {
		dev_dbg(kbdev->dev, "blocked queue(csi_index=%d) of group %d was kicked",
			queue->csi_index, group->handle);
	} else {
		err = scheduler_group_schedule(group);

		if (!err) {
			queue->enabled = true;
			if (kbasep_csf_scheduler_group_is_on_slot_locked(group)) {
				if (cs_enabled) {
					/* In normal situation, when a queue is
					 * already running, the queue update
					 * would be a doorbell kick on user
					 * side. However, if such a kick is
					 * shortly following a start or resume,
					 * the queue may actually in transition
					 * hence the said kick would enter the
					 * kernel as the hw_active flag is yet
					 * to be set. The sheduler needs to
					 * give a kick to the corresponding
					 * user door-bell on such a case.
					 */
					kbase_csf_ring_cs_user_doorbell(kbdev, queue);
				} else
					program_cs(kbdev, queue, true);
			}
			queue_delayed_work(system_long_wq, &kbdev->csf.scheduler.ping_work,
					   msecs_to_jiffies(kbase_get_timeout_ms(
						   kbdev, CSF_FIRMWARE_PING_TIMEOUT)));
		}
	}

	mutex_unlock(&kbdev->csf.scheduler.lock);

	if (evicted)
		kbase_csf_term_descheduled_queue_group(group);

	return err;
}

static enum kbase_csf_csg_slot_state update_csg_slot_status(
				struct kbase_device *kbdev, s8 slot)
{
	struct kbase_csf_csg_slot *csg_slot =
		&kbdev->csf.scheduler.csg_slots[slot];
	struct kbase_csf_cmd_stream_group_info *ginfo =
		&kbdev->csf.global_iface.groups[slot];
	u32 state;
	enum kbase_csf_csg_slot_state slot_state;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	state = CSG_ACK_STATE_GET(kbase_csf_firmware_csg_output(ginfo,
			CSG_ACK));
	slot_state = atomic_read(&csg_slot->state);

	switch (slot_state) {
	case CSG_SLOT_READY2RUN:
		if ((state == CSG_ACK_STATE_START) ||
		    (state == CSG_ACK_STATE_RESUME)) {
			slot_state = CSG_SLOT_RUNNING;
			atomic_set(&csg_slot->state, slot_state);
			csg_slot->trigger_jiffies = jiffies;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_RUNNING, csg_slot->resident_group,
						 state);
			dev_dbg(kbdev->dev, "Group %u running on slot %d\n",
				csg_slot->resident_group->handle, slot);
		}
		break;
	case CSG_SLOT_DOWN2STOP:
		if ((state == CSG_ACK_STATE_SUSPEND) ||
		    (state == CSG_ACK_STATE_TERMINATE)) {
			slot_state = CSG_SLOT_STOPPED;
			atomic_set(&csg_slot->state, slot_state);
			csg_slot->trigger_jiffies = jiffies;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_STOPPED, csg_slot->resident_group, state);
			dev_dbg(kbdev->dev, "Group %u stopped on slot %d\n",
				csg_slot->resident_group->handle, slot);
		}
		break;
	case CSG_SLOT_DOWN2STOP_TIMEDOUT:
	case CSG_SLOT_READY2RUN_TIMEDOUT:
	case CSG_SLOT_READY:
	case CSG_SLOT_RUNNING:
	case CSG_SLOT_STOPPED:
		break;
	default:
		dev_warn(kbdev->dev, "Unknown CSG slot state %d", slot_state);
		break;
	}

	return slot_state;
}

static bool csg_slot_running(struct kbase_device *kbdev, s8 slot)
{
	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	return (update_csg_slot_status(kbdev, slot) == CSG_SLOT_RUNNING);
}

static bool csg_slot_stopped_locked(struct kbase_device *kbdev, s8 slot)
{
	enum kbase_csf_csg_slot_state slot_state;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	slot_state = update_csg_slot_status(kbdev, slot);

	return (slot_state == CSG_SLOT_STOPPED ||
		slot_state == CSG_SLOT_READY);
}

static bool csg_slot_stopped_raw(struct kbase_device *kbdev, s8 slot)
{
	struct kbase_csf_cmd_stream_group_info *ginfo =
		&kbdev->csf.global_iface.groups[slot];
	u32 state;

	state = CSG_ACK_STATE_GET(kbase_csf_firmware_csg_output(ginfo,
			CSG_ACK));

	if (state == CSG_ACK_STATE_SUSPEND || state == CSG_ACK_STATE_TERMINATE) {
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_STOPPED, kbdev->csf.scheduler.csg_slots[slot].resident_group, state);
		dev_dbg(kbdev->dev, "(raw status) slot %d stopped\n", slot);
		return true;
	}

	return false;
}

static void halt_csg_slot(struct kbase_queue_group *group, bool suspend)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	struct kbase_csf_csg_slot *csg_slot =
		kbdev->csf.scheduler.csg_slots;
	s8 slot;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(group)))
		return;

	slot = group->csg_nr;

	/* When in transition, wait for it to complete */
	if (atomic_read(&csg_slot[slot].state) == CSG_SLOT_READY2RUN) {
		long remaining =
			kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);

		dev_dbg(kbdev->dev, "slot %d wait for up-running\n", slot);
		remaining = wait_event_timeout(kbdev->csf.event_wait,
				csg_slot_running(kbdev, slot), remaining);
		if (!remaining)
			dev_warn(kbdev->dev,
				 "[%llu] slot %d timeout (%d ms) on up-running\n",
				 kbase_backend_get_cycle_cnt(kbdev),
				 slot, kbdev->csf.fw_timeout_ms);
	}

	if (csg_slot_running(kbdev, slot)) {
		unsigned long flags;
		struct kbase_csf_cmd_stream_group_info *ginfo =
						&global_iface->groups[slot];
		u32 halt_cmd = suspend ? CSG_REQ_STATE_SUSPEND :
					 CSG_REQ_STATE_TERMINATE;

		dev_dbg(kbdev->dev, "Halting(suspend=%d) group %d of context %d_%d on slot %d",
			suspend, group->handle, group->kctx->tgid, group->kctx->id, slot);

		spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
		/* Set state to SUSPEND/TERMINATE */
		kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ, halt_cmd,
						  CSG_REQ_STATE_MASK);
		kbase_csf_ring_csg_doorbell(kbdev, slot);
		spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock,
					flags);
		atomic_set(&csg_slot[slot].state, CSG_SLOT_DOWN2STOP);
		csg_slot[slot].trigger_jiffies = jiffies;
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_STOP_REQ, group, halt_cmd);

		KBASE_TLSTREAM_TL_KBASE_DEVICE_HALT_CSG(
			kbdev, kbdev->gpu_props.props.raw_props.gpu_id, slot);
	}
}

static void term_csg_slot(struct kbase_queue_group *group)
{
	halt_csg_slot(group, false);
}

static void suspend_csg_slot(struct kbase_queue_group *group)
{
	halt_csg_slot(group, true);
}

static bool csf_wait_ge_condition_supported(struct kbase_device *kbdev)
{
	const uint32_t glb_major = GLB_VERSION_MAJOR_GET(kbdev->csf.global_iface.version);
	const uint32_t glb_minor = GLB_VERSION_MINOR_GET(kbdev->csf.global_iface.version);

	switch (glb_major) {
	case 0:
		break;
	case 1:
		if (glb_minor >= 4)
			return true;
		break;
	case 2:
		if (glb_minor >= 6)
			return true;
		break;
	case 3:
		if (glb_minor >= 6)
			return true;
		break;
	default:
		return true;
	}
	return false;
}
/**
 * evaluate_sync_update() - Evaluate the sync wait condition the GPU command
 *                          queue has been blocked on.
 *
 * @queue: Pointer to the GPU command queue
 *
 * Return: true if sync wait condition is satisfied.
 */
static bool evaluate_sync_update(struct kbase_queue *queue)
{
	struct kbase_vmap_struct *mapping;
	bool updated = false;
	u32 *sync_ptr;
	u32 sync_wait_cond;
	u32 sync_current_val;
	struct kbase_device *kbdev;
	bool sync_wait_cond_valid = false;

	if (WARN_ON(!queue))
		return false;

	kbdev = queue->kctx->kbdev;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	sync_ptr = kbase_phy_alloc_mapping_get(queue->kctx, queue->sync_ptr,
					&mapping);

	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_EVAL_START, queue->group, queue,
				   queue->sync_ptr);
	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_BLOCKED_REASON, queue->group, queue,
				   queue->blocked_reason);

	if (!sync_ptr) {
		dev_dbg(queue->kctx->kbdev->dev, "sync memory VA 0x%016llX already freed",
			queue->sync_ptr);
		goto out;
	}

	sync_wait_cond =
		CS_STATUS_WAIT_SYNC_WAIT_CONDITION_GET(queue->status_wait);
	sync_wait_cond_valid = (sync_wait_cond == CS_STATUS_WAIT_SYNC_WAIT_CONDITION_GT) ||
			       (sync_wait_cond == CS_STATUS_WAIT_SYNC_WAIT_CONDITION_LE) ||
			       ((sync_wait_cond == CS_STATUS_WAIT_SYNC_WAIT_CONDITION_GE) &&
				csf_wait_ge_condition_supported(kbdev));

	WARN_ON(!sync_wait_cond_valid);

	sync_current_val = READ_ONCE(*sync_ptr);
	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_CUR_VAL, queue->group, queue,
				   sync_current_val);

	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_TEST_VAL, queue->group, queue,
				   queue->sync_value);

	if (((sync_wait_cond == CS_STATUS_WAIT_SYNC_WAIT_CONDITION_GT) &&
	     (sync_current_val > queue->sync_value)) ||
	    ((sync_wait_cond == CS_STATUS_WAIT_SYNC_WAIT_CONDITION_GE) &&
	     (sync_current_val >= queue->sync_value) && csf_wait_ge_condition_supported(kbdev)) ||
	    ((sync_wait_cond == CS_STATUS_WAIT_SYNC_WAIT_CONDITION_LE) &&
	     (sync_current_val <= queue->sync_value))) {
		/* The sync wait condition is satisfied so the group to which
		 * queue is bound can be re-scheduled.
		 */
		updated = true;
	} else {
		dev_dbg(queue->kctx->kbdev->dev,
			"sync memory not updated yet(%u)", sync_current_val);
	}

	kbase_phy_alloc_mapping_put(queue->kctx, mapping);
out:
	KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_EVAL_END, queue->group, queue, updated);
	return updated;
}

/**
 * save_slot_cs() -  Save the state for blocked GPU command queue.
 *
 * @ginfo: Pointer to the CSG interface used by the group
 *         the queue is bound to.
 * @queue: Pointer to the GPU command queue.
 *
 * This function will check if GPU command queue is blocked on a sync wait and
 * evaluate the wait condition. If the wait condition isn't satisfied it would
 * save the state needed to reevaluate the condition in future.
 * The group to which queue is bound shall be in idle state.
 *
 * Return: true if the queue is blocked on a sync wait operation.
 */
static
bool save_slot_cs(struct kbase_csf_cmd_stream_group_info const *const ginfo,
		struct kbase_queue *queue)
{
	struct kbase_csf_cmd_stream_info *const stream =
		&ginfo->streams[queue->csi_index];
	u32 status = kbase_csf_firmware_cs_output(stream, CS_STATUS_WAIT);
	bool is_waiting = false;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	u64 cmd_ptr = kbase_csf_firmware_cs_output(stream, CS_STATUS_CMD_PTR_LO);

	cmd_ptr |= (u64)kbase_csf_firmware_cs_output(stream, CS_STATUS_CMD_PTR_HI) << 32;
	queue->saved_cmd_ptr = cmd_ptr;
#endif

	KBASE_KTRACE_ADD_CSF_GRP_Q(stream->kbdev, QUEUE_SYNC_UPDATE_WAIT_STATUS, queue->group,
				   queue, status);

	if (CS_STATUS_WAIT_SYNC_WAIT_GET(status)) {
		queue->status_wait = status;
		queue->sync_ptr = kbase_csf_firmware_cs_output(stream,
			CS_STATUS_WAIT_SYNC_POINTER_LO);
		queue->sync_ptr |= (u64)kbase_csf_firmware_cs_output(stream,
			CS_STATUS_WAIT_SYNC_POINTER_HI) << 32;
		queue->sync_value = kbase_csf_firmware_cs_output(stream,
			CS_STATUS_WAIT_SYNC_VALUE);

		queue->sb_status = CS_STATUS_SCOREBOARDS_NONZERO_GET(
			kbase_csf_firmware_cs_output(stream,
						     CS_STATUS_SCOREBOARDS));
		queue->blocked_reason = CS_STATUS_BLOCKED_REASON_REASON_GET(
			kbase_csf_firmware_cs_output(stream,
						     CS_STATUS_BLOCKED_REASON));

		if (!evaluate_sync_update(queue)) {
			is_waiting = true;
		} else {
			/* Sync object already got updated & met the condition
			 * thus it doesn't need to be reevaluated and so can
			 * clear the 'status_wait' here.
			 */
			queue->status_wait = 0;
		}
	} else {
		/* Invalidate wait status info that would have been recorded if
		 * this queue was blocked when the group (in idle state) was
		 * suspended previously. After that the group could have been
		 * unblocked due to the kicking of another queue bound to it &
		 * so the wait status info would have stuck with this queue.
		 */
		queue->status_wait = 0;
	}

	return is_waiting;
}

static void schedule_in_cycle(struct kbase_queue_group *group, bool force)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	/* Only try to schedule work for this event if no requests are pending,
	 * otherwise the function will end up canceling previous work requests,
	 * and scheduler is configured to wake up periodically (or the schedule
	 * of work needs to be enforced in situation such as entering into
	 * protected mode).
	 */
	if (likely(scheduler_timer_is_enabled_nolock(kbdev)) || force) {
		dev_dbg(kbdev->dev, "Kicking async for group %d\n",
			group->handle);
		kbase_csf_scheduler_invoke_tock(kbdev);
	}
}

static void ktrace_log_group_state(struct kbase_queue_group *const group)
{
	switch (group->run_state) {
	case KBASE_CSF_GROUP_INACTIVE:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_INACTIVE, group,
					group->run_state);
		break;
	case KBASE_CSF_GROUP_RUNNABLE:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_RUNNABLE, group,
					group->run_state);
		break;
	case KBASE_CSF_GROUP_IDLE:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_IDLE, group,
					group->run_state);
		break;
	case KBASE_CSF_GROUP_SUSPENDED:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_SUSPENDED, group,
					group->run_state);
		break;
	case KBASE_CSF_GROUP_SUSPENDED_ON_IDLE:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_SUSPENDED_ON_IDLE, group,
					group->run_state);
		break;
	case KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_SUSPENDED_ON_WAIT_SYNC,
					group, group->run_state);
		break;
	case KBASE_CSF_GROUP_FAULT_EVICTED:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_FAULT_EVICTED, group,
					group->run_state);
		break;
	case KBASE_CSF_GROUP_TERMINATED:
		KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, CSF_GROUP_TERMINATED, group,
					group->run_state);
		break;
	}
}

static
void insert_group_to_runnable(struct kbase_csf_scheduler *const scheduler,
		struct kbase_queue_group *const group,
		enum kbase_csf_group_state run_state)
{
	struct kbase_context *const kctx = group->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;

	lockdep_assert_held(&scheduler->lock);

	WARN_ON(group->run_state != KBASE_CSF_GROUP_INACTIVE);

	if (WARN_ON(group->priority >= KBASE_QUEUE_GROUP_PRIORITY_COUNT))
		return;

	group->run_state = run_state;

	ktrace_log_group_state(group);

	if (run_state == KBASE_CSF_GROUP_RUNNABLE)
		group->prepared_seq_num = KBASEP_GROUP_PREPARED_SEQ_NUM_INVALID;

	list_add_tail(&group->link,
			&kctx->csf.sched.runnable_groups[group->priority]);
	kctx->csf.sched.num_runnable_grps++;
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_RUNNABLE_INSERT, group,
				 kctx->csf.sched.num_runnable_grps);

	/* Add the kctx if not yet in runnable kctxs */
	if (kctx->csf.sched.num_runnable_grps == 1) {
		/* First runnable csg, adds to the runnable_kctxs */
		INIT_LIST_HEAD(&kctx->csf.link);
		list_add_tail(&kctx->csf.link, &scheduler->runnable_kctxs);
		KBASE_KTRACE_ADD(kbdev, SCHEDULER_RUNNABLE_KCTX_INSERT, kctx, 0u);
	}

	scheduler->total_runnable_grps++;

	if (likely(scheduler_timer_is_enabled_nolock(kbdev)) &&
	    (scheduler->total_runnable_grps == 1 ||
	     scheduler->state == SCHED_SUSPENDED ||
	     scheduler->state == SCHED_SLEEPING)) {
		dev_dbg(kbdev->dev, "Kicking scheduler on first runnable group\n");
		/* Fire a scheduling to start the time-slice */
		enqueue_tick_work(kbdev);
	} else
		schedule_in_cycle(group, false);

	/* Since a new group has become runnable, check if GPU needs to be
	 * powered up.
	 */
	scheduler_wakeup(kbdev, false);
}

static
void remove_group_from_runnable(struct kbase_csf_scheduler *const scheduler,
		struct kbase_queue_group *group,
		enum kbase_csf_group_state run_state)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_queue_group *new_head_grp;
	struct list_head *list =
		&kctx->csf.sched.runnable_groups[group->priority];
	unsigned long flags;

	lockdep_assert_held(&scheduler->lock);

	WARN_ON(!queue_group_scheduled_locked(group));

	group->run_state = run_state;

	ktrace_log_group_state(group);

	list_del_init(&group->link);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	/* The below condition will be true when the group running in protected
	 * mode is being terminated but the protected mode exit interrupt was't
	 * received. This can happen if the FW got stuck during protected mode
	 * for some reason (like GPU page fault or some internal error).
	 * In normal cases FW is expected to send the protected mode exit
	 * interrupt before it handles the CSG termination request.
	 */
	if (unlikely(scheduler->active_protm_grp == group)) {
		/* CSG slot cleanup should have happened for the pmode group */
		WARN_ON(kbasep_csf_scheduler_group_is_on_slot_locked(group));
		WARN_ON(group->run_state != KBASE_CSF_GROUP_INACTIVE);
		/* Initiate a GPU reset, in case it wasn't initiated yet,
		 * in order to rectify the anomaly.
		 */
		if (kbase_prepare_to_reset_gpu(kctx->kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu(kctx->kbdev);

		KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, SCHEDULER_PROTM_EXIT,
					 scheduler->active_protm_grp, 0u);
		scheduler->active_protm_grp = NULL;
	}
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	if (scheduler->top_grp == group) {
		/*
		 * Note: this disables explicit rotation in the next scheduling
		 * cycle. However, removing the top_grp is the same as an
		 * implicit rotation (e.g. if we instead rotated the top_ctx
		 * and then remove top_grp)
		 *
		 * This implicit rotation is assumed by the scheduler rotate
		 * functions.
		 */
		scheduler->top_grp = NULL;

		/*
		 * Trigger a scheduling tock for a CSG containing protected
		 * content in case there has been any in order to minimise
		 * latency.
		 */
		group = scheduler_get_protm_enter_async_group(kctx->kbdev,
							      NULL);
		if (group)
			schedule_in_cycle(group, true);
	}

	kctx->csf.sched.num_runnable_grps--;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, GROUP_RUNNABLE_REMOVE, group,
				 kctx->csf.sched.num_runnable_grps);
	new_head_grp = (!list_empty(list)) ?
				list_first_entry(list, struct kbase_queue_group, link) :
				NULL;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, GROUP_RUNNABLE_HEAD, new_head_grp, 0u);

	if (kctx->csf.sched.num_runnable_grps == 0) {
		struct kbase_context *new_head_kctx;
		struct list_head *kctx_list = &scheduler->runnable_kctxs;
		/* drop the kctx */
		list_del_init(&kctx->csf.link);
		if (scheduler->top_ctx == kctx)
			scheduler->top_ctx = NULL;
		KBASE_KTRACE_ADD(kctx->kbdev, SCHEDULER_RUNNABLE_KCTX_REMOVE, kctx, 0u);
		new_head_kctx = (!list_empty(kctx_list)) ?
					list_first_entry(kctx_list, struct kbase_context, csf.link) :
					NULL;
		KBASE_KTRACE_ADD(kctx->kbdev, SCHEDULER_RUNNABLE_KCTX_HEAD, new_head_kctx, 0u);
	}

	WARN_ON(scheduler->total_runnable_grps == 0);
	scheduler->total_runnable_grps--;
	if (!scheduler->total_runnable_grps) {
		dev_dbg(kctx->kbdev->dev, "Scheduler idle has no runnable groups");
		cancel_tick_timer(kctx->kbdev);
		WARN_ON(atomic_read(&scheduler->non_idle_offslot_grps));
		if (scheduler->state != SCHED_SUSPENDED)
			enqueue_gpu_idle_work(scheduler);
	}
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, SCHEDULER_TOP_GRP, scheduler->top_grp,
			scheduler->num_active_address_spaces |
			(((u64)scheduler->total_runnable_grps) << 32));
}

static void insert_group_to_idle_wait(struct kbase_queue_group *const group)
{
	struct kbase_context *kctx = group->kctx;

	lockdep_assert_held(&kctx->kbdev->csf.scheduler.lock);

	WARN_ON(group->run_state != KBASE_CSF_GROUP_IDLE);

	list_add_tail(&group->link, &kctx->csf.sched.idle_wait_groups);
	kctx->csf.sched.num_idle_wait_grps++;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, GROUP_IDLE_WAIT_INSERT, group,
				 kctx->csf.sched.num_idle_wait_grps);
	group->run_state = KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, CSF_GROUP_SUSPENDED_ON_WAIT_SYNC, group,
				 group->run_state);
	dev_dbg(kctx->kbdev->dev,
		"Group-%d suspended on sync_wait, total wait_groups: %u\n",
		group->handle, kctx->csf.sched.num_idle_wait_grps);
}

static void remove_group_from_idle_wait(struct kbase_queue_group *const group)
{
	struct kbase_context *kctx = group->kctx;
	struct list_head *list = &kctx->csf.sched.idle_wait_groups;
	struct kbase_queue_group *new_head_grp;

	lockdep_assert_held(&kctx->kbdev->csf.scheduler.lock);

	WARN_ON(group->run_state != KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC);

	list_del_init(&group->link);
	WARN_ON(kctx->csf.sched.num_idle_wait_grps == 0);
	kctx->csf.sched.num_idle_wait_grps--;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, GROUP_IDLE_WAIT_REMOVE, group,
				 kctx->csf.sched.num_idle_wait_grps);
	new_head_grp = (!list_empty(list)) ?
				list_first_entry(list, struct kbase_queue_group, link) :
				NULL;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, GROUP_IDLE_WAIT_HEAD, new_head_grp, 0u);
	group->run_state = KBASE_CSF_GROUP_INACTIVE;
	KBASE_KTRACE_ADD_CSF_GRP(kctx->kbdev, CSF_GROUP_INACTIVE, group, group->run_state);
}

static void deschedule_idle_wait_group(struct kbase_csf_scheduler *scheduler,
		struct kbase_queue_group *group)
{
	lockdep_assert_held(&scheduler->lock);

	if (WARN_ON(!group))
		return;

	remove_group_from_runnable(scheduler, group, KBASE_CSF_GROUP_IDLE);
	insert_group_to_idle_wait(group);
}

static void update_offslot_non_idle_cnt_for_faulty_grp(struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	if (group->prepared_seq_num < scheduler->non_idle_scanout_grps) {
		int new_val =
			atomic_dec_return(&scheduler->non_idle_offslot_grps);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_DEC, group, new_val);
	}
}

static void update_offslot_non_idle_cnt_for_onslot_grp(struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	WARN_ON(group->csg_nr < 0);

	if (group->prepared_seq_num < scheduler->non_idle_scanout_grps) {
		int new_val =
			atomic_dec_return(&scheduler->non_idle_offslot_grps);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_DEC, group, new_val);
	}
}

static void update_offslot_non_idle_cnt_on_grp_suspend(
				struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	if (scheduler->state == SCHED_BUSY) {
		/* active phase or, async entering the protected mode */
		if (group->prepared_seq_num >=
		    scheduler->non_idle_scanout_grps) {
			/* At scanout, it was tagged as on-slot idle */
			if (group->run_state == KBASE_CSF_GROUP_SUSPENDED) {
				int new_val = atomic_inc_return(
					&scheduler->non_idle_offslot_grps);
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_INC,
							 group, new_val);
			}
		} else {
			if (group->run_state != KBASE_CSF_GROUP_SUSPENDED) {
				int new_val = atomic_dec_return(
					&scheduler->non_idle_offslot_grps);
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_DEC,
							 group, new_val);
			}
		}
	} else {
		/* async phases */
		if (group->run_state == KBASE_CSF_GROUP_SUSPENDED) {
			int new_val = atomic_inc_return(
				&scheduler->non_idle_offslot_grps);
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_INC, group,
						 new_val);
		}
	}
}

static bool confirm_cmd_buf_empty(struct kbase_queue const *queue)
{
	bool cs_empty;
	bool cs_idle;
	u32 sb_status = 0;

	struct kbase_device const *const kbdev = queue->group->kctx->kbdev;
	struct kbase_csf_global_iface const *const iface =
		&kbdev->csf.global_iface;

	u32 glb_version = iface->version;

	u64 const *input_addr = (u64 const *)queue->user_io_addr;
	u64 const *output_addr = (u64 const *)(queue->user_io_addr + PAGE_SIZE);

	if (glb_version >= kbase_csf_interface_version(1, 0, 0)) {
		/* CS_STATUS_SCOREBOARD supported from CSF 1.0 */
		struct kbase_csf_cmd_stream_group_info const *const ginfo =
			&kbdev->csf.global_iface.groups[queue->group->csg_nr];
		struct kbase_csf_cmd_stream_info const *const stream =
			&ginfo->streams[queue->csi_index];

		sb_status = CS_STATUS_SCOREBOARDS_NONZERO_GET(
			kbase_csf_firmware_cs_output(stream,
						     CS_STATUS_SCOREBOARDS));
	}

	cs_empty = (input_addr[CS_INSERT_LO / sizeof(u64)] ==
		    output_addr[CS_EXTRACT_LO / sizeof(u64)]);
	cs_idle = cs_empty && (!sb_status);

	return cs_idle;
}

static void save_csg_slot(struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	struct kbase_csf_cmd_stream_group_info *ginfo;
	u32 state;

	lockdep_assert_held(&scheduler->lock);

	if (WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(group)))
		return;

	ginfo = &kbdev->csf.global_iface.groups[group->csg_nr];

	state =
	    CSG_ACK_STATE_GET(kbase_csf_firmware_csg_output(ginfo, CSG_ACK));

	if (!WARN_ON((state != CSG_ACK_STATE_SUSPEND) &&
		     (state != CSG_ACK_STATE_TERMINATE))) {
		u32 max_streams = ginfo->stream_num;
		u32 i;
		bool sync_wait = false;
		bool idle = kbase_csf_firmware_csg_output(ginfo, CSG_STATUS_STATE) &
			    CSG_STATUS_STATE_IDLE_MASK;
#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
		for (i = 0; i < max_streams; i++)
			update_hw_active(group->bound_queues[i], false);
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
		for (i = 0; idle && i < max_streams; i++) {
			struct kbase_queue *const queue =
					group->bound_queues[i];

			if (!queue || !queue->enabled)
				continue;

			if (save_slot_cs(ginfo, queue))
				sync_wait = true;
			else {
				/* Need to confirm if ringbuffer of the GPU
				 * queue is empty or not. A race can arise
				 * between the flush of GPU queue and suspend
				 * of CSG. If a queue is flushed after FW has
				 * set the IDLE bit in CSG_STATUS_STATE, then
				 * Scheduler will incorrectly consider CSG
				 * as idle. And there may not be any further
				 * flush call for the GPU queue, which would
				 * have de-idled the CSG.
				 */
				idle = confirm_cmd_buf_empty(queue);
			}
		}

		if (idle) {
			/* Take the suspended group out of the runnable_groups
			 * list of the context and move it to the
			 * idle_wait_groups list.
			 */
			if (sync_wait)
				deschedule_idle_wait_group(scheduler, group);
			else {
				group->run_state =
					KBASE_CSF_GROUP_SUSPENDED_ON_IDLE;
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_SUSPENDED_ON_IDLE, group,
							 group->run_state);
				dev_dbg(kbdev->dev, "Group-%d suspended: idle",
					group->handle);
			}
		} else {
			group->run_state = KBASE_CSF_GROUP_SUSPENDED;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_SUSPENDED, group,
						 group->run_state);
		}

		update_offslot_non_idle_cnt_on_grp_suspend(group);
		kbase_csf_tiler_heap_reclaim_sched_notify_grp_suspend(group);
	}
}

/* Cleanup_csg_slot after it has been vacated, ready for next csg run.
 * Return whether there is a kctx address fault associated with the group
 * for which the clean-up is done.
 */
static bool cleanup_csg_slot(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	struct kbase_csf_cmd_stream_group_info *ginfo;
	s8 slot;
	struct kbase_csf_csg_slot *csg_slot;
	unsigned long flags;
	u32 i;
	bool as_fault = false;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(group)))
		return as_fault;

	slot = group->csg_nr;
	csg_slot = &kbdev->csf.scheduler.csg_slots[slot];
	ginfo = &global_iface->groups[slot];

	/* Now loop through all the bound CSs, and clean them via a stop */
	for (i = 0; i < ginfo->stream_num; i++) {
		struct kbase_csf_cmd_stream_info *stream = &ginfo->streams[i];

		if (group->bound_queues[i]) {
			if (group->bound_queues[i]->enabled) {
				kbase_csf_firmware_cs_input_mask(stream,
					CS_REQ, CS_REQ_STATE_STOP,
					CS_REQ_STATE_MASK);
			}

			unassign_user_doorbell_from_queue(kbdev,
				group->bound_queues[i]);
		}
	}

	unassign_user_doorbell_from_group(kbdev, group);

	/* The csg does not need cleanup other than drop its AS */
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	as_fault = kbase_ctx_flag(kctx, KCTX_AS_DISABLED_ON_FAULT);
	kbase_ctx_sched_release_ctx(kctx);
	if (unlikely(group->faulted))
		as_fault = true;
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);

	/* now marking the slot is vacant */
	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	kbdev->csf.scheduler.csg_slots[slot].resident_group = NULL;
	clear_bit(slot, kbdev->csf.scheduler.csg_slots_idle_mask);
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_IDLE_CLEAR, group,
				 kbdev->csf.scheduler.csg_slots_idle_mask[0]);

	group->csg_nr = KBASEP_CSG_NR_INVALID;
	set_bit(slot, kbdev->csf.scheduler.csgs_events_enable_mask);
	clear_bit(slot, kbdev->csf.scheduler.csg_inuse_bitmap);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	csg_slot->trigger_jiffies = jiffies;
	atomic_set(&csg_slot->state, CSG_SLOT_READY);

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_CLEANED, group, slot);
	dev_dbg(kbdev->dev, "Cleanup done for group %d on slot %d\n",
		group->handle, slot);

	KBASE_TLSTREAM_TL_KBASE_DEVICE_DEPROGRAM_CSG(kbdev,
		kbdev->gpu_props.props.raw_props.gpu_id, slot);

	return as_fault;
}

static void update_csg_slot_priority(struct kbase_queue_group *group, u8 prio)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_csg_slot *csg_slot;
	struct kbase_csf_cmd_stream_group_info *ginfo;
	s8 slot;
	u8 prev_prio;
	u32 ep_cfg;
	u32 csg_req;
	unsigned long flags;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(group)))
		return;

	slot = group->csg_nr;
	csg_slot = &kbdev->csf.scheduler.csg_slots[slot];
	ginfo = &kbdev->csf.global_iface.groups[slot];

	/* CSGs remaining on-slot can be either idle or runnable.
	 * This also applies in protected mode.
	 */
	WARN_ON(!((group->run_state == KBASE_CSF_GROUP_RUNNABLE) ||
		(group->run_state == KBASE_CSF_GROUP_IDLE)));

	/* Update consumes a group from scanout */
	update_offslot_non_idle_cnt_for_onslot_grp(group);

	if (csg_slot->priority == prio)
		return;

	/* Read the csg_ep_cfg back for updating the priority field */
	ep_cfg = kbase_csf_firmware_csg_input_read(ginfo, CSG_EP_REQ);
	prev_prio = CSG_EP_REQ_PRIORITY_GET(ep_cfg);
	ep_cfg = CSG_EP_REQ_PRIORITY_SET(ep_cfg, prio);
	kbase_csf_firmware_csg_input(ginfo, CSG_EP_REQ, ep_cfg);

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	csg_req = kbase_csf_firmware_csg_output(ginfo, CSG_ACK);
	csg_req ^= CSG_REQ_EP_CFG_MASK;
	kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ, csg_req,
					  CSG_REQ_EP_CFG_MASK);
	kbase_csf_ring_csg_doorbell(kbdev, slot);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	csg_slot->priority = prio;

	dev_dbg(kbdev->dev, "Priority for group %d of context %d_%d on slot %d to be updated from %u to %u\n",
		group->handle, group->kctx->tgid, group->kctx->id, slot,
		prev_prio, prio);

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_PRIO_UPDATE, group, prev_prio);

	set_bit(slot, kbdev->csf.scheduler.csg_slots_prio_update);
}

static void program_csg_slot(struct kbase_queue_group *group, s8 slot,
		u8 prio)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	const u64 shader_core_mask =
		kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER);
	const u64 tiler_core_mask =
		kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER);
	const u64 compute_mask = shader_core_mask & group->compute_mask;
	const u64 fragment_mask = shader_core_mask & group->fragment_mask;
	const u64 tiler_mask = tiler_core_mask & group->tiler_mask;
	const u8 num_cores = kbdev->gpu_props.num_cores;
	const u8 compute_max = min(num_cores, group->compute_max);
	const u8 fragment_max = min(num_cores, group->fragment_max);
	const u8 tiler_max = min(CSG_TILER_MAX, group->tiler_max);
	struct kbase_csf_cmd_stream_group_info *ginfo;
	u32 ep_cfg = 0;
	u32 csg_req;
	u32 state;
	int i;
	unsigned long flags;
	const u64 normal_suspend_buf =
		group->normal_suspend_buf.reg->start_pfn << PAGE_SHIFT;
	struct kbase_csf_csg_slot *csg_slot =
		&kbdev->csf.scheduler.csg_slots[slot];

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ON(slot < 0) &&
	    WARN_ON(slot >= global_iface->group_num))
		return;

	WARN_ON(atomic_read(&csg_slot->state) != CSG_SLOT_READY);

	ginfo = &global_iface->groups[slot];

	/* Pick an available address space for this context */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_ctx_sched_retain_ctx(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	if (kctx->as_nr == KBASEP_AS_NR_INVALID) {
		dev_dbg(kbdev->dev, "Could not get a valid AS for group %d of context %d_%d on slot %d\n",
			 group->handle, kctx->tgid, kctx->id, slot);
		return;
	}

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	set_bit(slot, kbdev->csf.scheduler.csg_inuse_bitmap);
	kbdev->csf.scheduler.csg_slots[slot].resident_group = group;
	group->csg_nr = slot;
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	assign_user_doorbell_to_group(kbdev, group);

	/* Now loop through all the bound & kicked CSs, and program them */
	for (i = 0; i < MAX_SUPPORTED_STREAMS_PER_GROUP; i++) {
		struct kbase_queue *queue = group->bound_queues[i];

		if (queue)
			program_cs(kbdev, queue, false);
	}


	/* Endpoint programming for CSG */
	kbase_csf_firmware_csg_input(ginfo, CSG_ALLOW_COMPUTE_LO,
				     compute_mask & U32_MAX);
	kbase_csf_firmware_csg_input(ginfo, CSG_ALLOW_COMPUTE_HI,
				     compute_mask >> 32);
	kbase_csf_firmware_csg_input(ginfo, CSG_ALLOW_FRAGMENT_LO,
				     fragment_mask & U32_MAX);
	kbase_csf_firmware_csg_input(ginfo, CSG_ALLOW_FRAGMENT_HI,
				     fragment_mask >> 32);
	kbase_csf_firmware_csg_input(ginfo, CSG_ALLOW_OTHER,
				     tiler_mask & U32_MAX);

	/* Register group UID with firmware */
	kbase_csf_firmware_csg_input(ginfo, CSG_ITER_TRACE_CONFIG,
				     group->group_uid);

	ep_cfg = CSG_EP_REQ_COMPUTE_EP_SET(ep_cfg, compute_max);
	ep_cfg = CSG_EP_REQ_FRAGMENT_EP_SET(ep_cfg, fragment_max);
	ep_cfg = CSG_EP_REQ_TILER_EP_SET(ep_cfg, tiler_max);
	ep_cfg = CSG_EP_REQ_PRIORITY_SET(ep_cfg, prio);
	kbase_csf_firmware_csg_input(ginfo, CSG_EP_REQ, ep_cfg);

	/* Program the address space number assigned to the context */
	kbase_csf_firmware_csg_input(ginfo, CSG_CONFIG, kctx->as_nr);

	kbase_csf_firmware_csg_input(ginfo, CSG_SUSPEND_BUF_LO,
			normal_suspend_buf & U32_MAX);
	kbase_csf_firmware_csg_input(ginfo, CSG_SUSPEND_BUF_HI,
			normal_suspend_buf >> 32);

	if (group->protected_suspend_buf.reg) {
		const u64 protm_suspend_buf =
			group->protected_suspend_buf.reg->start_pfn <<
				PAGE_SHIFT;
		kbase_csf_firmware_csg_input(ginfo, CSG_PROTM_SUSPEND_BUF_LO,
			protm_suspend_buf & U32_MAX);
		kbase_csf_firmware_csg_input(ginfo, CSG_PROTM_SUSPEND_BUF_HI,
			protm_suspend_buf >> 32);
	}

	if (group->dvs_buf) {
		kbase_csf_firmware_csg_input(ginfo, CSG_DVS_BUF_LO,
					     group->dvs_buf & U32_MAX);
		kbase_csf_firmware_csg_input(ginfo, CSG_DVS_BUF_HI,
					     group->dvs_buf >> 32);
	}

	/* Enable all interrupts for now */
	kbase_csf_firmware_csg_input(ginfo, CSG_ACK_IRQ_MASK, ~((u32)0));

	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, flags);
	csg_req = kbase_csf_firmware_csg_output(ginfo, CSG_ACK);
	csg_req ^= CSG_REQ_EP_CFG_MASK;
	kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ, csg_req,
					  CSG_REQ_EP_CFG_MASK);

	/* Set state to START/RESUME */
	if (queue_group_suspended_locked(group)) {
		state = CSG_REQ_STATE_RESUME;
	} else {
		WARN_ON(group->run_state != KBASE_CSF_GROUP_RUNNABLE);
		state = CSG_REQ_STATE_START;
	}

	kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ,
			state, CSG_REQ_STATE_MASK);
	kbase_csf_ring_csg_doorbell(kbdev, slot);
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);

	/* Update status before rings the door-bell, marking ready => run */
	atomic_set(&csg_slot->state, CSG_SLOT_READY2RUN);
	csg_slot->trigger_jiffies = jiffies;
	csg_slot->priority = prio;

	/* Trace the programming of the CSG on the slot */
	KBASE_TLSTREAM_TL_KBASE_DEVICE_PROGRAM_CSG(
		kbdev, kbdev->gpu_props.props.raw_props.gpu_id, group->kctx->id,
		group->handle, slot, (state == CSG_REQ_STATE_RESUME) ? 1 : 0);

	dev_dbg(kbdev->dev, "Starting group %d of context %d_%d on slot %d with priority %u\n",
		group->handle, kctx->tgid, kctx->id, slot, prio);

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_START_REQ, group,
				 (((u64)ep_cfg) << 32) | ((((u32)kctx->as_nr) & 0xF) << 16) |
					 (state & (CSG_REQ_STATE_MASK >> CS_REQ_STATE_SHIFT)));

	/* Update the heap reclaim manager */
	kbase_csf_tiler_heap_reclaim_sched_notify_grp_active(group);

	/* Programming a slot consumes a group from scanout */
	update_offslot_non_idle_cnt_for_onslot_grp(group);
}

static void remove_scheduled_group(struct kbase_device *kbdev,
		struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	WARN_ON(group->prepared_seq_num ==
		KBASEP_GROUP_PREPARED_SEQ_NUM_INVALID);
	WARN_ON(list_empty(&group->link_to_schedule));

	list_del_init(&group->link_to_schedule);
	scheduler->ngrp_to_schedule--;
	group->prepared_seq_num = KBASEP_GROUP_PREPARED_SEQ_NUM_INVALID;
	group->kctx->csf.sched.ngrp_to_schedule--;
}

static void sched_evict_group(struct kbase_queue_group *group, bool fault,
			      bool update_non_idle_offslot_grps_cnt)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (queue_group_scheduled_locked(group)) {
		u32 i;

		if (update_non_idle_offslot_grps_cnt &&
		    (group->run_state == KBASE_CSF_GROUP_SUSPENDED ||
		     group->run_state == KBASE_CSF_GROUP_RUNNABLE)) {
			int new_val = atomic_dec_return(
				&scheduler->non_idle_offslot_grps);
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_DEC, group,
						 new_val);
		}

		for (i = 0; i < MAX_SUPPORTED_STREAMS_PER_GROUP; i++) {
			if (group->bound_queues[i])
				group->bound_queues[i]->enabled = false;
		}

		if (group->prepared_seq_num !=
				KBASEP_GROUP_PREPARED_SEQ_NUM_INVALID)
			remove_scheduled_group(kbdev, group);

		if (group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC)
			remove_group_from_idle_wait(group);
		else {
			remove_group_from_runnable(scheduler, group,
						KBASE_CSF_GROUP_INACTIVE);
		}

		WARN_ON(group->run_state != KBASE_CSF_GROUP_INACTIVE);

		if (fault) {
			group->run_state = KBASE_CSF_GROUP_FAULT_EVICTED;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_FAULT_EVICTED, group,
						 scheduler->total_runnable_grps);
		}

		KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_EVICT, group,
					 (((u64)scheduler->total_runnable_grps) << 32) |
						 ((u32)group->run_state));
		dev_dbg(kbdev->dev, "group %d exited scheduler, num_runnable_grps %d\n",
			group->handle, scheduler->total_runnable_grps);
		/* Notify a group has been evicted */
		wake_up_all(&kbdev->csf.event_wait);
	}

	kbase_csf_tiler_heap_reclaim_sched_notify_grp_evict(group);
}

static int term_group_sync(struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	long remaining = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	int err = 0;

	term_csg_slot(group);

	remaining = wait_event_timeout(kbdev->csf.event_wait,
		group->cs_unrecoverable || csg_slot_stopped_locked(kbdev, group->csg_nr),
		remaining);

	if (unlikely(!remaining)) {
		enum dumpfault_error_type error_type = DF_CSG_TERMINATE_TIMEOUT;

		dev_warn(kbdev->dev, "[%llu] term request timeout (%d ms) for group %d of context %d_%d on slot %d",
			 kbase_backend_get_cycle_cnt(kbdev), kbdev->csf.fw_timeout_ms,
			 group->handle, group->kctx->tgid,
			 group->kctx->id, group->csg_nr);
		if (kbase_csf_firmware_ping_wait(kbdev, FW_PING_AFTER_ERROR_TIMEOUT_MS))
			error_type = DF_PING_REQUEST_TIMEOUT;
		kbase_debug_csf_fault_notify(kbdev, group->kctx, error_type);
		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu(kbdev);


		err = -ETIMEDOUT;
	}

	return err;
}

void kbase_csf_scheduler_group_deschedule(struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	bool wait_for_termination = true;
	bool on_slot;

	kbase_reset_gpu_assert_failed_or_prevented(kbdev);
	lockdep_assert_held(&group->kctx->csf.lock);
	mutex_lock(&scheduler->lock);

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_DESCHEDULE, group, group->run_state);
	wait_for_dump_complete_on_group_deschedule(group);
	if (!queue_group_scheduled_locked(group))
		goto unlock;

	on_slot = kbasep_csf_scheduler_group_is_on_slot_locked(group);

#ifdef KBASE_PM_RUNTIME
	/* If the queue group is on slot and Scheduler is in SLEEPING state,
	 * then we need to wake up the Scheduler to exit the sleep state rather
	 * than waiting for the runtime suspend or power down of GPU.
	 * The group termination is usually triggered in the context of Application
	 * thread and it has been seen that certain Apps can destroy groups at
	 * random points and not necessarily when the App is exiting.
	 */
	if (on_slot && (scheduler->state == SCHED_SLEEPING)) {
		scheduler_wakeup(kbdev, true);

		/* Wait for MCU firmware to start running */
		if (kbase_csf_scheduler_wait_mcu_active(kbdev)) {
			dev_warn(
				kbdev->dev,
				"[%llu] Wait for MCU active failed when terminating group %d of context %d_%d on slot %d",
				kbase_backend_get_cycle_cnt(kbdev),
				group->handle, group->kctx->tgid,
				group->kctx->id, group->csg_nr);
			/* No point in waiting for CSG termination if MCU didn't
			 * become active.
			 */
			wait_for_termination = false;
		}
	}
#endif
	if (!on_slot) {
		sched_evict_group(group, false, true);
	} else {
		bool as_faulty;

		if (likely(wait_for_termination))
			term_group_sync(group);
		else
			term_csg_slot(group);

		/* Treat the csg been terminated */
		as_faulty = cleanup_csg_slot(group);
		/* remove from the scheduler list */
		sched_evict_group(group, as_faulty, false);
	}

	WARN_ON(queue_group_scheduled_locked(group));

unlock:
	mutex_unlock(&scheduler->lock);
}

/**
 * scheduler_group_schedule() - Schedule a GPU command queue group on firmware
 *
 * @group: Pointer to the queue group to be scheduled.
 *
 * This function would enable the scheduling of GPU command queue group on
 * firmware.
 *
 * Return: 0 on success, or negative on failure.
 */
static int scheduler_group_schedule(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&kctx->csf.lock);
	lockdep_assert_held(&scheduler->lock);

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_SCHEDULE, group, group->run_state);
	if (group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC)
		update_idle_suspended_group_state(group);
	else if (queue_group_idle_locked(group)) {
		WARN_ON(kctx->csf.sched.num_runnable_grps == 0);
		WARN_ON(kbdev->csf.scheduler.total_runnable_grps == 0);

		if (group->run_state == KBASE_CSF_GROUP_SUSPENDED_ON_IDLE)
			update_idle_suspended_group_state(group);
		else {
			struct kbase_queue_group *protm_grp;
			unsigned long flags;

			WARN_ON(!kbasep_csf_scheduler_group_is_on_slot_locked(
				group));

			group->run_state = KBASE_CSF_GROUP_RUNNABLE;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_RUNNABLE, group,
						 group->run_state);

			/* A normal mode CSG could be idle onslot during
			 * protected mode. In this case clear the
			 * appropriate bit in csg_slots_idle_mask.
			 */
			spin_lock_irqsave(&scheduler->interrupt_lock, flags);
			protm_grp = scheduler->active_protm_grp;
			if (protm_grp && protm_grp != group) {
				clear_bit((unsigned int)group->csg_nr,
					  scheduler->csg_slots_idle_mask);
				/* Request the update to confirm the condition inferred. */
				group->reevaluate_idle_status = true;
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_IDLE_CLEAR, group,
					scheduler->csg_slots_idle_mask[0]);
			}
			spin_unlock_irqrestore(&scheduler->interrupt_lock,
					       flags);

			/* If GPU is in protected mode then any doorbells rang
			 * would have no effect. Check if GPU is in protected
			 * mode and if this group has higher priority than the
			 * active protected mode group. If so prompt the FW
			 * to exit protected mode.
			 */
			if (protm_grp &&
			    group->scan_seq_num < protm_grp->scan_seq_num) {
				/* Prompt the FW to exit protected mode */
				scheduler_force_protm_exit(kbdev);
			}
		}
	} else if (!queue_group_scheduled_locked(group)) {
		int new_val;

		insert_group_to_runnable(&kbdev->csf.scheduler, group,
			KBASE_CSF_GROUP_RUNNABLE);
		/* A new group into the scheduler */
		new_val = atomic_inc_return(
			&kbdev->csf.scheduler.non_idle_offslot_grps);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_INC, group, new_val);
	}

	/* Since a group has become active now, check if GPU needs to be
	 * powered up. Also rekick the Scheduler.
	 */
	scheduler_wakeup(kbdev, true);

	return 0;
}

/**
 * set_max_csg_slots() - Set the number of available CSG slots
 *
 * @kbdev: Pointer of the GPU device.
 *
 * This function would set/limit the number of CSG slots that
 * can be used in the given tick/tock. It would be less than the total CSG
 * slots supported by firmware if the number of GPU address space slots
 * required to utilize all the CSG slots is more than the available
 * address space slots.
 */
static inline void set_max_csg_slots(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned int total_csg_slots = kbdev->csf.global_iface.group_num;
	unsigned int max_address_space_slots =
			kbdev->nr_hw_address_spaces - NUM_RESERVED_AS_SLOTS;

	WARN_ON(scheduler->num_active_address_spaces > total_csg_slots);

	if (likely(scheduler->num_active_address_spaces <=
		   max_address_space_slots))
		scheduler->num_csg_slots_for_tick = total_csg_slots;
}

/**
 * count_active_address_space() - Count the number of GPU address space slots
 *
 * @kbdev: Pointer of the GPU device.
 * @kctx: Pointer of the Kbase context.
 *
 * This function would update the counter that is tracking the number of GPU
 * address space slots that would be required to program the CS
 * group slots from the groups at the head of groups_to_schedule list.
 */
static inline void count_active_address_space(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned int total_csg_slots = kbdev->csf.global_iface.group_num;
	unsigned int max_address_space_slots =
			kbdev->nr_hw_address_spaces - NUM_RESERVED_AS_SLOTS;

	if (scheduler->ngrp_to_schedule <= total_csg_slots) {
		if (kctx->csf.sched.ngrp_to_schedule == 1)
			scheduler->num_active_address_spaces++;

		if (scheduler->num_active_address_spaces <=
		    max_address_space_slots)
			scheduler->num_csg_slots_for_tick++;
	}
}

/* Two schemes are used in assigning the priority to CSG slots for a given
 * CSG from the 'groups_to_schedule' list.
 * This is needed as an idle on-slot group is deprioritized by moving it to
 * the tail of 'groups_to_schedule' list. As a result it can either get
 * evicted from the CSG slot in current tick/tock dealing, or its position
 * can be after the lower priority non-idle groups in the 'groups_to_schedule'
 * list. The latter case can result in the on-slot subset containing both
 * non-idle and idle CSGs, and is handled through the 2nd scheme described
 * below.
 *
 * First scheme :- If all the slots are going to be occupied by the non-idle or
 * idle groups, then a simple assignment of the priority is done as per the
 * position of a group in the 'groups_to_schedule' list. So maximum priority
 * gets assigned to the slot of a group which is at the head of the list.
 * Here the 'groups_to_schedule' list would effectively be ordered as per the
 * static priority of groups.
 *
 * Second scheme :- If the slots are going to be occupied by a mix of idle and
 * non-idle groups then the priority assignment needs to ensure that the
 * priority of a slot belonging to a higher priority idle group will always be
 * greater than the priority of a slot belonging to a lower priority non-idle
 * group, reflecting the original position of a group in the scan order (i.e
 * static priority) 'scan_seq_num', which is set during the prepare phase of a
 * tick/tock before the group is moved to 'idle_groups_to_schedule' list if it
 * is idle.
 * The priority range [MAX_CSG_SLOT_PRIORITY, 0] is partitioned with the first
 * 'slots_for_tick' groups in the original scan order are assigned a priority in
 * the subrange [MAX_CSG_SLOT_PRIORITY, MAX_CSG_SLOT_PRIORITY - slots_for_tick),
 * whereas rest of the groups are assigned the priority in the subrange
 * [MAX_CSG_SLOT_PRIORITY - slots_for_tick, 0]. This way even if an idle higher
 * priority group ends up after the non-idle lower priority groups in the
 * 'groups_to_schedule' list, it will get a higher slot priority. And this will
 * enable the FW to quickly start the execution of higher priority group when it
 * gets de-idled.
 */
static u8 get_slot_priority(struct kbase_queue_group *group)
{
	struct kbase_csf_scheduler *scheduler =
		&group->kctx->kbdev->csf.scheduler;
	u8 slot_prio;
	u32 slots_for_tick = scheduler->num_csg_slots_for_tick;
	u32 used_slots = slots_for_tick - scheduler->remaining_tick_slots;
	/* Check if all the slots are going to be occupied by the non-idle or
	 * idle groups.
	 */
	if (scheduler->non_idle_scanout_grps >= slots_for_tick ||
	    !scheduler->non_idle_scanout_grps) {
		slot_prio = (u8)(MAX_CSG_SLOT_PRIORITY - used_slots);
	} else {
		/* There will be a mix of idle and non-idle groups. */
		if (group->scan_seq_num < slots_for_tick)
			slot_prio = (u8)(MAX_CSG_SLOT_PRIORITY -
					 group->scan_seq_num);
		else if (MAX_CSG_SLOT_PRIORITY > (slots_for_tick + used_slots))
			slot_prio = (u8)(MAX_CSG_SLOT_PRIORITY - (slots_for_tick + used_slots));
		else
			slot_prio = 0;
	}
	return slot_prio;
}

/**
 * update_resident_groups_priority() - Update the priority of resident groups
 *
 * @kbdev:    The GPU device.
 *
 * This function will update the priority of all resident queue groups
 * that are at the head of groups_to_schedule list, preceding the first
 * non-resident group.
 *
 * This function will also adjust kbase_csf_scheduler.remaining_tick_slots on
 * the priority update.
 */
static void update_resident_groups_priority(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 num_groups = scheduler->num_csg_slots_for_tick;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	while (!list_empty(&scheduler->groups_to_schedule)) {
		struct kbase_queue_group *group =
			list_first_entry(&scheduler->groups_to_schedule,
					struct kbase_queue_group,
					 link_to_schedule);
		bool resident =
			kbasep_csf_scheduler_group_is_on_slot_locked(group);

		if ((group->prepared_seq_num >= num_groups) || !resident)
			break;

		update_csg_slot_priority(group,
					 get_slot_priority(group));

		/* Drop the head group from the list */
		remove_scheduled_group(kbdev, group);
		scheduler->remaining_tick_slots--;
	}
}

/**
 * program_group_on_vacant_csg_slot() - Program a non-resident group on the
 *                                      given vacant CSG slot.
 * @kbdev:    Pointer to the GPU device.
 * @slot:     Vacant CSG slot number.
 *
 * This function will program a non-resident group at the head of
 * kbase_csf_scheduler.groups_to_schedule list on the given vacant
 * CSG slot, provided the initial position of the non-resident
 * group in the list is less than the number of CSG slots and there is
 * an available GPU address space slot.
 * kbase_csf_scheduler.remaining_tick_slots would also be adjusted after
 * programming the slot.
 */
static void program_group_on_vacant_csg_slot(struct kbase_device *kbdev,
		s8 slot)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_queue_group *const group =
		list_empty(&scheduler->groups_to_schedule) ? NULL :
			list_first_entry(&scheduler->groups_to_schedule,
					struct kbase_queue_group,
					link_to_schedule);
	u32 num_groups = scheduler->num_csg_slots_for_tick;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	if (group && (group->prepared_seq_num < num_groups)) {
		bool ret = kbasep_csf_scheduler_group_is_on_slot_locked(group);

		if (!WARN_ON(ret)) {
			if (kctx_as_enabled(group->kctx) && !group->faulted) {
				program_csg_slot(group, slot,
					get_slot_priority(group));

				if (likely(csg_slot_in_use(kbdev, slot))) {
					/* Drop the head group from the list */
					remove_scheduled_group(kbdev, group);
					scheduler->remaining_tick_slots--;
				}
			} else {
				update_offslot_non_idle_cnt_for_faulty_grp(
					group);
				remove_scheduled_group(kbdev, group);
			}
		}
	}
}

/**
 * program_vacant_csg_slot() - Program the vacant CSG slot with a non-resident
 *                             group and update the priority of resident groups.
 *
 * @kbdev:    Pointer to the GPU device.
 * @slot:     Vacant CSG slot number.
 *
 * This function will first update the priority of all resident queue groups
 * that are at the head of groups_to_schedule list, preceding the first
 * non-resident group, it will then try to program the given CS
 * group slot with the non-resident group. Finally update the priority of all
 * resident queue groups following the non-resident group.
 *
 * kbase_csf_scheduler.remaining_tick_slots would also be adjusted.
 */
static void program_vacant_csg_slot(struct kbase_device *kbdev, s8 slot)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	struct kbase_csf_csg_slot *const csg_slot =
				scheduler->csg_slots;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	WARN_ON(atomic_read(&csg_slot[slot].state) != CSG_SLOT_READY);

	/* First update priority for already resident groups (if any)
	 * before the non-resident group
	 */
	update_resident_groups_priority(kbdev);

	/* Now consume the vacant slot for the non-resident group */
	program_group_on_vacant_csg_slot(kbdev, slot);

	/* Now update priority for already resident groups (if any)
	 * following the non-resident group
	 */
	update_resident_groups_priority(kbdev);
}

static bool slots_state_changed(struct kbase_device *kbdev,
		unsigned long *slots_mask,
		bool (*state_check_func)(struct kbase_device *, s8))
{
	u32 num_groups = kbdev->csf.global_iface.group_num;
	DECLARE_BITMAP(changed_slots, MAX_SUPPORTED_CSGS) = {0};
	bool changed = false;
	u32 i;

	for_each_set_bit(i, slots_mask, num_groups) {
		if (state_check_func(kbdev, (s8)i)) {
			set_bit(i, changed_slots);
			changed = true;
		}
	}

	if (changed)
		bitmap_copy(slots_mask, changed_slots, MAX_SUPPORTED_CSGS);

	return changed;
}

/**
 * program_suspending_csg_slots() - Program the CSG slots vacated on suspension
 *                                  of queue groups running on them.
 *
 * @kbdev:    Pointer to the GPU device.
 *
 * This function will first wait for the ongoing suspension to complete on a
 * CSG slot and will then program the vacant slot with the
 * non-resident queue group inside the groups_to_schedule list.
 * The programming of the non-resident queue group on the vacant slot could
 * fail due to unavailability of free GPU address space slot and so the
 * programming is re-attempted after the ongoing suspension has completed
 * for all the CSG slots.
 * The priority of resident groups before and after the non-resident group
 * in the groups_to_schedule list would also be updated.
 * This would be repeated for all the slots undergoing suspension.
 * GPU reset would be initiated if the wait for suspend times out.
 */
static void program_suspending_csg_slots(struct kbase_device *kbdev)
{
	u32 num_groups = kbdev->csf.global_iface.group_num;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	DECLARE_BITMAP(slot_mask, MAX_SUPPORTED_CSGS);
	DECLARE_BITMAP(evicted_mask, MAX_SUPPORTED_CSGS) = {0};
	bool suspend_wait_failed = false;
	long remaining = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	/* In the current implementation, csgs_events_enable_mask would be used
	 * only to indicate suspending CSGs.
	 */
	bitmap_complement(slot_mask, scheduler->csgs_events_enable_mask,
		MAX_SUPPORTED_CSGS);

	while (!bitmap_empty(slot_mask, MAX_SUPPORTED_CSGS)) {
		DECLARE_BITMAP(changed, MAX_SUPPORTED_CSGS);

		bitmap_copy(changed, slot_mask, MAX_SUPPORTED_CSGS);

		remaining = wait_event_timeout(kbdev->csf.event_wait,
			slots_state_changed(kbdev, changed,
				csg_slot_stopped_raw),
			remaining);

		if (likely(remaining)) {
			u32 i;

			for_each_set_bit(i, changed, num_groups) {
				struct kbase_queue_group *group =
					scheduler->csg_slots[i].resident_group;

				if (WARN_ON(!csg_slot_stopped_locked(kbdev, (s8)i)))
					continue;

				/* The on slot csg is now stopped */
				clear_bit(i, slot_mask);

				if (likely(group)) {
					bool as_fault;
					/* Only do save/cleanup if the
					 * group is not terminated during
					 * the sleep.
					 */
					save_csg_slot(group);
					as_fault = cleanup_csg_slot(group);
					/* If AS fault detected, evict it */
					if (as_fault) {
						sched_evict_group(group, true, true);
						set_bit(i, evicted_mask);
					}
				}

				program_vacant_csg_slot(kbdev, (s8)i);
			}
		} else {
			u32 i;

			/* Groups that have failed to suspend in time shall
			 * raise a fatal error as they could no longer be
			 * safely resumed.
			 */
			for_each_set_bit(i, slot_mask, num_groups) {
				struct kbase_queue_group *const group =
					scheduler->csg_slots[i].resident_group;
				enum dumpfault_error_type error_type = DF_CSG_SUSPEND_TIMEOUT;

				struct base_gpu_queue_group_error const
					err_payload = { .error_type =
								BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
							.payload = {
								.fatal_group = {
									.status =
										GPU_EXCEPTION_TYPE_SW_FAULT_2,
								} } };

				if (unlikely(group == NULL))
					continue;

				/* TODO GPUCORE-25328: The CSG can't be
				 * terminated, the GPU will be reset as a
				 * work-around.
				 */
				dev_warn(
					kbdev->dev,
					"[%llu] Group %d of context %d_%d on slot %u failed to suspend (timeout %d ms)",
					kbase_backend_get_cycle_cnt(kbdev),
					group->handle, group->kctx->tgid,
					group->kctx->id, i,
					kbdev->csf.fw_timeout_ms);
				if (kbase_csf_firmware_ping_wait(kbdev,
								 FW_PING_AFTER_ERROR_TIMEOUT_MS))
					error_type = DF_PING_REQUEST_TIMEOUT;
				schedule_actions_trigger_df(kbdev, group->kctx, error_type);

				kbase_csf_add_group_fatal_error(group, &err_payload);
				kbase_event_wakeup(group->kctx);

				/* The group has failed suspension, stop
				 * further examination.
				 */
				clear_bit(i, slot_mask);
				set_bit(i, scheduler->csgs_events_enable_mask);
				update_offslot_non_idle_cnt_for_onslot_grp(
					group);
			}

			suspend_wait_failed = true;
		}
	}

	if (!bitmap_empty(evicted_mask, MAX_SUPPORTED_CSGS))
		dev_info(kbdev->dev, "Scheduler evicting slots: 0x%*pb\n",
			 num_groups, evicted_mask);

	if (likely(!suspend_wait_failed)) {
		u32 i;

		while (scheduler->ngrp_to_schedule &&
		       scheduler->remaining_tick_slots) {
			i = find_first_zero_bit(scheduler->csg_inuse_bitmap,
					num_groups);
			if (WARN_ON(i == num_groups))
				break;
			program_vacant_csg_slot(kbdev, (s8)i);
			if (!csg_slot_in_use(kbdev, (int)i)) {
				dev_warn(kbdev->dev, "Couldn't use CSG slot %d despite being vacant", i);
				break;
			}
		}
	} else {
		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu(kbdev);
	}
}

static void suspend_queue_group(struct kbase_queue_group *group)
{
	unsigned long flags;
	struct kbase_csf_scheduler *const scheduler =
		&group->kctx->kbdev->csf.scheduler;

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	/* This shall be used in program_suspending_csg_slots() where we
	 * assume that whilst CSGs are being suspended, this bitmask is not
	 * used by anything else i.e., it indicates only the CSGs going
	 * through suspension.
	 */
	clear_bit(group->csg_nr, scheduler->csgs_events_enable_mask);
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	/* If AS fault detected, terminate the group */
	if (!kctx_as_enabled(group->kctx) || group->faulted)
		term_csg_slot(group);
	else
		suspend_csg_slot(group);
}

static void wait_csg_slots_start(struct kbase_device *kbdev)
{
	u32 num_groups = kbdev->csf.global_iface.group_num;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	long remaining = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	DECLARE_BITMAP(slot_mask, MAX_SUPPORTED_CSGS) = {0};
	u32 i;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	/* extract start slot flags for check */
	for (i = 0; i < num_groups; i++) {
		if (atomic_read(&scheduler->csg_slots[i].state) ==
		    CSG_SLOT_READY2RUN)
			set_bit(i, slot_mask);
	}

	while (!bitmap_empty(slot_mask, MAX_SUPPORTED_CSGS)) {
		DECLARE_BITMAP(changed, MAX_SUPPORTED_CSGS);

		bitmap_copy(changed, slot_mask, MAX_SUPPORTED_CSGS);

		remaining = wait_event_timeout(kbdev->csf.event_wait,
			slots_state_changed(kbdev, changed, csg_slot_running),
			remaining);

		if (likely(remaining)) {
			for_each_set_bit(i, changed, num_groups) {
				struct kbase_queue_group *group =
					scheduler->csg_slots[i].resident_group;

				/* The on slot csg is now running */
				clear_bit(i, slot_mask);
				group->run_state = KBASE_CSF_GROUP_RUNNABLE;
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_RUNNABLE, group,
							 group->run_state);
			}
		} else {
			const int csg_nr = ffs(slot_mask[0]) - 1;
			struct kbase_queue_group *group =
				scheduler->csg_slots[csg_nr].resident_group;
			enum dumpfault_error_type error_type = DF_CSG_START_TIMEOUT;

			dev_err(kbdev->dev,
				"[%llu] Timeout (%d ms) waiting for CSG slots to start, slots: 0x%*pb\n",
				kbase_backend_get_cycle_cnt(kbdev), kbdev->csf.fw_timeout_ms,
				num_groups, slot_mask);
			if (kbase_csf_firmware_ping_wait(kbdev, FW_PING_AFTER_ERROR_TIMEOUT_MS))
				error_type = DF_PING_REQUEST_TIMEOUT;
			schedule_actions_trigger_df(kbdev, group->kctx, error_type);

			if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
				kbase_reset_gpu(kbdev);
			break;
		}
	}
}

/**
 * group_on_slot_is_idle() - Check if the given slot has a CSG-idle state
 *                           flagged after the completion of a CSG status
 *                           update command
 *
 * @kbdev:  Pointer to the GPU device.
 * @slot:   The given slot for checking an occupying resident group's idle
 *          state.
 *
 * This function is called at the start of scheduling tick to check the
 * idle status of a queue group resident on a CSG slot.
 * The caller must make sure the corresponding status update command has
 * been called and completed before checking this status.
 *
 * Return: true if the group resident on slot is idle, otherwise false.
 */
static bool group_on_slot_is_idle(struct kbase_device *kbdev,
				  unsigned long slot)
{
	struct kbase_csf_cmd_stream_group_info *ginfo =
					&kbdev->csf.global_iface.groups[slot];
	bool idle = kbase_csf_firmware_csg_output(ginfo, CSG_STATUS_STATE) &
			CSG_STATUS_STATE_IDLE_MASK;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	return idle;
}

/**
 * slots_update_state_changed() -  Check the handshake state of a subset of
 *                                 command group slots.
 *
 * @kbdev:          The GPU device.
 * @field_mask:     The field mask for checking the state in the csg_req/ack.
 * @slots_mask:     A bit_map specifying the slots to check.
 * @slots_done:     A cleared bit_map for returning the slots that
 *                  have finished update.
 *
 * Checks the state of a subset of slots selected through the slots_mask
 * bit_map. Records which slots' handshake completed and send it back in the
 * slots_done bit_map.
 *
 * Return: true if the slots_done is set for at least one slot.
 *         Otherwise false.
 */
static
bool slots_update_state_changed(struct kbase_device *kbdev, u32 field_mask,
		const unsigned long *slots_mask, unsigned long *slots_done)
{
	u32 num_groups = kbdev->csf.global_iface.group_num;
	bool changed = false;
	u32 i;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	for_each_set_bit(i, slots_mask, num_groups) {
		struct kbase_csf_cmd_stream_group_info const *const ginfo =
					    &kbdev->csf.global_iface.groups[i];
		u32 state = kbase_csf_firmware_csg_input_read(ginfo, CSG_REQ);

		state ^= kbase_csf_firmware_csg_output(ginfo, CSG_ACK);

		if (!(state & field_mask)) {
			set_bit(i, slots_done);
			changed = true;
		}
	}

	return changed;
}

/**
 * wait_csg_slots_handshake_ack - Wait the req/ack handshakes to complete on
 *                                the specified groups.
 *
 * @kbdev:           Pointer to the GPU device.
 * @field_mask:      The field mask for checking the state in the csg_req/ack.
 * @slot_mask:       Bitmap reflecting the slots, the function will modify
 *                   the acknowledged slots by clearing their corresponding
 *                   bits.
 * @wait_in_jiffies: Wait duration in jiffies, controlling the time-out.
 *
 * This function waits for the acknowledgment of the request that have
 * already been placed for the CSG slots by the caller. Currently used for
 * the CSG priority update and status update requests.
 *
 * Return: 0 on all specified slots acknowledged; otherwise -ETIMEDOUT. For
 *         timed out condition with unacknowledged slots, their bits remain
 *         set in the slot_mask.
 */
static int wait_csg_slots_handshake_ack(struct kbase_device *kbdev,
		u32 field_mask, unsigned long *slot_mask, long wait_in_jiffies)
{
	const u32 num_groups = kbdev->csf.global_iface.group_num;
	long remaining = wait_in_jiffies;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	while (!bitmap_empty(slot_mask, num_groups) &&
	       !kbase_reset_gpu_is_active(kbdev)) {
		DECLARE_BITMAP(dones, MAX_SUPPORTED_CSGS) = { 0 };

		remaining = wait_event_timeout(kbdev->csf.event_wait,
				slots_update_state_changed(kbdev, field_mask,
						   slot_mask, dones),
				remaining);

		if (likely(remaining))
			bitmap_andnot(slot_mask, slot_mask, dones, num_groups);
		else {

			/* Timed-out on the wait */
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static void wait_csg_slots_finish_prio_update(struct kbase_device *kbdev)
{
	unsigned long *slot_mask =
			kbdev->csf.scheduler.csg_slots_prio_update;
	long wait_time = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	int ret = wait_csg_slots_handshake_ack(kbdev, CSG_REQ_EP_CFG_MASK,
					       slot_mask, wait_time);

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (unlikely(ret != 0)) {
		const int csg_nr = ffs(slot_mask[0]) - 1;
		struct kbase_queue_group *group =
			kbdev->csf.scheduler.csg_slots[csg_nr].resident_group;
		enum dumpfault_error_type error_type = DF_CSG_EP_CFG_TIMEOUT;

		dev_warn(
			kbdev->dev,
			"[%llu] Timeout (%d ms) on CSG_REQ:EP_CFG, skipping the update wait: slot mask=0x%lx",
			kbase_backend_get_cycle_cnt(kbdev),
			kbdev->csf.fw_timeout_ms,
			slot_mask[0]);
		if (kbase_csf_firmware_ping_wait(kbdev, FW_PING_AFTER_ERROR_TIMEOUT_MS))
			error_type = DF_PING_REQUEST_TIMEOUT;
		schedule_actions_trigger_df(kbdev, group->kctx, error_type);

		/* Timeout could indicate firmware is unresponsive so trigger a GPU reset. */
		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
			kbase_reset_gpu(kbdev);
	}
}

static void report_csg_termination(struct kbase_queue_group *const group)
{
	struct base_gpu_queue_group_error
		err = { .error_type = BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
			.payload = { .fatal_group = {
					     .status = GPU_EXCEPTION_TYPE_SW_FAULT_2,
				     } } };

	kbase_csf_add_group_fatal_error(group, &err);
}

void kbase_csf_scheduler_evict_ctx_slots(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct list_head *evicted_groups)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_queue_group *group;
	u32 num_groups = kbdev->csf.global_iface.group_num;
	u32 slot;
	DECLARE_BITMAP(slot_mask, MAX_SUPPORTED_CSGS) = {0};

	lockdep_assert_held(&kctx->csf.lock);
	mutex_lock(&scheduler->lock);

	/* This code is only called during reset, so we don't wait for the CSG
	 * slots to be stopped
	 */
	WARN_ON(!kbase_reset_gpu_is_active(kbdev));

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_EVICT_CTX_SLOTS_START, kctx, 0u);
	for (slot = 0; slot < num_groups; slot++) {
		group = kbdev->csf.scheduler.csg_slots[slot].resident_group;
		if (group && group->kctx == kctx) {
			bool as_fault;

			dev_dbg(kbdev->dev, "Evicting group [%d] running on slot [%d] due to reset",
				group->handle, group->csg_nr);

			term_csg_slot(group);
			as_fault = cleanup_csg_slot(group);
			/* remove the group from the scheduler list */
			sched_evict_group(group, as_fault, false);
			/* signal Userspace that CSG is being terminated */
			report_csg_termination(group);
			/* return the evicted group to the caller */
			list_add_tail(&group->link, evicted_groups);
			set_bit(slot, slot_mask);
		}
	}

	dev_info(kbdev->dev, "Evicting context %d_%d slots: 0x%*pb\n",
			kctx->tgid, kctx->id, num_groups, slot_mask);

	/* Fatal errors may have been the cause of the GPU reset
	 * taking place, in which case we want to make sure that
	 * we wake up the fatal event queue to notify userspace
	 * only once. Otherwise, we may have duplicate event
	 * notifications between the time the first notification
	 * occurs and the time the GPU is reset.
	 */
	kbase_event_wakeup(kctx);

	mutex_unlock(&scheduler->lock);
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_EVICT_CTX_SLOTS_END, kctx, num_groups);
}

/**
 * scheduler_slot_protm_ack - Acknowledging the protected region requests
 * from the resident group on a given slot.
 *
 * @kbdev:  Pointer to the GPU device.
 * @group:  Pointer to the resident group on the given slot.
 * @slot:   The slot that the given group is actively operating on.
 *
 * The function assumes that the given slot is in stable running state and
 * has already been judged by the caller on that any pending protected region
 * requests of the resident group should be acknowledged.
 *
 * Return: true if the group has pending protm request(s) and is acknowledged.
 *         The caller should arrange to enter the protected mode for servicing
 *         it. Otherwise return false, indicating the group has no pending protm
 *         request.
 */
static bool scheduler_slot_protm_ack(struct kbase_device *const kbdev,
		struct kbase_queue_group *const group,
		const int slot)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	bool protm_ack = false;
	struct kbase_csf_cmd_stream_group_info *ginfo =
		&kbdev->csf.global_iface.groups[slot];
	u32 max_csi;
	int i;

	if (WARN_ON(scheduler->csg_slots[slot].resident_group != group))
		return protm_ack;

	lockdep_assert_held(&scheduler->lock);
	lockdep_assert_held(&group->kctx->kbdev->csf.scheduler.interrupt_lock);

	max_csi = ginfo->stream_num;
	for (i = find_first_bit(group->protm_pending_bitmap, max_csi);
	     i < max_csi;
	     i = find_next_bit(group->protm_pending_bitmap, max_csi, i + 1)) {
		struct kbase_queue *queue = group->bound_queues[i];

		clear_bit(i, group->protm_pending_bitmap);
		KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_PROTM_PEND_CLEAR, group, queue,
					   group->protm_pending_bitmap[0]);

		if (!WARN_ON(!queue) && queue->enabled) {
			struct kbase_csf_cmd_stream_info *stream =
						&ginfo->streams[i];
			u32 cs_protm_ack = kbase_csf_firmware_cs_output(
						stream, CS_ACK) &
						CS_ACK_PROTM_PEND_MASK;
			u32 cs_protm_req = kbase_csf_firmware_cs_input_read(
						stream, CS_REQ) &
						CS_REQ_PROTM_PEND_MASK;

			KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_PROTM_ACK, group,
						   queue, cs_protm_ack ^ cs_protm_req);

			if (cs_protm_ack == cs_protm_req) {
				dev_dbg(kbdev->dev,
					"PROTM-ack already done for queue-%d group-%d slot-%d",
					queue->csi_index, group->handle, slot);
				continue;
			}

			kbase_csf_firmware_cs_input_mask(stream, CS_REQ,
						cs_protm_ack,
						CS_ACK_PROTM_PEND_MASK);
			protm_ack = true;
			dev_dbg(kbdev->dev,
				"PROTM-ack for queue-%d, group-%d slot-%d",
				queue->csi_index, group->handle, slot);
		}
	}

	return protm_ack;
}

/**
 * protm_enter_set_next_pending_seq - Update the scheduler's field of
 * tick_protm_pending_seq to that from the next available on-slot protm
 * pending CSG.
 *
 * @kbdev:     Pointer to the GPU device.
 *
 * If applicable, the function updates the scheduler's tick_protm_pending_seq
 * field from the next available on-slot protm pending CSG. If not, the field
 * is set to KBASEP_TICK_PROTM_PEND_SCAN_SEQ_NR_INVALID.
 */
static void protm_enter_set_next_pending_seq(struct kbase_device *const kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 num_groups = kbdev->csf.global_iface.group_num;
	u32 num_csis = kbdev->csf.global_iface.groups[0].stream_num;
	DECLARE_BITMAP(active_csgs, MAX_SUPPORTED_CSGS) = { 0 };
	u32 i;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	bitmap_xor(active_csgs, scheduler->csg_slots_idle_mask, scheduler->csg_inuse_bitmap,
		   num_groups);
	/* Reset the tick's pending protm seq number to invalid initially */
	scheduler->tick_protm_pending_seq = KBASEP_TICK_PROTM_PEND_SCAN_SEQ_NR_INVALID;
	for_each_set_bit(i, active_csgs, num_groups) {
		struct kbase_queue_group *group = scheduler->csg_slots[i].resident_group;

		/* Set to the next pending protm group's scan_seq_number */
		if ((group != scheduler->active_protm_grp) &&
		    (!bitmap_empty(group->protm_pending_bitmap, num_csis)) &&
		    (group->scan_seq_num < scheduler->tick_protm_pending_seq))
			scheduler->tick_protm_pending_seq = group->scan_seq_num;
	}
}

/**
 * scheduler_group_check_protm_enter - Request the given group to be evaluated
 * for triggering the protected mode.
 *
 * @kbdev:     Pointer to the GPU device.
 * @input_grp: Pointer to the GPU queue group.
 *
 * The function assumes the given group is either an active running group or
 * the scheduler internally maintained field scheduler->top_grp.
 *
 * If the GPU is not already running in protected mode and the input group
 * has protected region requests from its bound queues, the requests are
 * acknowledged and the GPU is instructed to enter the protected mode.
 */
static void scheduler_group_check_protm_enter(struct kbase_device *const kbdev,
				struct kbase_queue_group *const input_grp)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned long flags;
	bool protm_in_use;

	lockdep_assert_held(&scheduler->lock);

	/* This lock is taken to prevent the issuing of MMU command during the
	 * transition to protected mode. This helps avoid the scenario where the
	 * entry to protected mode happens with a memory region being locked and
	 * the same region is then accessed by the GPU in protected mode.
	 */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&scheduler->interrupt_lock, flags);

	/* Check if the previous transition to enter & exit the protected
	 * mode has completed or not.
	 */
	protm_in_use = kbase_csf_scheduler_protected_mode_in_use(kbdev) ||
		       kbdev->protected_mode;
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_PROTM_ENTER_CHECK, input_grp, protm_in_use);

	/* Firmware samples the PROTM_PEND ACK bit for CSs when
	 * Host sends PROTM_ENTER global request. So if PROTM_PEND ACK bit
	 * is set for a CS after Host has sent the PROTM_ENTER
	 * Global request, then there is no guarantee that firmware will
	 * notice that prior to switching to protected mode. And firmware
	 * may not again raise the PROTM_PEND interrupt for that CS
	 * later on. To avoid that uncertainty PROTM_PEND ACK bit
	 * is not set for a CS if the request to enter protected
	 * mode has already been sent. It will be set later (after the exit
	 * from protected mode has taken place) when the group to which
	 * CS is bound becomes the top group.
	 *
	 * The actual decision of entering protected mode is hinging on the
	 * input group is the top priority group, or, in case the previous
	 * top-group is evicted from the scheduler during the tick, its would
	 * be replacement, and that it is currently in a stable state (i.e. the
	 * slot state is running).
	 */
	if (!protm_in_use && !WARN_ON(!input_grp)) {
		const int slot =
			kbase_csf_scheduler_group_get_slot_locked(input_grp);

		/* check the input_grp is running and requesting protected mode
		 */
		if (slot >= 0 &&
		    atomic_read(&scheduler->csg_slots[slot].state) ==
			    CSG_SLOT_RUNNING) {
			if (kctx_as_enabled(input_grp->kctx) &&
			    scheduler_slot_protm_ack(kbdev, input_grp, slot)) {
				int err;

				/* Option of acknowledging to multiple
				 * CSGs from the same kctx is dropped,
				 * after consulting with the
				 * architecture team. See the comment in
				 * GPUCORE-21394.
				 */

				/* Switch to protected mode */
				scheduler->active_protm_grp = input_grp;
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_PROTM_ENTER, input_grp,
							 0u);

				kbase_csf_enter_protected_mode(kbdev);
				/* Set the pending protm seq number to the next one */
				protm_enter_set_next_pending_seq(kbdev);

				spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

				err = kbase_csf_wait_protected_mode_enter(kbdev);
				mutex_unlock(&kbdev->mmu_hw_mutex);

				if (err)
					schedule_actions_trigger_df(kbdev, input_grp->kctx,
							DF_PROTECTED_MODE_ENTRY_FAILURE);

				scheduler->protm_enter_time = ktime_get_raw();

				return;
			}
		}
	}

	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
}

/**
 * scheduler_check_pmode_progress - Check if protected mode execution is progressing
 *
 * @kbdev:     Pointer to the GPU device.
 *
 * This function is called when the GPU is in protected mode.
 *
 * It will check if the time spent in protected mode is less
 * than CSF_SCHED_PROTM_PROGRESS_TIMEOUT. If not, a PROTM_EXIT
 * request is sent to the FW.
 */
static void scheduler_check_pmode_progress(struct kbase_device *kbdev)
{
	u64 protm_spent_time_ms;
	u64 protm_progress_timeout =
		kbase_get_timeout_ms(kbdev, CSF_SCHED_PROTM_PROGRESS_TIMEOUT);
	s64 diff_ms_signed =
		ktime_ms_delta(ktime_get_raw(), kbdev->csf.scheduler.protm_enter_time);

	if (diff_ms_signed < 0)
		return;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	protm_spent_time_ms = (u64)diff_ms_signed;
	if (protm_spent_time_ms < protm_progress_timeout)
		return;

	dev_dbg(kbdev->dev, "Protected mode progress timeout: %llu >= %llu",
		protm_spent_time_ms, protm_progress_timeout);

	/* Prompt the FW to exit protected mode */
	scheduler_force_protm_exit(kbdev);
}

static void scheduler_apply(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	const u32 total_csg_slots = kbdev->csf.global_iface.group_num;
	const u32 available_csg_slots = scheduler->num_csg_slots_for_tick;
	u32 suspend_cnt = 0;
	u32 remain_cnt = 0;
	u32 resident_cnt = 0;
	struct kbase_queue_group *group;
	u32 i;
	u32 spare;

	lockdep_assert_held(&scheduler->lock);

	/* Suspend those resident groups not in the run list */
	for (i = 0; i < total_csg_slots; i++) {
		group = scheduler->csg_slots[i].resident_group;
		if (group) {
			resident_cnt++;
			if (group->prepared_seq_num >= available_csg_slots) {
				suspend_queue_group(group);
				suspend_cnt++;
			} else
				remain_cnt++;
		}
	}

	/* Initialize the remaining available csg slots for the tick/tock */
	scheduler->remaining_tick_slots = available_csg_slots;

	/* If there are spare slots, apply heads in the list */
	spare = (available_csg_slots > resident_cnt) ?
		(available_csg_slots - resident_cnt) : 0;
	while (!list_empty(&scheduler->groups_to_schedule)) {
		group = list_first_entry(&scheduler->groups_to_schedule,
				struct kbase_queue_group,
				link_to_schedule);

		if (kbasep_csf_scheduler_group_is_on_slot_locked(group) &&
		    group->prepared_seq_num < available_csg_slots) {
			/* One of the resident remainders */
			update_csg_slot_priority(group,
					get_slot_priority(group));
		} else if (spare != 0) {
			s8 slot = (s8)find_first_zero_bit(
				     kbdev->csf.scheduler.csg_inuse_bitmap,
				     total_csg_slots);

			if (WARN_ON(slot >= (s8)total_csg_slots))
				break;

			if (!kctx_as_enabled(group->kctx) || group->faulted) {
				/* Drop the head group and continue */
				update_offslot_non_idle_cnt_for_faulty_grp(
					group);
				remove_scheduled_group(kbdev, group);
				continue;
			}
			program_csg_slot(group, slot,
					 get_slot_priority(group));
			if (unlikely(!csg_slot_in_use(kbdev, slot)))
				break;

			spare--;
		} else
			break;

		/* Drop the head csg from the list */
		remove_scheduled_group(kbdev, group);
		if (!WARN_ON(!scheduler->remaining_tick_slots))
			scheduler->remaining_tick_slots--;
	}

	/* Dealing with groups currently going through suspend */
	program_suspending_csg_slots(kbdev);
}

static void scheduler_ctx_scan_groups(struct kbase_device *kbdev,
		struct kbase_context *kctx, int priority)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_queue_group *group;

	lockdep_assert_held(&scheduler->lock);
	lockdep_assert_held(&scheduler->interrupt_lock);
	if (WARN_ON(priority < 0) ||
	    WARN_ON(priority >= KBASE_QUEUE_GROUP_PRIORITY_COUNT))
		return;

	if (!kctx_as_enabled(kctx))
		return;

	list_for_each_entry(group, &kctx->csf.sched.runnable_groups[priority],
			    link) {
		if (WARN_ON(!list_empty(&group->link_to_schedule)))
			/* This would be a bug */
			list_del_init(&group->link_to_schedule);

		if (unlikely(group->faulted))
			continue;

		/* Set the scanout sequence number, starting from 0 */
		group->scan_seq_num = scheduler->csg_scan_count_for_tick++;

		if (scheduler->tick_protm_pending_seq ==
				KBASEP_TICK_PROTM_PEND_SCAN_SEQ_NR_INVALID) {
			if (!bitmap_empty(group->protm_pending_bitmap,
			     kbdev->csf.global_iface.groups[0].stream_num))
				scheduler->tick_protm_pending_seq =
					group->scan_seq_num;
		}

		if (queue_group_idle_locked(group)) {
			if (can_schedule_idle_group(group))
				list_add_tail(&group->link_to_schedule,
					&scheduler->idle_groups_to_schedule);
			continue;
		}

		if (!scheduler->ngrp_to_schedule) {
			/* keep the top csg's origin */
			scheduler->top_ctx = kctx;
			scheduler->top_grp = group;
		}

		list_add_tail(&group->link_to_schedule,
			      &scheduler->groups_to_schedule);
		group->prepared_seq_num = scheduler->ngrp_to_schedule++;

		kctx->csf.sched.ngrp_to_schedule++;
		count_active_address_space(kbdev, kctx);
	}
}

/**
 * scheduler_rotate_groups() - Rotate the runnable queue groups to provide
 *                             fairness of scheduling within a single
 *                             kbase_context.
 *
 * @kbdev:    Pointer to the GPU device.
 *
 * Since only kbase_csf_scheduler's top_grp (i.e. the queue group assigned
 * the highest slot priority) is guaranteed to get the resources that it
 * needs we only rotate the kbase_context corresponding to it -
 * kbase_csf_scheduler's top_ctx.
 *
 * The priority level chosen for rotation is the one containing the previous
 * scheduling cycle's kbase_csf_scheduler's top_grp.
 *
 * In a 'fresh-slice-cycle' this always corresponds to the highest group
 * priority in use by kbase_csf_scheduler's top_ctx. That is, it's the priority
 * level of the previous scheduling cycle's first runnable kbase_context.
 *
 * We choose this priority level because when higher priority work is
 * scheduled, we should always cause the scheduler to run and do a scan. The
 * scan always enumerates the highest priority work first (whether that be
 * based on process priority or group priority), and thus
 * kbase_csf_scheduler's top_grp will point to the first of those high priority
 * groups, which necessarily must be the highest priority group in
 * kbase_csf_scheduler's top_ctx. The fresh-slice-cycle will run later and pick
 * up that group appropriately.
 *
 * If kbase_csf_scheduler's top_grp was instead evicted (and thus is NULL),
 * then no explicit rotation occurs on the next fresh-slice-cycle schedule, but
 * will set up kbase_csf_scheduler's top_ctx again for the next scheduling
 * cycle. Implicitly, a rotation had already occurred by removing
 * the kbase_csf_scheduler's top_grp
 *
 * If kbase_csf_scheduler's top_grp became idle and all other groups belonging
 * to kbase_csf_scheduler's top_grp's priority level in kbase_csf_scheduler's
 * top_ctx are also idle, then the effect of this will be to rotate idle
 * groups, which might not actually become resident in the next
 * scheduling slice. However this is acceptable since a queue group becoming
 * idle is implicitly a rotation (as above with evicted queue groups), as it
 * automatically allows a new queue group to take the maximum slot priority
 * whilst the idle kbase_csf_scheduler's top_grp ends up near the back of
 * the kbase_csf_scheduler's groups_to_schedule list. In this example, it will
 * be for a group in the next lowest priority level or in absence of those the
 * next kbase_context's queue groups.
 */
static void scheduler_rotate_groups(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_context *const top_ctx = scheduler->top_ctx;
	struct kbase_queue_group *const top_grp = scheduler->top_grp;

	lockdep_assert_held(&scheduler->lock);
	if (top_ctx && top_grp) {
		struct list_head *list =
			&top_ctx->csf.sched.runnable_groups[top_grp->priority];

		WARN_ON(top_grp->kctx != top_ctx);
		if (!WARN_ON(list_empty(list))) {
			struct kbase_queue_group *new_head_grp;

			list_move_tail(&top_grp->link, list);
			new_head_grp = (!list_empty(list)) ?
						list_first_entry(list, struct kbase_queue_group, link) :
						NULL;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_RUNNABLE_ROTATE, top_grp,
						 top_ctx->csf.sched.num_runnable_grps);
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_RUNNABLE_HEAD, new_head_grp, 0u);
			dev_dbg(kbdev->dev,
			    "groups rotated for a context, num_runnable_groups: %u\n",
			    scheduler->top_ctx->csf.sched.num_runnable_grps);
		}
	}
}

static void scheduler_rotate_ctxs(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct list_head *list = &scheduler->runnable_kctxs;

	lockdep_assert_held(&scheduler->lock);
	if (scheduler->top_ctx) {
		if (!WARN_ON(list_empty(list))) {
			struct kbase_context *pos;
			bool found = false;

			/* Locate the ctx on the list */
			list_for_each_entry(pos, list, csf.link) {
				if (scheduler->top_ctx == pos) {
					found = true;
					break;
				}
			}

			if (!WARN_ON(!found)) {
				struct kbase_context *new_head_kctx;

				list_move_tail(&pos->csf.link, list);
				KBASE_KTRACE_ADD(kbdev, SCHEDULER_RUNNABLE_KCTX_ROTATE, pos, 0u);
				new_head_kctx = (!list_empty(list)) ?
							list_first_entry(list, struct kbase_context, csf.link) :
							NULL;
				KBASE_KTRACE_ADD(kbdev, SCHEDULER_RUNNABLE_KCTX_HEAD, new_head_kctx,
						 0u);
				dev_dbg(kbdev->dev, "contexts rotated\n");
			}
		}
	}
}

/**
 * scheduler_update_idle_slots_status() - Get the status update for the CSG
 *                       slots for which the IDLE notification was received
 *                        previously.
 *
 * @kbdev:             Pointer to the GPU device.
 * @csg_bitmap:        Bitmap of the CSG slots for which
 *                     the status update request completed successfully.
 * @failed_csg_bitmap: Bitmap of the idle CSG slots for which
 *                     the status update request timedout.
 *
 * This function sends a CSG status update request for all the CSG slots
 * present in the bitmap scheduler->csg_slots_idle_mask. Additionally, if
 * the group's 'reevaluate_idle_status' field is set, the nominally non-idle
 * slots are also included in the status update for a confirmation of their
 * status. The function wait for the status update request to complete and
 * returns the update completed slots bitmap and any timed out idle-flagged
 * slots bitmap.
 *
 * The bits set in the scheduler->csg_slots_idle_mask bitmap are cleared by
 * this function.
 */
static void scheduler_update_idle_slots_status(struct kbase_device *kbdev,
		unsigned long *csg_bitmap, unsigned long *failed_csg_bitmap)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	const u32 num_groups = kbdev->csf.global_iface.group_num;
	struct kbase_csf_global_iface *const global_iface =
						&kbdev->csf.global_iface;
	unsigned long flags, i;
	u32 active_chk = 0;

	lockdep_assert_held(&scheduler->lock);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);

	for_each_set_bit(i, scheduler->csg_inuse_bitmap, num_groups) {
		struct kbase_csf_csg_slot *csg_slot = &scheduler->csg_slots[i];
		struct kbase_queue_group *group = csg_slot->resident_group;
		struct kbase_csf_cmd_stream_group_info *const ginfo =
						&global_iface->groups[i];
		u32 csg_req;
		bool idle_flag;

		if (WARN_ON(!group)) {
			clear_bit(i, scheduler->csg_inuse_bitmap);
			clear_bit(i, scheduler->csg_slots_idle_mask);
			continue;
		}

		idle_flag = test_bit(i, scheduler->csg_slots_idle_mask);
		if (idle_flag || group->reevaluate_idle_status) {
			if (idle_flag) {
#ifdef CONFIG_MALI_BIFROST_DEBUG
				if (!bitmap_empty(group->protm_pending_bitmap,
						  ginfo->stream_num)) {
					dev_warn(kbdev->dev,
						"Idle bit set for group %d of ctx %d_%d on slot %d with pending protm execution",
						group->handle, group->kctx->tgid,
						group->kctx->id, (int)i);
				}
#endif
				clear_bit(i, scheduler->csg_slots_idle_mask);
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_IDLE_CLEAR, group,
							 scheduler->csg_slots_idle_mask[0]);
			} else {
				/* Updates include slots for which reevaluation is needed.
				 * Here one tracks the extra included slots in active_chk.
				 * For protm pending slots, their status of activeness are
				 * assured so no need to request an update.
				 */
				active_chk |= BIT(i);
				group->reevaluate_idle_status = false;
			}

			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_UPDATE_IDLE_SLOT_REQ, group, i);
			csg_req = kbase_csf_firmware_csg_output(ginfo, CSG_ACK);
			csg_req ^= CSG_REQ_STATUS_UPDATE_MASK;
			kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ, csg_req,
							CSG_REQ_STATUS_UPDATE_MASK);

			/* Track the slot update requests in csg_bitmap.
			 * Note, if the scheduler requested extended update, the resulting
			 * csg_bitmap would be the idle_flags + active_chk. Otherwise it's
			 * identical to the idle_flags.
			 */
			set_bit(i, csg_bitmap);
		} else {
			group->run_state = KBASE_CSF_GROUP_RUNNABLE;
		}
	}


	/* The groups are aggregated into a single kernel doorbell request */
	if (!bitmap_empty(csg_bitmap, num_groups)) {
		long wt =
			kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
		u32 db_slots = (u32)csg_bitmap[0];

		kbase_csf_ring_csg_slots_doorbell(kbdev, db_slots);
		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

		if (wait_csg_slots_handshake_ack(kbdev,
				CSG_REQ_STATUS_UPDATE_MASK, csg_bitmap, wt)) {
			const int csg_nr = ffs(csg_bitmap[0]) - 1;
			struct kbase_queue_group *group =
				scheduler->csg_slots[csg_nr].resident_group;

			dev_warn(
				kbdev->dev,
				"[%llu] Timeout (%d ms) on CSG_REQ:STATUS_UPDATE, treat groups as not idle: slot mask=0x%lx",
				kbase_backend_get_cycle_cnt(kbdev),
				kbdev->csf.fw_timeout_ms,
				csg_bitmap[0]);
			schedule_actions_trigger_df(kbdev, group->kctx,
				DF_CSG_STATUS_UPDATE_TIMEOUT);

			/* Store the bitmap of timed out slots */
			bitmap_copy(failed_csg_bitmap, csg_bitmap, num_groups);
			csg_bitmap[0] = ~csg_bitmap[0] & db_slots;

			/* Mask off any failed bit position contributed from active ones, as the
			 * intention is to retain the failed bit pattern contains only those from
			 * idle flags reporting back to the caller. This way, any failed to update
			 * original idle flag would be kept as 'idle' (an informed guess, as the
			 * update did not come to a conclusive result). So will be the failed
			 * active ones be treated as still 'non-idle'. This is for a graceful
			 * handling to the unexpected timeout condition.
			 */
			failed_csg_bitmap[0] &= ~active_chk;

		} else {
			KBASE_KTRACE_ADD(kbdev, SCHEDULER_UPDATE_IDLE_SLOTS_ACK, NULL, db_slots);
			csg_bitmap[0] = db_slots;
		}
	} else {
		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
	}
}

/**
 * scheduler_handle_idle_slots() - Update the idle status of queue groups
 *                    resident on CSG slots for which the
 *                    IDLE notification was received previously.
 *
 * @kbdev:  Pointer to the GPU device.
 *
 * This function is called at the start of scheduling tick/tock to reconfirm
 * the idle status of queue groups resident on CSG slots for
 * which idle notification was received previously, i.e. all the CSG slots
 * present in the bitmap scheduler->csg_slots_idle_mask.
 * The confirmation is done by sending the CSG status update request to the
 * firmware. On completion, the firmware will mark the idleness at the
 * slot's interface CSG_STATUS_STATE register accordingly.
 *
 * The run state of the groups resident on still idle CSG slots is changed to
 * KBASE_CSF_GROUP_IDLE and the bitmap scheduler->csg_slots_idle_mask is
 * updated accordingly.
 * The bits corresponding to slots for which the status update request timedout
 * remain set in scheduler->csg_slots_idle_mask.
 */
static void scheduler_handle_idle_slots(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 num_groups = kbdev->csf.global_iface.group_num;
	unsigned long flags, i;
	DECLARE_BITMAP(csg_bitmap, MAX_SUPPORTED_CSGS) = { 0 };
	DECLARE_BITMAP(failed_csg_bitmap, MAX_SUPPORTED_CSGS) = { 0 };

	lockdep_assert_held(&scheduler->lock);

	scheduler_update_idle_slots_status(kbdev, csg_bitmap,
					   failed_csg_bitmap);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	for_each_set_bit(i, csg_bitmap, num_groups) {
		struct kbase_csf_csg_slot *csg_slot = &scheduler->csg_slots[i];
		struct kbase_queue_group *group = csg_slot->resident_group;

		if (WARN_ON(atomic_read(&csg_slot->state) != CSG_SLOT_RUNNING))
			continue;
		if (WARN_ON(!group))
			continue;
		if (WARN_ON(group->run_state != KBASE_CSF_GROUP_RUNNABLE &&
					group->run_state != KBASE_CSF_GROUP_IDLE))
			continue;
		if (WARN_ON(group->priority >= KBASE_QUEUE_GROUP_PRIORITY_COUNT))
			continue;

		if (group_on_slot_is_idle(kbdev, i)) {
			group->run_state = KBASE_CSF_GROUP_IDLE;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_IDLE, group, group->run_state);
			set_bit(i, scheduler->csg_slots_idle_mask);
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_IDLE_SET,
						 group, scheduler->csg_slots_idle_mask[0]);
		} else {
			group->run_state = KBASE_CSF_GROUP_RUNNABLE;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_RUNNABLE, group,
						 group->run_state);
		}
	}

	bitmap_or(scheduler->csg_slots_idle_mask,
		  scheduler->csg_slots_idle_mask,
		  failed_csg_bitmap, num_groups);
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_HANDLE_IDLE_SLOTS, NULL,
				 scheduler->csg_slots_idle_mask[0]);
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
}

static void scheduler_scan_idle_groups(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_queue_group *group, *n;

	list_for_each_entry_safe(group, n, &scheduler->idle_groups_to_schedule,
				 link_to_schedule) {
		WARN_ON(!can_schedule_idle_group(group));

		if (!scheduler->ngrp_to_schedule) {
			/* keep the top csg's origin */
			scheduler->top_ctx = group->kctx;
			scheduler->top_grp = group;
		}

		group->prepared_seq_num = scheduler->ngrp_to_schedule++;
		list_move_tail(&group->link_to_schedule,
			       &scheduler->groups_to_schedule);

		group->kctx->csf.sched.ngrp_to_schedule++;
		count_active_address_space(kbdev, group->kctx);
	}
}

static void scheduler_rotate(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	/* Dealing with rotation */
	scheduler_rotate_groups(kbdev);
	scheduler_rotate_ctxs(kbdev);
}

static struct kbase_queue_group *get_tock_top_group(
	struct kbase_csf_scheduler *const scheduler)
{
	struct kbase_context *kctx;
	int i;

	lockdep_assert_held(&scheduler->lock);
	for (i = 0; i < KBASE_QUEUE_GROUP_PRIORITY_COUNT; ++i) {
		list_for_each_entry(kctx,
			&scheduler->runnable_kctxs, csf.link) {
			struct kbase_queue_group *group;

			list_for_each_entry(group,
					&kctx->csf.sched.runnable_groups[i],
					link) {
				if (queue_group_idle_locked(group))
					continue;

				return group;
			}
		}
	}

	return NULL;
}

/**
 * suspend_active_groups_on_powerdown() - Suspend active CSG groups upon
 *                                        suspend or GPU IDLE.
 *
 * @kbdev:          Pointer to the device
 * @system_suspend: Flag to indicate it's for system suspend.
 *
 * This function will suspend all active CSG groups upon either
 * system suspend, runtime suspend or GPU IDLE.
 *
 * Return: 0 on success, -1 otherwise.
 */
static int suspend_active_groups_on_powerdown(struct kbase_device *kbdev,
					      bool system_suspend)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	DECLARE_BITMAP(slot_mask, MAX_SUPPORTED_CSGS) = { 0 };

	int ret = suspend_active_queue_groups(kbdev, slot_mask);

	if (unlikely(ret)) {
		const int csg_nr = ffs(slot_mask[0]) - 1;
		struct kbase_queue_group *group =
			scheduler->csg_slots[csg_nr].resident_group;
		enum dumpfault_error_type error_type = DF_CSG_SUSPEND_TIMEOUT;

		/* The suspend of CSGs failed,
		 * trigger the GPU reset to be in a deterministic state.
		 */
		dev_warn(kbdev->dev, "[%llu] Timeout (%d ms) waiting for CSG slots to suspend on power down, slot_mask: 0x%*pb\n",
			 kbase_backend_get_cycle_cnt(kbdev),
			 kbdev->csf.fw_timeout_ms,
			 kbdev->csf.global_iface.group_num, slot_mask);
		if (kbase_csf_firmware_ping_wait(kbdev, FW_PING_AFTER_ERROR_TIMEOUT_MS))
			error_type = DF_PING_REQUEST_TIMEOUT;
		schedule_actions_trigger_df(kbdev, group->kctx, error_type);

		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu(kbdev);

		return -1;
	}

	/* Check if the groups became active whilst the suspend was ongoing,
	 * but only for the case where the system suspend is not in progress
	 */
	if (!system_suspend && atomic_read(&scheduler->non_idle_offslot_grps))
		return -1;

	return 0;
}

/**
 * all_on_slot_groups_remained_idle - Live check for all groups' idleness
 *
 * @kbdev: Pointer to the device.
 *
 * Returns false if any of the queues inside any of the groups that have been
 * assigned a physical CSG slot have work to execute, or have executed work
 * since having received a GPU idle notification. This function is used to
 * handle a rance condition between firmware reporting GPU idle and userspace
 * submitting more work by directly ringing a doorbell.
 *
 * Return: false if any queue inside any resident group has work to be processed
 *         or has processed work since GPU idle event, true otherwise.
 */
static bool all_on_slot_groups_remained_idle(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	/* All CSGs have the same number of CSs */
	size_t const max_streams = kbdev->csf.global_iface.groups[0].stream_num;
	size_t i;

	lockdep_assert_held(&scheduler->lock);
	lockdep_assert_held(&scheduler->interrupt_lock);

	for_each_set_bit(i, scheduler->csg_slots_idle_mask,
			  kbdev->csf.global_iface.group_num) {
		struct kbase_queue_group *const group =
			scheduler->csg_slots[i].resident_group;
		size_t j;

		for (j = 0; j < max_streams; ++j) {
			struct kbase_queue const *const queue =
				group->bound_queues[j];
			u64 const *output_addr;
			u64 cur_extract_ofs;

			if (!queue || !queue->user_io_addr)
				continue;

			output_addr = (u64 const *)(queue->user_io_addr + PAGE_SIZE);
			cur_extract_ofs = output_addr[CS_EXTRACT_LO / sizeof(u64)];
			if (cur_extract_ofs != queue->extract_ofs) {
				/* More work has been executed since the idle
				 * notification.
				 */
				return false;
			}
		}
	}

	return true;
}

static bool scheduler_idle_suspendable(struct kbase_device *kbdev)
{
	bool suspend;
	unsigned long flags;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	if ((scheduler->state == SCHED_SUSPENDED) ||
	    (scheduler->state == SCHED_SLEEPING))
		return false;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	spin_lock(&scheduler->interrupt_lock);

	if (scheduler->fast_gpu_idle_handling) {
		scheduler->fast_gpu_idle_handling = false;

		if (scheduler->total_runnable_grps) {
			suspend = !atomic_read(&scheduler->non_idle_offslot_grps) &&
				  kbase_pm_idle_groups_sched_suspendable(kbdev);
		} else
			suspend = kbase_pm_no_runnables_sched_suspendable(kbdev);
		spin_unlock(&scheduler->interrupt_lock);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		return suspend;
	}

	if (scheduler->total_runnable_grps) {

		/* Check both on-slots and off-slots groups idle status */
		suspend = kbase_csf_scheduler_all_csgs_idle(kbdev) &&
			  !atomic_read(&scheduler->non_idle_offslot_grps) &&
			  kbase_pm_idle_groups_sched_suspendable(kbdev);
	} else
		suspend = kbase_pm_no_runnables_sched_suspendable(kbdev);

	/* Confirm that all groups are actually idle before proceeding with
	 * suspension as groups might potentially become active again without
	 * informing the scheduler in case userspace rings a doorbell directly.
	 */
	if (suspend && (unlikely(atomic_read(&scheduler->gpu_no_longer_idle)) ||
			unlikely(!all_on_slot_groups_remained_idle(kbdev))))
		suspend = false;

	spin_unlock(&scheduler->interrupt_lock);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return suspend;
}

#ifdef KBASE_PM_RUNTIME
/**
 * scheduler_sleep_on_idle - Put the Scheduler in sleeping state on GPU
 *                           becoming idle.
 *
 * @kbdev: Pointer to the device.
 *
 * This function is called on GPU idle notification to trigger the transition of
 * GPU to sleep state, where MCU firmware pauses execution and L2 cache is
 * turned off. Scheduler's state is changed to sleeping and all the active queue
 * groups remain on the CSG slots.
 */
static void scheduler_sleep_on_idle(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	dev_dbg(kbdev->dev,
		"Scheduler to be put to sleep on GPU becoming idle");
	cancel_tick_timer(kbdev);
	scheduler_pm_idle_before_sleep(kbdev);
	scheduler->state = SCHED_SLEEPING;
	KBASE_KTRACE_ADD(kbdev, SCHED_SLEEPING, NULL, scheduler->state);
}
#endif

/**
 * scheduler_suspend_on_idle - Put the Scheduler in suspended state on GPU
 *                             becoming idle.
 *
 * @kbdev: Pointer to the device.
 *
 * This function is called on GPU idle notification to trigger the power down of
 * GPU. Scheduler's state is changed to suspended and all the active queue
 * groups are suspended before halting the MCU firmware.
 *
 * Return: true if scheduler will be suspended or false if suspend is aborted.
 */
static bool scheduler_suspend_on_idle(struct kbase_device *kbdev)
{
	int ret = suspend_active_groups_on_powerdown(kbdev, false);

	if (ret) {
		dev_dbg(kbdev->dev, "Aborting suspend scheduler (grps: %d)",
			atomic_read(
				&kbdev->csf.scheduler.non_idle_offslot_grps));
		/* Bring forward the next tick */
		kbase_csf_scheduler_tick_advance(kbdev);
		return false;
	}

	dev_dbg(kbdev->dev, "Scheduler to be suspended on GPU becoming idle");
	scheduler_suspend(kbdev);
	cancel_tick_timer(kbdev);
	return true;
}

static void gpu_idle_worker(struct work_struct *work)
{
	struct kbase_device *kbdev = container_of(
		work, struct kbase_device, csf.scheduler.gpu_idle_work);
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	bool scheduler_is_idle_suspendable = false;
	bool all_groups_suspended = false;

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_GPU_IDLE_WORKER_START, NULL, 0u);

#define __ENCODE_KTRACE_INFO(reset, idle, all_suspend)                         \
	(((u32)reset) | (((u32)idle) << 4) | (((u32)all_suspend) << 8))

	if (kbase_reset_gpu_try_prevent(kbdev)) {
		dev_warn(kbdev->dev, "Quit idle for failing to prevent gpu reset.\n");
		KBASE_KTRACE_ADD(kbdev, SCHEDULER_GPU_IDLE_WORKER_END, NULL,
				 __ENCODE_KTRACE_INFO(true, false, false));
		return;
	}
	kbase_debug_csf_fault_wait_completion(kbdev);
	mutex_lock(&scheduler->lock);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(scheduler->state == SCHED_BUSY)) {
		mutex_unlock(&scheduler->lock);
		kbase_reset_gpu_allow(kbdev);
		return;
	}
#endif

	scheduler_is_idle_suspendable = scheduler_idle_suspendable(kbdev);
	if (scheduler_is_idle_suspendable) {
		KBASE_KTRACE_ADD(kbdev, SCHEDULER_GPU_IDLE_WORKER_HANDLING_START, NULL,
				 kbase_csf_ktrace_gpu_cycle_cnt(kbdev));
#ifdef KBASE_PM_RUNTIME
		if (kbase_pm_gpu_sleep_allowed(kbdev) &&
		    kbase_csf_scheduler_get_nr_active_csgs(kbdev))
			scheduler_sleep_on_idle(kbdev);
		else
#endif
			all_groups_suspended = scheduler_suspend_on_idle(kbdev);

		KBASE_KTRACE_ADD(kbdev, SCHEDULER_GPU_IDLE_WORKER_HANDLING_END, NULL, 0u);
	}

	mutex_unlock(&scheduler->lock);
	kbase_reset_gpu_allow(kbdev);
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_GPU_IDLE_WORKER_END, NULL,
			 __ENCODE_KTRACE_INFO(false, scheduler_is_idle_suspendable,
					      all_groups_suspended));
#undef __ENCODE_KTRACE_INFO
}

static int scheduler_prepare(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned long flags;
	int i;

	lockdep_assert_held(&scheduler->lock);

	/* Empty the groups_to_schedule */
	while (!list_empty(&scheduler->groups_to_schedule)) {
		struct kbase_queue_group *grp =
			list_first_entry(&scheduler->groups_to_schedule,
					 struct kbase_queue_group,
					 link_to_schedule);

		remove_scheduled_group(kbdev, grp);
	}

	/* Pre-scan init scheduler fields */
	if (WARN_ON(scheduler->ngrp_to_schedule != 0))
		scheduler->ngrp_to_schedule = 0;
	scheduler->top_ctx = NULL;
	scheduler->top_grp = NULL;
	scheduler->csg_scan_count_for_tick = 0;
	WARN_ON(!list_empty(&scheduler->idle_groups_to_schedule));
	scheduler->num_active_address_spaces = 0;
	scheduler->num_csg_slots_for_tick = 0;
	bitmap_zero(scheduler->csg_slots_prio_update, MAX_SUPPORTED_CSGS);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	scheduler->tick_protm_pending_seq =
		KBASEP_TICK_PROTM_PEND_SCAN_SEQ_NR_INVALID;
	/* Scan out to run groups */
	for (i = 0; i < KBASE_QUEUE_GROUP_PRIORITY_COUNT; ++i) {
		struct kbase_context *kctx;

		list_for_each_entry(kctx, &scheduler->runnable_kctxs, csf.link)
			scheduler_ctx_scan_groups(kbdev, kctx, i);
	}
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	/* Update this tick's non-idle groups */
	scheduler->non_idle_scanout_grps = scheduler->ngrp_to_schedule;

	/* Initial number of non-idle off-slot groups, before the scheduler's
	 * scheduler_apply() operation. This gives a sensible start point view
	 * of the tick. It will be subject to up/downs during the scheduler
	 * active phase.
	 */
	atomic_set(&scheduler->non_idle_offslot_grps,
		   scheduler->non_idle_scanout_grps);
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_INC, NULL,
				 scheduler->non_idle_scanout_grps);

	/* Adds those idle but runnable groups to the scanout list */
	scheduler_scan_idle_groups(kbdev);

	WARN_ON(scheduler->csg_scan_count_for_tick < scheduler->ngrp_to_schedule);

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_TOP_GRP, scheduler->top_grp,
			scheduler->num_active_address_spaces |
			(((u64)scheduler->ngrp_to_schedule) << 32));
	set_max_csg_slots(kbdev);
	dev_dbg(kbdev->dev, "prepared groups length: %u, num_active_address_spaces: %u\n",
		scheduler->ngrp_to_schedule, scheduler->num_active_address_spaces);
	return 0;
}

/**
 * keep_lru_on_slots() - Check the condition for LRU is met.
 *
 * @kbdev: Pointer to the device.
 *
 * This function tries to maintain the Last-Recent-Use case on slots, when
 * the scheduler has no non-idle off-slot CSGs for a replacement
 * consideration. This effectively extends the previous scheduling results
 * for the new one. That is, the last recent used CSGs are retained on slots
 * for the new tick/tock action.
 *
 * Return: true for avoiding on-slot CSGs changes (i.e. keep existing LRU),
 *         otherwise false.
 */
static bool keep_lru_on_slots(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	bool keep_lru = false;
	int on_slots = bitmap_weight(scheduler->csg_inuse_bitmap,
				     kbdev->csf.global_iface.group_num);

	lockdep_assert_held(&scheduler->lock);

	if (on_slots && !atomic_read(&scheduler->non_idle_offslot_grps)) {
		unsigned long flags;

		spin_lock_irqsave(&scheduler->interrupt_lock, flags);
		/* All on-slots are idle, no non-idle off-slot CSGs available
		 * for considering a meaningful change. Set keep_lru.
		 */
		keep_lru = kbase_csf_scheduler_all_csgs_idle(kbdev);

		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

		dev_dbg(kbdev->dev, "Keep_LRU: %d, CSGs on-slots: %d\n",
			keep_lru, on_slots);
	}

	return keep_lru;
}

/**
 * prepare_fast_local_tock() - making preparation arrangement for exercizing
 *                             a fast local tock inside scheduling-actions.
 *
 * @kbdev:  Pointer to the GPU device.
 *
 * The function assumes that a scheduling action of firing a fast local tock
 * call (i.e. an equivalent tock action without dropping the lock) is desired
 * if there are idle onslot CSGs. The function updates those affected CSGs'
 * run-state as a preparation. This should only be called from inside the
 * schedule_actions(), where the previous idle-flags are still considered to
 * be reflective, following its earlier idle confirmation operational call,
 * plus some potential newly idle CSGs in the scheduling action committing
 * steps.
 *
 * Return: number of on-slots CSGs that can be considered for replacing.
 */
static int prepare_fast_local_tock(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 num_groups = kbdev->csf.global_iface.group_num;
	unsigned long flags, i;
	DECLARE_BITMAP(csg_bitmap, MAX_SUPPORTED_CSGS) = { 0 };

	lockdep_assert_held(&scheduler->lock);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	bitmap_copy(csg_bitmap, scheduler->csg_slots_idle_mask, num_groups);
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	/* Marking the flagged idle CSGs' run state to IDLE, so
	 * the intended fast local tock can replacing them with off-slots
	 * non-idle CSGs.
	 */
	for_each_set_bit(i, csg_bitmap, num_groups) {
		struct kbase_csf_csg_slot *csg_slot = &scheduler->csg_slots[i];
		struct kbase_queue_group *group = csg_slot->resident_group;

		if (!queue_group_idle_locked(group)) {
			group->run_state = KBASE_CSF_GROUP_IDLE;
			KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_IDLE, group, group->run_state);
		}
	}

	/* Return the number of idle slots for potential replacement */
	return bitmap_weight(csg_bitmap, num_groups);
}

static int wait_csg_slots_suspend(struct kbase_device *kbdev, unsigned long *slot_mask,
				  unsigned int timeout_ms)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	long remaining = kbase_csf_timeout_in_jiffies(timeout_ms);
	u32 num_groups = kbdev->csf.global_iface.group_num;
	int err = 0;
	DECLARE_BITMAP(slot_mask_local, MAX_SUPPORTED_CSGS);

	lockdep_assert_held(&scheduler->lock);

	bitmap_copy(slot_mask_local, slot_mask, MAX_SUPPORTED_CSGS);

	while (!bitmap_empty(slot_mask_local, MAX_SUPPORTED_CSGS) && remaining) {
		DECLARE_BITMAP(changed, MAX_SUPPORTED_CSGS);

		bitmap_copy(changed, slot_mask_local, MAX_SUPPORTED_CSGS);

		remaining = wait_event_timeout(
			kbdev->csf.event_wait,
			slots_state_changed(kbdev, changed, csg_slot_stopped_locked), remaining);

		if (likely(remaining)) {
			u32 i;

			for_each_set_bit(i, changed, num_groups) {
				struct kbase_queue_group *group;

				if (WARN_ON(!csg_slot_stopped_locked(kbdev, (s8)i)))
					continue;

				/* The on slot csg is now stopped */
				clear_bit(i, slot_mask_local);

				group = scheduler->csg_slots[i].resident_group;
				if (likely(group)) {
					/* Only do save/cleanup if the
					 * group is not terminated during
					 * the sleep.
					 */
					save_csg_slot(group);
					if (cleanup_csg_slot(group))
						sched_evict_group(group, true, true);
				}
			}
		} else {
			dev_warn(
				kbdev->dev,
				"[%llu] Suspend request sent on CSG slots 0x%lx timed out for slots 0x%lx",
				kbase_backend_get_cycle_cnt(kbdev), slot_mask[0],
				slot_mask_local[0]);
			/* Return the bitmask of the timed out slots to the caller */
			bitmap_copy(slot_mask, slot_mask_local, MAX_SUPPORTED_CSGS);

			err = -ETIMEDOUT;
		}
	}

	return err;
}

/**
 * evict_lru_or_blocked_csg() - Evict the least-recently-used idle or blocked CSG
 *
 * @kbdev: Pointer to the device
 *
 * Used to allow for speedier starting/resumption of another CSG. The worst-case
 * scenario of the evicted CSG being scheduled next is expected to be rare.
 * Also, the eviction will not be applied if the GPU is running in protected mode.
 * Otherwise the the eviction attempt would force the MCU to quit the execution of
 * the protected mode, and likely re-request to enter it again.
 */
static void evict_lru_or_blocked_csg(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	size_t i;
	struct kbase_queue_group *lru_idle_group = NULL;
	const u32 total_csg_slots = kbdev->csf.global_iface.group_num;
	const bool all_addr_spaces_used = (scheduler->num_active_address_spaces >=
					   (kbdev->nr_hw_address_spaces - NUM_RESERVED_AS_SLOTS));
	u8 as_usage[BASE_MAX_NR_AS] = { 0 };

	lockdep_assert_held(&scheduler->lock);
	if (kbase_csf_scheduler_protected_mode_in_use(kbdev))
		return;

	BUILD_BUG_ON(MAX_SUPPORTED_CSGS > (sizeof(int) * BITS_PER_BYTE));
	if (fls(scheduler->csg_inuse_bitmap[0]) != total_csg_slots)
		return; /* Some CSG slots remain unused */

	if (all_addr_spaces_used) {
		for (i = 0; i != total_csg_slots; ++i) {
			if (scheduler->csg_slots[i].resident_group != NULL)
				as_usage[scheduler->csg_slots[i].resident_group->kctx->as_nr]++;
		}
	}

	for (i = 0; i != total_csg_slots; ++i) {
		struct kbase_queue_group *const group = scheduler->csg_slots[i].resident_group;

		/* We expect that by this point all groups would normally be
		 * assigned a physical CSG slot, but if circumstances have
		 * changed then bail out of this optimisation.
		 */
		if (group == NULL)
			return;

		/* Real-time priority CSGs must be kept on-slot even when
		 * idle.
		 */
		if ((group->run_state == KBASE_CSF_GROUP_IDLE) &&
		    (group->priority != BASE_QUEUE_GROUP_PRIORITY_REALTIME) &&
		    ((lru_idle_group == NULL) ||
		     (lru_idle_group->prepared_seq_num < group->prepared_seq_num))) {
			/* If all address spaces are used, we need to ensure the group does not
			 * share the AS with other active CSGs. Or CSG would be freed without AS
			 * and this optimization would not work.
			 */
			if ((!all_addr_spaces_used) || (as_usage[group->kctx->as_nr] == 1))
				lru_idle_group = group;
		}
	}

	if (lru_idle_group != NULL) {
		unsigned long slot_mask = 1 << lru_idle_group->csg_nr;

		dev_dbg(kbdev->dev, "Suspending LRU idle group %d of context %d_%d on slot %d",
			lru_idle_group->handle, lru_idle_group->kctx->tgid,
			lru_idle_group->kctx->id, lru_idle_group->csg_nr);
		suspend_queue_group(lru_idle_group);
		if (wait_csg_slots_suspend(kbdev, &slot_mask, kbdev->csf.fw_timeout_ms)) {
			enum dumpfault_error_type error_type = DF_CSG_SUSPEND_TIMEOUT;

			dev_warn(
				kbdev->dev,
				"[%llu] LRU idle group %d of context %d_%d failed to suspend on slot %d (timeout %d ms)",
				kbase_backend_get_cycle_cnt(kbdev), lru_idle_group->handle,
				lru_idle_group->kctx->tgid, lru_idle_group->kctx->id,
				lru_idle_group->csg_nr, kbdev->csf.fw_timeout_ms);
			if (kbase_csf_firmware_ping_wait(kbdev, FW_PING_AFTER_ERROR_TIMEOUT_MS))
				error_type = DF_PING_REQUEST_TIMEOUT;
			schedule_actions_trigger_df(kbdev, lru_idle_group->kctx, error_type);
		}
	}
}

static void schedule_actions(struct kbase_device *kbdev, bool is_tick)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned long flags;
	struct kbase_queue_group *protm_grp;
	int ret;
	bool skip_scheduling_actions;
	bool skip_idle_slots_update;
	bool new_protm_top_grp = false;
	int local_tock_slots = 0;

	kbase_reset_gpu_assert_prevented(kbdev);
	lockdep_assert_held(&scheduler->lock);

	ret = kbase_csf_scheduler_wait_mcu_active(kbdev);
	if (ret) {
		dev_err(kbdev->dev,
			"Wait for MCU power on failed on scheduling tick/tock");
		return;
	}

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	skip_idle_slots_update = kbase_csf_scheduler_protected_mode_in_use(kbdev);
	skip_scheduling_actions =
			!skip_idle_slots_update && kbdev->protected_mode;
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	/* Skip scheduling actions as GPU reset hasn't been performed yet to
	 * rectify the anomaly that happened when pmode exit interrupt wasn't
	 * received before the termination of group running in pmode.
	 */
	if (unlikely(skip_scheduling_actions)) {
		dev_info(kbdev->dev,
			 "Scheduling actions skipped due to anomaly in pmode");
		return;
	}

	if (!skip_idle_slots_update) {
		/* Updating on-slot idle CSGs when not in protected mode. */
		scheduler_handle_idle_slots(kbdev);

		/* Determine whether the condition is met for keeping the
		 * Last-Recent-Use. If true, skipping the remaining action
		 * steps and thus extending the previous tick's arrangement,
		 * in particular, no alterations to on-slot CSGs.
		 */
		if (keep_lru_on_slots(kbdev))
			return;
	}

	if (is_tick)
		scheduler_rotate(kbdev);

redo_local_tock:
	scheduler_prepare(kbdev);
	/* Need to specifically enqueue the GPU idle work if there are no groups
	 * to schedule despite the runnable groups. This scenario will happen
	 * if System suspend is done when all groups are idle and and no work
	 * is submitted for the groups after the System resume.
	 */
	if (unlikely(!scheduler->ngrp_to_schedule &&
		     scheduler->total_runnable_grps)) {
		dev_dbg(kbdev->dev, "No groups to schedule in the tick");
		enqueue_gpu_idle_work(scheduler);
		return;
	}
	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	protm_grp = scheduler->active_protm_grp;

	/* Avoid update if the top-group remains unchanged and in protected
	 * mode. For the said case, all the slots update is effectively
	 * competing against the active protected mode group (typically the
	 * top-group). If we update other slots, even on leaving the
	 * top-group slot untouched, the firmware would exit the protected mode
	 * for interacting with the host-driver. After it, as the top-group
	 * would again raise the request for entering protected mode, we would
	 * be actively doing the switching over twice without progressing the
	 * queue jobs.
	 */
	if (protm_grp && scheduler->top_grp == protm_grp) {
		int new_val;

		dev_dbg(kbdev->dev, "Scheduler keep protm exec: group-%d",
			protm_grp->handle);
		new_val = atomic_dec_return(&scheduler->non_idle_offslot_grps);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_DEC, protm_grp,
					 new_val);

		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

		scheduler_check_pmode_progress(kbdev);
	} else if (scheduler->top_grp) {
		if (protm_grp)
			dev_dbg(kbdev->dev, "Scheduler drop protm exec: group-%d",
				protm_grp->handle);

		if (!bitmap_empty(scheduler->top_grp->protm_pending_bitmap,
			     kbdev->csf.global_iface.groups[0].stream_num)) {
			dev_dbg(kbdev->dev, "Scheduler prepare protm exec: group-%d of context %d_%d",
				scheduler->top_grp->handle,
				scheduler->top_grp->kctx->tgid,
				scheduler->top_grp->kctx->id);

			/* When entering protected mode all CSG slots can be occupied
			 * but only the protected mode CSG will be running. Any event
			 * that would trigger the execution of an on-slot idle CSG will
			 * need to be handled by the host during protected mode.
			 */
			new_protm_top_grp = true;
		}

		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

		scheduler_apply(kbdev);

		/* Scheduler is dropping the exec of the previous protm_grp,
		 * Until the protm quit completes, the GPU is effectively
		 * locked in the secure mode.
		 */
		if (protm_grp)
			scheduler_force_protm_exit(kbdev);

		wait_csg_slots_start(kbdev);
		wait_csg_slots_finish_prio_update(kbdev);

		if (new_protm_top_grp) {
			scheduler_group_check_protm_enter(kbdev,
						scheduler->top_grp);
		} else if (!local_tock_slots &&
			   atomic_read(&scheduler->non_idle_offslot_grps)) {
			/* If during the scheduling action, we have off-slot
			 * non-idle CSGs in waiting, if it happens to have
			 * some new idle slots emerging during the committed
			 * action steps, trigger a one-off fast local tock.
			 */
			local_tock_slots = prepare_fast_local_tock(kbdev);

			if (local_tock_slots) {
				dev_dbg(kbdev->dev,
					"In-cycle %d idle slots available\n",
					local_tock_slots);
				goto redo_local_tock;
			}
		}
	} else {
		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
	}

	evict_lru_or_blocked_csg(kbdev);
}

/**
 * can_skip_scheduling() - Check if the scheduling actions can be skipped.
 *
 * @kbdev: Pointer to the device
 *
 * This function is called on a scheduling tick or tock to determine if the
 * scheduling actions can be skipped.
 * If Scheduler is in sleeping state and exit from the sleep state is allowed
 * then activation of MCU will be triggered. The tick or tock work item could
 * have been in flight when the state of Scheduler was changed to sleeping.
 *
 * Return: true if the scheduling actions can be skipped.
 */
static bool can_skip_scheduling(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);

	if (unlikely(!kbase_reset_gpu_is_not_pending(kbdev)))
		return true;

	if (scheduler->state == SCHED_SUSPENDED)
		return true;

#ifdef KBASE_PM_RUNTIME
	if (scheduler->state == SCHED_SLEEPING) {
		unsigned long flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		if (kbdev->pm.backend.exit_gpu_sleep_mode) {
			int ret = scheduler_pm_active_after_sleep(kbdev, &flags);

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			if (!ret) {
				scheduler->state = SCHED_INACTIVE;
				KBASE_KTRACE_ADD(kbdev, SCHED_INACTIVE, NULL, scheduler->state);
				return false;
			}

			dev_info(kbdev->dev,
				 "Skip scheduling due to system suspend");
			return true;
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return true;
	}
#endif

	return false;
}

static void schedule_on_tock(struct work_struct *work)
{
	struct kbase_device *kbdev =
		container_of(work, struct kbase_device, csf.scheduler.tock_work.work);
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	int err;

	err = kbase_reset_gpu_try_prevent(kbdev);
	/* Regardless of whether reset failed or is currently happening, exit
	 * early
	 */
	if (err)
		return;

	kbase_debug_csf_fault_wait_completion(kbdev);
	mutex_lock(&scheduler->lock);
	if (can_skip_scheduling(kbdev))
	{
		atomic_set(&scheduler->pending_tock_work, false);
		goto exit_no_schedule_unlock;
	}

	WARN_ON(!(scheduler->state == SCHED_INACTIVE));
	scheduler->state = SCHED_BUSY;
	KBASE_KTRACE_ADD(kbdev, SCHED_BUSY, NULL, scheduler->state);

	/* Undertaking schedule action steps */
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_TOCK_START, NULL, 0u);
	while (atomic_cmpxchg(&scheduler->pending_tock_work, true, false) == true)
		schedule_actions(kbdev, false);

	/* Record time information on a non-skipped tock */
	scheduler->last_schedule = jiffies;

	scheduler->state = SCHED_INACTIVE;
	KBASE_KTRACE_ADD(kbdev, SCHED_INACTIVE, NULL, scheduler->state);
	if (!scheduler->total_runnable_grps)
		enqueue_gpu_idle_work(scheduler);
	mutex_unlock(&scheduler->lock);
	kbase_reset_gpu_allow(kbdev);

	dev_dbg(kbdev->dev,
		"Waking up for event after schedule-on-tock completes.");
	wake_up_all(&kbdev->csf.event_wait);
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_TOCK_END, NULL, 0u);
	return;

exit_no_schedule_unlock:
	mutex_unlock(&scheduler->lock);
	kbase_reset_gpu_allow(kbdev);
}

static void schedule_on_tick(struct work_struct *work)
{
	struct kbase_device *kbdev =
		container_of(work, struct kbase_device, csf.scheduler.tick_work);
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	int err = kbase_reset_gpu_try_prevent(kbdev);
	/* Regardless of whether reset failed or is currently happening, exit
	 * early
	 */
	if (err)
		return;

	kbase_debug_csf_fault_wait_completion(kbdev);
	mutex_lock(&scheduler->lock);

	WARN_ON(scheduler->tick_timer_active);
	if (can_skip_scheduling(kbdev))
		goto exit_no_schedule_unlock;

	scheduler->state = SCHED_BUSY;
	KBASE_KTRACE_ADD(kbdev, SCHED_BUSY, NULL, scheduler->state);

	/* Undertaking schedule action steps */
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_TICK_START, NULL, scheduler->total_runnable_grps);
	schedule_actions(kbdev, true);

	/* Record time information */
	scheduler->last_schedule = jiffies;

	/* Kicking next scheduling if needed */
	if (likely(scheduler_timer_is_enabled_nolock(kbdev)) &&
			(scheduler->total_runnable_grps > 0)) {
		start_tick_timer(kbdev);
		dev_dbg(kbdev->dev,
			"scheduling for next tick, num_runnable_groups:%u\n",
			scheduler->total_runnable_grps);
	} else if (!scheduler->total_runnable_grps) {
		enqueue_gpu_idle_work(scheduler);
	}

	scheduler->state = SCHED_INACTIVE;
	mutex_unlock(&scheduler->lock);
	KBASE_KTRACE_ADD(kbdev, SCHED_INACTIVE, NULL, scheduler->state);
	kbase_reset_gpu_allow(kbdev);

	dev_dbg(kbdev->dev, "Waking up for event after schedule-on-tick completes.");
	wake_up_all(&kbdev->csf.event_wait);
	KBASE_KTRACE_ADD(kbdev, SCHEDULER_TICK_END, NULL,
			 scheduler->total_runnable_grps);
	return;

exit_no_schedule_unlock:
	mutex_unlock(&scheduler->lock);
	kbase_reset_gpu_allow(kbdev);
}

static int suspend_active_queue_groups(struct kbase_device *kbdev,
				       unsigned long *slot_mask)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	u32 num_groups = kbdev->csf.global_iface.group_num;
	u32 slot_num;
	int ret;

	lockdep_assert_held(&scheduler->lock);

	for (slot_num = 0; slot_num < num_groups; slot_num++) {
		struct kbase_queue_group *group =
			scheduler->csg_slots[slot_num].resident_group;

		if (group) {
			suspend_queue_group(group);
			set_bit(slot_num, slot_mask);
		}
	}

	ret = wait_csg_slots_suspend(kbdev, slot_mask, kbdev->reset_timeout_ms);
	return ret;
}

static int suspend_active_queue_groups_on_reset(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	DECLARE_BITMAP(slot_mask, MAX_SUPPORTED_CSGS) = { 0 };
	int ret;
	int ret2;

	mutex_lock(&scheduler->lock);

	ret = suspend_active_queue_groups(kbdev, slot_mask);

	if (ret) {
		dev_warn(kbdev->dev, "Timeout waiting for CSG slots to suspend before reset, slot_mask: 0x%*pb\n",
			 kbdev->csf.global_iface.group_num, slot_mask);
	}

	/* Need to flush the GPU cache to ensure suspend buffer
	 * contents are not lost on reset of GPU.
	 * Do this even if suspend operation had timed out for some of
	 * the CSG slots.
	 * In case the scheduler already in suspended state, the
	 * cache clean is required as the async reset request from
	 * the debugfs may race against the scheduler suspend operation
	 * due to the extra context ref-count, which prevents the
	 * L2 powering down cache clean operation in the non racing
	 * case.
	 * LSC is being flushed together to cover buslogging usecase,
	 * where GPU reset is done regularly to avoid the log buffer
	 * overflow.
	 */
	kbase_gpu_start_cache_clean(kbdev, GPU_COMMAND_CACHE_CLN_INV_L2_LSC);
	ret2 = kbase_gpu_wait_cache_clean_timeout(kbdev,
			kbdev->reset_timeout_ms);
	if (ret2) {
		dev_warn(kbdev->dev, "[%llu] Timeout waiting for cache clean to complete before reset",
			 kbase_backend_get_cycle_cnt(kbdev));
		if (!ret)
			ret = ret2;
	}

	mutex_unlock(&scheduler->lock);

	return ret;
}

/**
 * scheduler_handle_reset_in_protected_mode() - Update the state of normal mode
 *                                              groups when reset is done during
 *                                              protected mode execution.
 *
 * @kbdev: Pointer to the device.
 *
 * This function is called at the time of GPU reset, before the suspension of
 * queue groups, to handle the case when the reset is getting performed whilst
 * GPU is in protected mode.
 * On entry to protected mode all the groups, except the top group that executes
 * in protected mode, are implicitly suspended by the FW. Thus this function
 * simply marks the normal mode groups as suspended (and cleans up the
 * corresponding CSG slots) to prevent their potential forceful eviction from
 * the Scheduler. So if GPU was in protected mode and there was no fault, then
 * only the protected mode group would be suspended in the regular way post exit
 * from this function. And if GPU was in normal mode, then all on-slot groups
 * will get suspended in the regular way.
 *
 * Return: true if the groups remaining on the CSG slots need to be suspended in
 *         the regular way by sending CSG SUSPEND reqs to FW, otherwise false.
 */
static bool scheduler_handle_reset_in_protected_mode(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 const num_groups = kbdev->csf.global_iface.group_num;
	struct kbase_queue_group *protm_grp;
	bool suspend_on_slot_groups = true;
	bool pmode_active;
	unsigned long flags;
	u32 csg_nr;

	mutex_lock(&scheduler->lock);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	protm_grp = scheduler->active_protm_grp;
	pmode_active = kbdev->protected_mode;

	if (likely(!protm_grp && !pmode_active)) {
		/* Case 1: GPU is not in protected mode or it successfully
		 * exited protected mode. All on-slot groups can be suspended in
		 * the regular way before reset.
		 */
		suspend_on_slot_groups = true;
	} else if (protm_grp && pmode_active) {
		/* Case 2: GPU went successfully into protected mode and hasn't
		 * exited from it yet and the protected mode group is still
		 * active. If there was no fault for the protected mode group
		 * then it can be suspended in the regular way before reset.
		 * The other normal mode on-slot groups were already implicitly
		 * suspended on entry to protected mode so they can be marked as
		 * suspended right away.
		 */
		suspend_on_slot_groups = !protm_grp->faulted;
	} else if (!protm_grp && pmode_active) {
		/* Case 3: GPU went successfully into protected mode and hasn't
		 * exited from it yet but the protected mode group got deleted.
		 * This would have happened if the FW got stuck during protected
		 * mode for some reason (like GPU page fault or some internal
		 * error). In normal cases FW is expected to send the pmode exit
		 * interrupt before it handles the CSG termination request.
		 * The other normal mode on-slot groups would already have been
		 * implicitly suspended on entry to protected mode so they can be
		 * marked as suspended right away.
		 */
		suspend_on_slot_groups = false;
	} else if (protm_grp && !pmode_active) {
		/* Case 4: GPU couldn't successfully enter protected mode, i.e.
		 * PROTM_ENTER request had timed out.
		 * All the on-slot groups need to be suspended in the regular
		 * way before reset.
		 */
		suspend_on_slot_groups = true;
	}

	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	if (likely(!pmode_active))
		goto unlock;

	/* GPU hasn't exited protected mode, so all the on-slot groups barring
	 * the protected mode group can be marked as suspended right away.
	 */
	for (csg_nr = 0; csg_nr < num_groups; csg_nr++) {
		struct kbase_queue_group *const group =
			kbdev->csf.scheduler.csg_slots[csg_nr].resident_group;
		int new_val;

		if (!group || (group == protm_grp))
			continue;

		cleanup_csg_slot(group);
		group->run_state = KBASE_CSF_GROUP_SUSPENDED;
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_SUSPENDED, group, group->run_state);

		/* Simply treat the normal mode groups as non-idle. The tick
		 * scheduled after the reset will re-initialize the counter
		 * anyways.
		 */
		new_val = atomic_inc_return(&scheduler->non_idle_offslot_grps);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_NONIDLE_OFFSLOT_GRP_INC, group, new_val);
	}

unlock:
	mutex_unlock(&scheduler->lock);
	return suspend_on_slot_groups;
}

static void cancel_tick_work(struct kbase_csf_scheduler *const scheduler)
{
	cancel_work_sync(&scheduler->tick_work);
}

static void cancel_tock_work(struct kbase_csf_scheduler *const scheduler)
{
	atomic_set(&scheduler->pending_tock_work, false);
	cancel_delayed_work_sync(&scheduler->tock_work);
}

static void scheduler_inner_reset(struct kbase_device *kbdev)
{
	u32 const num_groups = kbdev->csf.global_iface.group_num;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned long flags;

	WARN_ON(kbase_csf_scheduler_get_nr_active_csgs(kbdev));

	/* Cancel any potential queued delayed work(s) */
	cancel_work_sync(&kbdev->csf.scheduler.gpu_idle_work);
	cancel_tick_timer(kbdev);
	cancel_tick_work(scheduler);
	cancel_tock_work(scheduler);
	cancel_delayed_work_sync(&scheduler->ping_work);

	mutex_lock(&scheduler->lock);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	bitmap_fill(scheduler->csgs_events_enable_mask, MAX_SUPPORTED_CSGS);
	if (scheduler->active_protm_grp)
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_PROTM_EXIT, scheduler->active_protm_grp,
					 0u);
	scheduler->active_protm_grp = NULL;
	memset(kbdev->csf.scheduler.csg_slots, 0,
	       num_groups * sizeof(struct kbase_csf_csg_slot));
	bitmap_zero(kbdev->csf.scheduler.csg_inuse_bitmap, num_groups);
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	scheduler->top_ctx = NULL;
	scheduler->top_grp = NULL;

	KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_TOP_GRP, scheduler->top_grp,
			scheduler->num_active_address_spaces |
			(((u64)scheduler->total_runnable_grps) << 32));

	mutex_unlock(&scheduler->lock);
}

void kbase_csf_scheduler_reset(struct kbase_device *kbdev)
{
	struct kbase_context *kctx;

	WARN_ON(!kbase_reset_gpu_is_active(kbdev));

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_RESET_START, NULL, 0u);

	kbase_debug_csf_fault_wait_completion(kbdev);

	if (scheduler_handle_reset_in_protected_mode(kbdev) &&
	    !suspend_active_queue_groups_on_reset(kbdev)) {
		/* As all groups have been successfully evicted from the CSG
		 * slots, clear out thee scheduler data fields and return
		 */
		scheduler_inner_reset(kbdev);
		return;
	}

	mutex_lock(&kbdev->kctx_list_lock);

	/* The loop to iterate over the kbase contexts is present due to lock
	 * ordering issue between kctx->csf.lock & kbdev->csf.scheduler.lock.
	 * CSF ioctls first take kctx->csf.lock which is context-specific and
	 * then take kbdev->csf.scheduler.lock for global actions like assigning
	 * a CSG slot.
	 * If the lock ordering constraint was not there then could have
	 * directly looped over the active queue groups.
	 */
	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
		/* Firmware reload would reinitialize the CSG & CS interface IO
		 * pages, so just need to internally mark the currently active
		 * queue groups as terminated (similar to the unexpected OoM
		 * event case).
		 * No further work can now get executed for the active groups
		 * (new groups would have to be created to execute work) and
		 * in near future Clients would be duly informed of this
		 * reset. The resources (like User IO pages, GPU queue memory)
		 * allocated for the associated queues would be freed when the
		 * Clients do the teardown when they become aware of the reset.
		 */
		kbase_csf_active_queue_groups_reset(kbdev, kctx);
	}

	mutex_unlock(&kbdev->kctx_list_lock);

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_RESET_END, NULL, 0u);

	/* After queue groups reset, the scheduler data fields clear out */
	scheduler_inner_reset(kbdev);
}

static void firmware_aliveness_monitor(struct work_struct *work)
{
	struct kbase_device *kbdev = container_of(work, struct kbase_device,
					csf.scheduler.ping_work.work);
	int err;

	/* Ensure that reset will not be occurring while this function is being
	 * executed as otherwise calling kbase_reset_gpu when reset is already
	 * occurring is a programming error.
	 *
	 * We must use the 'try' variant as the Reset worker can try to flush
	 * this workqueue, which would otherwise deadlock here if we tried to
	 * wait for the reset (and thus ourselves) to complete.
	 */
	err = kbase_reset_gpu_try_prevent(kbdev);
	if (err) {
		/* It doesn't matter whether the value was -EAGAIN or a fatal
		 * error, just stop processing. In case of -EAGAIN, the Reset
		 * worker will restart the scheduler later to resume ping
		 */
		return;
	}

	mutex_lock(&kbdev->csf.scheduler.lock);

#ifdef CONFIG_MALI_BIFROST_DEBUG
	if (fw_debug) {
		/* ping requests cause distraction in firmware debugging */
		goto exit;
	}
#endif

	if (kbdev->csf.scheduler.state == SCHED_SUSPENDED ||
	    kbdev->csf.scheduler.state == SCHED_SLEEPING)
		goto exit;

	if (kbase_csf_scheduler_get_nr_active_csgs(kbdev) != 1)
		goto exit;

	if (kbase_csf_scheduler_protected_mode_in_use(kbdev))
		goto exit;

	if (kbase_pm_context_active_handle_suspend(kbdev,
			KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE)) {
		/* Suspend pending - no real need to ping */
		goto exit;
	}

	kbase_csf_scheduler_wait_mcu_active(kbdev);

	err = kbase_csf_firmware_ping_wait(kbdev, kbdev->csf.fw_timeout_ms);

	if (err) {
		/* It is acceptable to enqueue a reset whilst we've prevented
		 * them, it will happen after we've allowed them again
		 */
		if (kbase_prepare_to_reset_gpu(
			    kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
			kbase_reset_gpu(kbdev);
	} else if (kbase_csf_scheduler_get_nr_active_csgs(kbdev) == 1) {
		queue_delayed_work(
			system_long_wq, &kbdev->csf.scheduler.ping_work,
			msecs_to_jiffies(kbase_get_timeout_ms(kbdev, CSF_FIRMWARE_PING_TIMEOUT)));
	}

	kbase_pm_context_idle(kbdev);
exit:
	mutex_unlock(&kbdev->csf.scheduler.lock);
	kbase_reset_gpu_allow(kbdev);
}

int kbase_csf_scheduler_group_copy_suspend_buf(struct kbase_queue_group *group,
		struct kbase_suspend_copy_buffer *sus_buf)
{
	struct kbase_context *const kctx = group->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	bool on_slot;
	int err = 0;

	kbase_reset_gpu_assert_prevented(kbdev);
	lockdep_assert_held(&kctx->csf.lock);
	mutex_lock(&scheduler->lock);

	on_slot = kbasep_csf_scheduler_group_is_on_slot_locked(group);

#ifdef KBASE_PM_RUNTIME
	if (on_slot && (scheduler->state == SCHED_SLEEPING)) {
		if (wait_for_scheduler_to_exit_sleep(kbdev)) {
			dev_warn(
				kbdev->dev,
				"Wait for scheduler to exit sleep state timedout when copying suspend buffer for group %d of ctx %d_%d on slot %d",
				group->handle, group->kctx->tgid,
				group->kctx->id, group->csg_nr);

			scheduler_wakeup(kbdev, true);

			/* Wait for MCU firmware to start running */
			if (kbase_csf_scheduler_wait_mcu_active(kbdev))
				dev_warn(
					kbdev->dev,
					"Wait for MCU active failed when copying suspend buffer for group %d of ctx %d_%d on slot %d",
					group->handle, group->kctx->tgid,
					group->kctx->id, group->csg_nr);
		}

		/* Check the group state again as scheduler lock would have been
		 * released when waiting for the exit from SLEEPING state.
		 */
		on_slot = kbasep_csf_scheduler_group_is_on_slot_locked(group);
	}
#endif
	if (on_slot) {
		DECLARE_BITMAP(slot_mask, MAX_SUPPORTED_CSGS) = {0};

		set_bit(kbase_csf_scheduler_group_get_slot(group), slot_mask);

		if (!WARN_ON(scheduler->state == SCHED_SUSPENDED))
			suspend_queue_group(group);
		err = wait_csg_slots_suspend(kbdev, slot_mask,
					     kbdev->csf.fw_timeout_ms);
		if (err) {
			dev_warn(kbdev->dev, "[%llu] Timeout waiting for the group %d to suspend on slot %d",
				 kbase_backend_get_cycle_cnt(kbdev),
				 group->handle, group->csg_nr);
			goto exit;
		}
	}

	if (queue_group_suspended_locked(group)) {
		unsigned int target_page_nr = 0, i = 0;
		u64 offset = sus_buf->offset;
		size_t to_copy = sus_buf->size;
		const u32 csg_suspend_buf_nr_pages =
			PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);

		if (scheduler->state != SCHED_SUSPENDED) {
			/* Similar to the case of HW counters, need to flush
			 * the GPU L2 cache before reading from the suspend buffer
			 * pages as they are mapped and cached on GPU side.
			 * Flushing LSC is not done here, since only the flush of
			 * CSG suspend buffer contents is needed from the L2 cache.
			 */
			kbase_gpu_start_cache_clean(
				kbdev, GPU_COMMAND_CACHE_CLN_INV_L2);
			kbase_gpu_wait_cache_clean(kbdev);
		} else {
			/* Make sure power down transitions have completed,
			 * i.e. L2 has been powered off as that would ensure
			 * its contents are flushed to memory.
			 * This is needed as Scheduler doesn't wait for the
			 * power down to finish.
			 */
			kbase_pm_wait_for_desired_state(kbdev);
		}

		for (i = 0; i < csg_suspend_buf_nr_pages &&
				target_page_nr < sus_buf->nr_pages; i++) {
			struct page *pg =
				as_page(group->normal_suspend_buf.phy[i]);
			void *sus_page = kmap(pg);

			if (sus_page) {
				kbase_sync_single_for_cpu(kbdev,
					kbase_dma_addr(pg),
					PAGE_SIZE, DMA_BIDIRECTIONAL);

				err = kbase_mem_copy_to_pinned_user_pages(
						sus_buf->pages, sus_page,
						&to_copy, sus_buf->nr_pages,
						&target_page_nr, offset);
				kunmap(pg);
				if (err)
					break;
			} else {
				err = -ENOMEM;
				break;
			}
		}
		schedule_in_cycle(group, false);
	} else {
		/* If addr-space fault, the group may have been evicted */
		err = -EIO;
	}

exit:
	mutex_unlock(&scheduler->lock);
	return err;
}

KBASE_EXPORT_TEST_API(kbase_csf_scheduler_group_copy_suspend_buf);

/**
 * group_sync_updated() - Evaluate sync wait condition of all blocked command
 *                        queues of the group.
 *
 * @group: Pointer to the command queue group that has blocked command queue(s)
 *         bound to it.
 *
 * Return: true if sync wait condition is satisfied for at least one blocked
 *         queue of the group.
 */
static bool group_sync_updated(struct kbase_queue_group *group)
{
	bool updated = false;
	int stream;

	/* Groups can also be blocked on-slot during protected mode. */
	WARN_ON(group->run_state != KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC &&
		    group->run_state != KBASE_CSF_GROUP_IDLE);

	for (stream = 0; stream < MAX_SUPPORTED_STREAMS_PER_GROUP; ++stream) {
		struct kbase_queue *const queue = group->bound_queues[stream];

		/* To check the necessity of sync-wait evaluation,
		 * we rely on the cached 'status_wait' instead of reading it
		 * directly from shared memory as the CSG has been already
		 * evicted from the CSG slot, thus this CSG doesn't have
		 * valid information in the shared memory.
		 */
		if (queue && queue->enabled &&
		    CS_STATUS_WAIT_SYNC_WAIT_GET(queue->status_wait))
			if (evaluate_sync_update(queue)) {
				updated = true;
				queue->status_wait = 0;
			}
	}

	return updated;
}

/**
 * scheduler_get_protm_enter_async_group() -  Check if the GPU queue group
 *                          can be now allowed to execute in protected mode.
 *
 * @kbdev:    Pointer to the GPU device.
 * @group:    Pointer to the GPU queue group.
 *
 * This function is called outside the scheduling tick/tock to determine
 * if the given GPU queue group can now execute in protected mode or not.
 * If the group pointer passed is NULL then the evaluation is done for the
 * highest priority group on the scheduler maintained group lists without
 * tick associated rotation actions. This is referred as the 'top-group'
 * in a tock action sense.
 *
 * It returns the same group pointer, that was passed as an argument, if that
 * group matches the highest priority group and has pending protected region
 * requests otherwise NULL is returned.
 *
 * If the group pointer passed is NULL then the internal evaluated highest
 * priority group is returned if that has pending protected region requests
 * otherwise NULL is returned.
 *
 * The evaluated highest priority group may not necessarily be the same as the
 * scheduler->top_grp. This can happen if there is dynamic de-idle update
 * during the tick interval for some on-slots groups that were idle during the
 * scheduler normal scheduling action, where the scheduler->top_grp was set.
 * The recorded scheduler->top_grp is untouched by this evualuation, so will not
 * affect the scheduler context/priority list rotation arrangement.
 *
 * Return: the pointer to queue group that can currently execute in protected
 *         mode or NULL.
 */
static struct kbase_queue_group *scheduler_get_protm_enter_async_group(
		struct kbase_device *const kbdev,
		struct kbase_queue_group *const group)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_queue_group *match_grp, *input_grp;

	lockdep_assert_held(&scheduler->lock);

	if (scheduler->state != SCHED_INACTIVE)
		return NULL;

	match_grp = get_tock_top_group(scheduler);
	input_grp = group ? group : match_grp;

	if (input_grp && (input_grp == match_grp)) {
		struct kbase_csf_cmd_stream_group_info *ginfo =
				&kbdev->csf.global_iface.groups[0];
		unsigned long *pending =
				input_grp->protm_pending_bitmap;
		unsigned long flags;

		spin_lock_irqsave(&scheduler->interrupt_lock, flags);

		if (kbase_csf_scheduler_protected_mode_in_use(kbdev) ||
		    bitmap_empty(pending, ginfo->stream_num))
			input_grp = NULL;

		spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
	} else {
		input_grp = NULL;
	}

	return input_grp;
}

void kbase_csf_scheduler_group_protm_enter(struct kbase_queue_group *group)
{
	struct kbase_device *const kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	int err = kbase_reset_gpu_try_prevent(kbdev);
	/* Regardless of whether reset failed or is currently happening, exit
	 * early
	 */
	if (err)
		return;

	mutex_lock(&scheduler->lock);

	if (group->run_state == KBASE_CSF_GROUP_IDLE)
		group->run_state = KBASE_CSF_GROUP_RUNNABLE;
	/* Check if the group is now eligible for execution in protected mode. */
	if (scheduler_get_protm_enter_async_group(kbdev, group))
		scheduler_group_check_protm_enter(kbdev, group);

	mutex_unlock(&scheduler->lock);
	kbase_reset_gpu_allow(kbdev);
}

/**
 * check_sync_update_for_on_slot_group() - Check the sync wait condition
 *                                         for all the queues bound to
 *                                         the given on-slot group.
 *
 * @group:    Pointer to the on-slot group that requires evaluation.
 *
 * This function is called if the GPU is in protected mode and there are on
 * slot idle groups with higher priority than the active protected mode group
 * or this function is called when CQS object is signaled whilst GPU is in
 * sleep state.
 * This function will evaluate the sync condition, if any, of all the queues
 * bound to the given group.
 *
 * Return: true if the sync condition of at least one queue has been satisfied.
 */
static bool check_sync_update_for_on_slot_group(
		struct kbase_queue_group *group)
{
	struct kbase_device *const kbdev = group->kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler =
				&kbdev->csf.scheduler;
	bool sync_update_done = false;
	int i;

	lockdep_assert_held(&scheduler->lock);

	for (i = 0; i < MAX_SUPPORTED_STREAMS_PER_GROUP; i++) {
		struct kbase_queue *queue = group->bound_queues[i];

		if (queue && queue->enabled && !sync_update_done) {
			struct kbase_csf_cmd_stream_group_info *const ginfo =
				&kbdev->csf.global_iface.groups[group->csg_nr];
			struct kbase_csf_cmd_stream_info *const stream =
				&ginfo->streams[queue->csi_index];
			u32 status = kbase_csf_firmware_cs_output(
					stream, CS_STATUS_WAIT);
			unsigned long flags;

			KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, QUEUE_SYNC_UPDATE_WAIT_STATUS,
						   queue->group, queue, status);

			if (!CS_STATUS_WAIT_SYNC_WAIT_GET(status))
				continue;

			/* Save the information of sync object of the command
			 * queue so the callback function, 'group_sync_updated'
			 * can evaluate the sync object when it gets updated
			 * later.
			 */
			queue->status_wait = status;
			queue->sync_ptr = kbase_csf_firmware_cs_output(
				stream, CS_STATUS_WAIT_SYNC_POINTER_LO);
			queue->sync_ptr |= (u64)kbase_csf_firmware_cs_output(
				stream, CS_STATUS_WAIT_SYNC_POINTER_HI) << 32;
			queue->sync_value = kbase_csf_firmware_cs_output(
				stream, CS_STATUS_WAIT_SYNC_VALUE);
			queue->blocked_reason =
				CS_STATUS_BLOCKED_REASON_REASON_GET(
					kbase_csf_firmware_cs_output(
						stream,
						CS_STATUS_BLOCKED_REASON));

			if (!evaluate_sync_update(queue))
				continue;

			/* Update csg_slots_idle_mask and group's run_state */
			if (group->run_state != KBASE_CSF_GROUP_RUNNABLE) {
				/* Only clear the group's idle flag if it has been dealt
				 * with by the scheduler's tick/tock action, otherwise
				 * leave it untouched.
				 */
				spin_lock_irqsave(&scheduler->interrupt_lock,
						  flags);
				clear_bit((unsigned int)group->csg_nr,
					  scheduler->csg_slots_idle_mask);
				KBASE_KTRACE_ADD_CSF_GRP(
					kbdev, CSG_SLOT_IDLE_CLEAR, group,
					scheduler->csg_slots_idle_mask[0]);
				spin_unlock_irqrestore(
					&scheduler->interrupt_lock, flags);
				/* Request the scheduler to confirm the condition inferred
				 * here inside the protected mode.
				 */
				group->reevaluate_idle_status = true;
				group->run_state = KBASE_CSF_GROUP_RUNNABLE;
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_RUNNABLE, group,
							 group->run_state);
			}

			KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_SYNC_UPDATE_DONE, group, 0u);
			sync_update_done = true;
		}
	}

	return sync_update_done;
}

/**
 * check_sync_update_for_idle_groups_protm() - Check the sync wait condition
 *                                             for the idle groups on slot
 *                                             during protected mode.
 *
 * @kbdev:    Pointer to the GPU device
 *
 * This function checks the gpu queues of all the idle groups on slot during
 * protected mode that has a higher priority than the active protected mode
 * group.
 *
 * Return: true if the sync condition of at least one queue in a group has been
 * satisfied.
 */
static bool check_sync_update_for_idle_groups_protm(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	struct kbase_queue_group *protm_grp;
	bool exit_protm = false;
	unsigned long flags;
	u32 num_groups;
	u32 i;

	lockdep_assert_held(&scheduler->lock);

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	protm_grp = scheduler->active_protm_grp;
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);

	if (!protm_grp)
		return exit_protm;

	num_groups = kbdev->csf.global_iface.group_num;

	for_each_set_bit(i, scheduler->csg_slots_idle_mask, num_groups) {
		struct kbase_csf_csg_slot *csg_slot =
					&scheduler->csg_slots[i];
		struct kbase_queue_group *group = csg_slot->resident_group;

		if (group->scan_seq_num < protm_grp->scan_seq_num) {
			/* If sync update has been performed for the group that
			 * has a higher priority than the protm group, then we
			 * need to exit protected mode.
			 */
			if (check_sync_update_for_on_slot_group(group))
				exit_protm = true;
		}
	}

	return exit_protm;
}

static void check_sync_update_in_sleep_mode(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 const num_groups = kbdev->csf.global_iface.group_num;
	u32 csg_nr;

	lockdep_assert_held(&scheduler->lock);

	for (csg_nr = 0; csg_nr < num_groups; csg_nr++) {
		struct kbase_queue_group *const group =
			kbdev->csf.scheduler.csg_slots[csg_nr].resident_group;

		if (!group)
			continue;

		if (check_sync_update_for_on_slot_group(group)) {
			scheduler_wakeup(kbdev, true);
			return;
		}
	}
}

/**
 * check_group_sync_update_worker() - Check the sync wait condition for all the
 *                                    blocked queue groups
 *
 * @work:    Pointer to the context-specific work item for evaluating the wait
 *           condition for all the queue groups in idle_wait_groups list.
 *
 * This function checks the gpu queues of all the groups present in both
 * idle_wait_groups list of a context and all on slot idle groups (if GPU
 * is in protected mode).
 * If the sync wait condition for at least one queue bound to the group has
 * been satisfied then the group is moved to the per context list of
 * runnable groups so that Scheduler can consider scheduling the group
 * in next tick or exit protected mode.
 */
static void check_group_sync_update_worker(struct work_struct *work)
{
	struct kbase_context *const kctx = container_of(work,
		struct kbase_context, csf.sched.sync_update_work);
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	bool sync_updated = false;

	mutex_lock(&scheduler->lock);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(scheduler->state == SCHED_BUSY)) {
		queue_work(kctx->csf.sched.sync_update_wq,
			&kctx->csf.sched.sync_update_work);
		mutex_unlock(&scheduler->lock);
		return;
	}
#endif

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_GROUP_SYNC_UPDATE_WORKER_START, kctx, 0u);
	if (kctx->csf.sched.num_idle_wait_grps != 0) {
		struct kbase_queue_group *group, *temp;

		list_for_each_entry_safe(group, temp,
				&kctx->csf.sched.idle_wait_groups, link) {
			if (group_sync_updated(group)) {
				sync_updated = true;
				/* Move this group back in to the runnable
				 * groups list of the context.
				 */
				update_idle_suspended_group_state(group);
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, GROUP_SYNC_UPDATE_DONE, group, 0u);
			}
		}
	} else {
		WARN_ON(!list_empty(&kctx->csf.sched.idle_wait_groups));
	}

	if (check_sync_update_for_idle_groups_protm(kbdev)) {
		scheduler_force_protm_exit(kbdev);
		sync_updated = true;
	}

	/* If scheduler is in sleep or suspended state, re-activate it
	 * to serve on-slot CSGs blocked on CQS which has been signaled.
	 */
	if (!sync_updated && (scheduler->state == SCHED_SLEEPING))
		check_sync_update_in_sleep_mode(kbdev);

	KBASE_KTRACE_ADD(kbdev, SCHEDULER_GROUP_SYNC_UPDATE_WORKER_END, kctx, 0u);

	mutex_unlock(&scheduler->lock);
}

static
enum kbase_csf_event_callback_action check_group_sync_update_cb(void *param)
{
	struct kbase_context *const kctx = param;

	KBASE_KTRACE_ADD(kctx->kbdev, SCHEDULER_GROUP_SYNC_UPDATE_EVENT, kctx, 0u);

	queue_work(kctx->csf.sched.sync_update_wq,
		&kctx->csf.sched.sync_update_work);

	return KBASE_CSF_EVENT_CALLBACK_KEEP;
}

int kbase_csf_scheduler_context_init(struct kbase_context *kctx)
{
	int priority;
	int err;

	for (priority = 0; priority < KBASE_QUEUE_GROUP_PRIORITY_COUNT;
	     ++priority) {
		INIT_LIST_HEAD(&kctx->csf.sched.runnable_groups[priority]);
	}

	kctx->csf.sched.num_runnable_grps = 0;
	INIT_LIST_HEAD(&kctx->csf.sched.idle_wait_groups);
	kctx->csf.sched.num_idle_wait_grps = 0;
	kctx->csf.sched.ngrp_to_schedule = 0;

	kctx->csf.sched.sync_update_wq =
		alloc_ordered_workqueue("mali_kbase_csf_sync_update_wq",
			WQ_HIGHPRI);
	if (!kctx->csf.sched.sync_update_wq) {
		dev_err(kctx->kbdev->dev,
			"Failed to initialize scheduler context workqueue");
		return -ENOMEM;
	}

	INIT_WORK(&kctx->csf.sched.sync_update_work,
		check_group_sync_update_worker);

	kbase_csf_tiler_heap_reclaim_ctx_init(kctx);

	err = kbase_csf_event_wait_add(kctx, check_group_sync_update_cb, kctx);

	if (err) {
		dev_err(kctx->kbdev->dev,
			"Failed to register a sync update callback");
		destroy_workqueue(kctx->csf.sched.sync_update_wq);
	}

	return err;
}

void kbase_csf_scheduler_context_term(struct kbase_context *kctx)
{
	kbase_csf_event_wait_remove(kctx, check_group_sync_update_cb, kctx);
	cancel_work_sync(&kctx->csf.sched.sync_update_work);
	destroy_workqueue(kctx->csf.sched.sync_update_wq);
}

int kbase_csf_scheduler_init(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u32 num_groups = kbdev->csf.global_iface.group_num;

	bitmap_zero(scheduler->csg_inuse_bitmap, num_groups);
	bitmap_zero(scheduler->csg_slots_idle_mask, num_groups);

	scheduler->csg_slots = kcalloc(num_groups,
				sizeof(*scheduler->csg_slots), GFP_KERNEL);
	if (!scheduler->csg_slots) {
		dev_err(kbdev->dev,
			"Failed to allocate memory for csg slot status array\n");
		return -ENOMEM;
	}

	return 0;
}

int kbase_csf_scheduler_early_init(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	scheduler->timer_enabled = true;

	scheduler->wq = alloc_ordered_workqueue("csf_scheduler_wq", WQ_HIGHPRI);
	if (!scheduler->wq) {
		dev_err(kbdev->dev, "Failed to allocate scheduler workqueue\n");
		return -ENOMEM;
	}
	scheduler->idle_wq = alloc_ordered_workqueue(
		"csf_scheduler_gpu_idle_wq", WQ_HIGHPRI);
	if (!scheduler->idle_wq) {
		dev_err(kbdev->dev,
			"Failed to allocate GPU idle scheduler workqueue\n");
		destroy_workqueue(kbdev->csf.scheduler.wq);
		return -ENOMEM;
	}

	INIT_WORK(&scheduler->tick_work, schedule_on_tick);
	INIT_DEFERRABLE_WORK(&scheduler->tock_work, schedule_on_tock);
	atomic_set(&scheduler->pending_tock_work, false);

	INIT_DEFERRABLE_WORK(&scheduler->ping_work, firmware_aliveness_monitor);

	mutex_init(&scheduler->lock);
	spin_lock_init(&scheduler->interrupt_lock);

	/* Internal lists */
	INIT_LIST_HEAD(&scheduler->runnable_kctxs);
	INIT_LIST_HEAD(&scheduler->groups_to_schedule);
	INIT_LIST_HEAD(&scheduler->idle_groups_to_schedule);

	BUILD_BUG_ON(MAX_SUPPORTED_CSGS >
		(sizeof(scheduler->csgs_events_enable_mask) * BITS_PER_BYTE));
	bitmap_fill(scheduler->csgs_events_enable_mask, MAX_SUPPORTED_CSGS);
	scheduler->state = SCHED_SUSPENDED;
	KBASE_KTRACE_ADD(kbdev, SCHED_SUSPENDED, NULL, scheduler->state);
	scheduler->pm_active_count = 0;
	scheduler->ngrp_to_schedule = 0;
	scheduler->total_runnable_grps = 0;
	scheduler->top_ctx = NULL;
	scheduler->top_grp = NULL;
	scheduler->last_schedule = 0;
	scheduler->active_protm_grp = NULL;
	scheduler->csg_scheduling_period_ms = CSF_SCHEDULER_TIME_TICK_MS;
	scheduler_doorbell_init(kbdev);

	INIT_WORK(&scheduler->gpu_idle_work, gpu_idle_worker);
	scheduler->fast_gpu_idle_handling = false;
	atomic_set(&scheduler->gpu_no_longer_idle, false);
	atomic_set(&scheduler->non_idle_offslot_grps, 0);

	hrtimer_init(&scheduler->tick_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	scheduler->tick_timer.function = tick_timer_callback;
	scheduler->tick_timer_active = false;

	kbase_csf_tiler_heap_reclaim_mgr_init(kbdev);

	return 0;
}

void kbase_csf_scheduler_term(struct kbase_device *kbdev)
{
	if (kbdev->csf.scheduler.csg_slots) {
		WARN_ON(atomic_read(&kbdev->csf.scheduler.non_idle_offslot_grps));
		/* The unload of Driver can take place only when all contexts have
		 * been terminated. The groups that were not terminated by the User
		 * are terminated on context termination. So no CSGs are expected
		 * to be active at the time of Driver unload.
		 */
		WARN_ON(kbase_csf_scheduler_get_nr_active_csgs(kbdev));
		flush_work(&kbdev->csf.scheduler.gpu_idle_work);
		mutex_lock(&kbdev->csf.scheduler.lock);

		if (kbdev->csf.scheduler.state != SCHED_SUSPENDED) {
			unsigned long flags;
			/* The power policy could prevent the Scheduler from
			 * getting suspended when GPU becomes idle.
			 */
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			WARN_ON(kbase_pm_idle_groups_sched_suspendable(kbdev));
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			scheduler_suspend(kbdev);
		}

		mutex_unlock(&kbdev->csf.scheduler.lock);
		cancel_delayed_work_sync(&kbdev->csf.scheduler.ping_work);
		cancel_tick_timer(kbdev);
		cancel_tick_work(&kbdev->csf.scheduler);
		cancel_tock_work(&kbdev->csf.scheduler);
		kfree(kbdev->csf.scheduler.csg_slots);
		kbdev->csf.scheduler.csg_slots = NULL;
	}
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSF_GROUP_TERMINATED, NULL,
				 kbase_csf_scheduler_get_nr_active_csgs(kbdev));
}

void kbase_csf_scheduler_early_term(struct kbase_device *kbdev)
{
	if (kbdev->csf.scheduler.idle_wq)
		destroy_workqueue(kbdev->csf.scheduler.idle_wq);
	if (kbdev->csf.scheduler.wq)
		destroy_workqueue(kbdev->csf.scheduler.wq);

	kbase_csf_tiler_heap_reclaim_mgr_term(kbdev);
	mutex_destroy(&kbdev->csf.scheduler.lock);
}

/**
 * scheduler_enable_tick_timer_nolock - Enable the scheduler tick timer.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function will restart the scheduler tick so that regular scheduling can
 * be resumed without any explicit trigger (like kicking of GPU queues). This
 * is a variant of kbase_csf_scheduler_enable_tick_timer() that assumes the
 * CSF scheduler lock to already have been held.
 */
static void scheduler_enable_tick_timer_nolock(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (unlikely(!scheduler_timer_is_enabled_nolock(kbdev)))
		return;

	WARN_ON((scheduler->state != SCHED_INACTIVE) &&
		(scheduler->state != SCHED_SUSPENDED) &&
		(scheduler->state != SCHED_SLEEPING));

	if (scheduler->total_runnable_grps > 0) {
		enqueue_tick_work(kbdev);
		dev_dbg(kbdev->dev, "Re-enabling the scheduler timer\n");
	} else if (scheduler->state != SCHED_SUSPENDED) {
		enqueue_gpu_idle_work(scheduler);
	}
}

void kbase_csf_scheduler_enable_tick_timer(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->csf.scheduler.lock);
	scheduler_enable_tick_timer_nolock(kbdev);
	mutex_unlock(&kbdev->csf.scheduler.lock);
}

bool kbase_csf_scheduler_timer_is_enabled(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	bool enabled;

	mutex_lock(&scheduler->lock);
	enabled = scheduler_timer_is_enabled_nolock(kbdev);
	mutex_unlock(&scheduler->lock);

	return enabled;
}

void kbase_csf_scheduler_timer_set_enabled(struct kbase_device *kbdev,
		bool enable)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	bool currently_enabled;

	mutex_lock(&scheduler->lock);

	currently_enabled = scheduler_timer_is_enabled_nolock(kbdev);
	if (currently_enabled && !enable) {
		scheduler->timer_enabled = false;
		cancel_tick_timer(kbdev);
		mutex_unlock(&scheduler->lock);
		/* The non-sync version to cancel the normal work item is not
		 * available, so need to drop the lock before cancellation.
		 */
		cancel_tick_work(scheduler);
		cancel_tock_work(scheduler);
		return;
	}

	if (!currently_enabled && enable) {
		scheduler->timer_enabled = true;

		scheduler_enable_tick_timer_nolock(kbdev);
	}

	mutex_unlock(&scheduler->lock);
}

void kbase_csf_scheduler_kick(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	mutex_lock(&scheduler->lock);

	if (unlikely(scheduler_timer_is_enabled_nolock(kbdev)))
		goto out;

	if (scheduler->total_runnable_grps > 0) {
		enqueue_tick_work(kbdev);
		dev_dbg(kbdev->dev, "Kicking the scheduler manually\n");
	}

out:
	mutex_unlock(&scheduler->lock);
}

int kbase_csf_scheduler_pm_suspend_no_lock(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	int result = 0;

	lockdep_assert_held(&scheduler->lock);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(scheduler->state == SCHED_BUSY))
		return -EBUSY;
#endif

#ifdef KBASE_PM_RUNTIME
	/* If scheduler is in sleeping state, then MCU needs to be activated
	 * to suspend CSGs.
	 */
	if (scheduler->state == SCHED_SLEEPING) {
		dev_info(kbdev->dev, "Activating MCU out of sleep on system suspend");
		result = force_scheduler_to_exit_sleep(kbdev);
		if (result) {
			dev_warn(kbdev->dev, "Scheduler failed to exit from sleep");
			goto exit;
		}
	}
#endif
	if (scheduler->state != SCHED_SUSPENDED) {
		result = suspend_active_groups_on_powerdown(kbdev, true);
		if (result) {
			dev_warn(kbdev->dev, "failed to suspend active groups");
			goto exit;
		} else {
			dev_info(kbdev->dev, "Scheduler PM suspend");
			scheduler_suspend(kbdev);
			cancel_tick_timer(kbdev);
		}
	}

exit:
	return result;
}

int kbase_csf_scheduler_pm_suspend(struct kbase_device *kbdev)
{
	int result = 0;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	/* Cancel any potential queued delayed work(s) */
	cancel_tick_work(scheduler);
	cancel_tock_work(scheduler);

	result = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (result) {
		dev_warn(kbdev->dev, "Stop PM suspending for failing to prevent gpu reset.\n");
		return result;
	}

	mutex_lock(&scheduler->lock);

	result = kbase_csf_scheduler_pm_suspend_no_lock(kbdev);
	mutex_unlock(&scheduler->lock);

	kbase_reset_gpu_allow(kbdev);

	return result;
}
KBASE_EXPORT_TEST_API(kbase_csf_scheduler_pm_suspend);

void kbase_csf_scheduler_pm_resume_no_lock(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->lock);
	if ((scheduler->total_runnable_grps > 0) &&
	    (scheduler->state == SCHED_SUSPENDED)) {
		dev_info(kbdev->dev, "Scheduler PM resume");
		scheduler_wakeup(kbdev, true);
	}
}

void kbase_csf_scheduler_pm_resume(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->csf.scheduler.lock);

	kbase_csf_scheduler_pm_resume_no_lock(kbdev);
	mutex_unlock(&kbdev->csf.scheduler.lock);
}
KBASE_EXPORT_TEST_API(kbase_csf_scheduler_pm_resume);

void kbase_csf_scheduler_pm_active(struct kbase_device *kbdev)
{
	/* Here the lock is taken to synchronize against the runtime suspend
	 * callback function, which may need to wake up the MCU for suspending
	 * the CSGs before powering down the GPU.
	 */
	mutex_lock(&kbdev->csf.scheduler.lock);
	scheduler_pm_active_handle_suspend(kbdev,
			KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE);
	mutex_unlock(&kbdev->csf.scheduler.lock);
}
KBASE_EXPORT_TEST_API(kbase_csf_scheduler_pm_active);

void kbase_csf_scheduler_pm_idle(struct kbase_device *kbdev)
{
	/* Here the lock is taken just to maintain symmetry with
	 * kbase_csf_scheduler_pm_active().
	 */
	mutex_lock(&kbdev->csf.scheduler.lock);
	scheduler_pm_idle(kbdev);
	mutex_unlock(&kbdev->csf.scheduler.lock);
}
KBASE_EXPORT_TEST_API(kbase_csf_scheduler_pm_idle);

int kbase_csf_scheduler_wait_mcu_active(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	unsigned long flags;
	int err;

	kbase_pm_lock(kbdev);
	WARN_ON(!kbdev->pm.active_count);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(!scheduler->pm_active_count);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	kbase_pm_unlock(kbdev);

	kbase_pm_wait_for_poweroff_work_complete(kbdev);

	err = kbase_pm_wait_for_desired_state(kbdev);
	if (!err) {
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		WARN_ON(kbdev->pm.backend.mcu_state != KBASE_MCU_ON);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

	return err;
}
KBASE_EXPORT_TEST_API(kbase_csf_scheduler_wait_mcu_active);

#ifdef KBASE_PM_RUNTIME
int kbase_csf_scheduler_handle_runtime_suspend(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	unsigned long flags;
	int ret;

	dev_dbg(kbdev->dev, "Handling runtime suspend");

	kbase_reset_gpu_assert_prevented(kbdev);
	lockdep_assert_held(&scheduler->lock);
	WARN_ON(scheduler->pm_active_count);

	if (scheduler->state == SCHED_SUSPENDED) {
		WARN_ON(kbdev->pm.backend.gpu_sleep_mode_active);
		return 0;
	}

	ret = suspend_active_groups_on_powerdown(kbdev, false);

	if (ret) {
		dev_dbg(kbdev->dev, "Aborting runtime suspend (grps: %d)",
			 atomic_read(&scheduler->non_idle_offslot_grps));

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbdev->pm.backend.exit_gpu_sleep_mode = true;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		kbase_csf_scheduler_invoke_tick(kbdev);
		return ret;
	}

	scheduler->state = SCHED_SUSPENDED;
	KBASE_KTRACE_ADD(kbdev, SCHED_SUSPENDED, NULL, scheduler->state);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.gpu_sleep_mode_active = false;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	wake_up_all(&kbdev->csf.event_wait);
	return 0;
}

void kbase_csf_scheduler_reval_idleness_post_sleep(struct kbase_device *kbdev)
{
	u32 csg_nr;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(kbdev->pm.backend.mcu_state != KBASE_MCU_IN_SLEEP);

	for (csg_nr = 0; csg_nr < kbdev->csf.global_iface.group_num; csg_nr++) {
		struct kbase_csf_cmd_stream_group_info *ginfo =
			&kbdev->csf.global_iface.groups[csg_nr];
		bool csg_idle;

		if (!kbdev->csf.scheduler.csg_slots[csg_nr].resident_group)
			continue;

		csg_idle =
			kbase_csf_firmware_csg_output(ginfo, CSG_STATUS_STATE) &
			CSG_STATUS_STATE_IDLE_MASK;
		if (!csg_idle) {
			dev_dbg(kbdev->dev,
				"Re-activate Scheduler after MCU sleep");
			kbdev->pm.backend.exit_gpu_sleep_mode = true;
			kbase_csf_scheduler_invoke_tick(kbdev);
			break;
		}
	}
}

void kbase_csf_scheduler_force_sleep(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	mutex_lock(&scheduler->lock);
	if (kbase_pm_gpu_sleep_allowed(kbdev) &&
	    (scheduler->state == SCHED_INACTIVE))
		scheduler_sleep_on_idle(kbdev);
	mutex_unlock(&scheduler->lock);
}
#endif

void kbase_csf_scheduler_force_wakeup(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	mutex_lock(&scheduler->lock);
	scheduler_wakeup(kbdev, true);
	mutex_unlock(&scheduler->lock);
}
