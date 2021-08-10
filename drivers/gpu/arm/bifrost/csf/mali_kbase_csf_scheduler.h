/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_SCHEDULER_H_
#define _KBASE_CSF_SCHEDULER_H_

#include "mali_kbase_csf.h"

/**
 * kbase_csf_scheduler_queue_start() - Enable the running of GPU command queue
 *                                     on firmware.
 *
 * @queue: Pointer to the GPU command queue to be started.
 *
 * This function would enable the start of a CSI, within a
 * CSG, to which the @queue was bound.
 * If the CSG is already scheduled and resident, the CSI will be started
 * right away, otherwise once the group is made resident.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_scheduler_queue_start(struct kbase_queue *queue);

/**
 * kbase_csf_scheduler_queue_stop() - Disable the running of GPU command queue
 *                                    on firmware.
 *
 * @queue: Pointer to the GPU command queue to be stopped.
 *
 * This function would stop the CSI, within a CSG, to which @queue was bound.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_scheduler_queue_stop(struct kbase_queue *queue);

/**
 * kbase_csf_scheduler_group_protm_enter - Handle the protm enter event for the
 *                                         GPU command queue group.
 *
 * @group: The command queue group.
 *
 * This function could request the firmware to enter the protected mode
 * and allow the execution of protected region instructions for all the
 * bound queues of the group that have protm pending bit set in their
 * respective CS_ACK register.
 */
void kbase_csf_scheduler_group_protm_enter(struct kbase_queue_group *group);

/**
 * kbase_csf_scheduler_group_get_slot() - Checks if a queue group is
 *                           programmed on a firmware CSG slot
 *                           and returns the slot number.
 *
 * @group: The command queue group.
 *
 * Return: The slot number, if the group is programmed on a slot.
 *         Otherwise returns a negative number.
 *
 * Note: This function should not be used if the interrupt_lock is held. Use
 * kbase_csf_scheduler_group_get_slot_locked() instead.
 */
int kbase_csf_scheduler_group_get_slot(struct kbase_queue_group *group);

/**
 * kbase_csf_scheduler_group_get_slot_locked() - Checks if a queue group is
 *                           programmed on a firmware CSG slot
 *                           and returns the slot number.
 *
 * @group: The command queue group.
 *
 * Return: The slot number, if the group is programmed on a slot.
 *         Otherwise returns a negative number.
 *
 * Note: Caller must hold the interrupt_lock.
 */
int kbase_csf_scheduler_group_get_slot_locked(struct kbase_queue_group *group);

/**
 * kbase_csf_scheduler_group_events_enabled() - Checks if interrupt events
 *                                     should be handled for a queue group.
 *
 * @kbdev: The device of the group.
 * @group: The queue group.
 *
 * Return: true if interrupt events should be handled.
 *
 * Note: Caller must hold the interrupt_lock.
 */
bool kbase_csf_scheduler_group_events_enabled(struct kbase_device *kbdev,
		struct kbase_queue_group *group);

/**
 * kbase_csf_scheduler_get_group_on_slot()- Gets the queue group that has been
 *                          programmed to a firmware CSG slot.
 *
 * @kbdev: The GPU device.
 * @slot:  The slot for which to get the queue group.
 *
 * Return: Pointer to the programmed queue group.
 *
 * Note: Caller must hold the interrupt_lock.
 */
struct kbase_queue_group *kbase_csf_scheduler_get_group_on_slot(
		struct kbase_device *kbdev, int slot);

/**
 * kbase_csf_scheduler_group_deschedule() - Deschedule a GPU command queue
 *                                          group from the firmware.
 *
 * @group: Pointer to the queue group to be descheduled.
 *
 * This function would disable the scheduling of GPU command queue group on
 * firmware.
 */
void kbase_csf_scheduler_group_deschedule(struct kbase_queue_group *group);

/**
 * kbase_csf_scheduler_evict_ctx_slots() - Evict all GPU command queue groups
 *                                         of a given context that are active
 *                                         running from the firmware.
 *
 * @kbdev:          The GPU device.
 * @kctx:           Kbase context for the evict operation.
 * @evicted_groups: List_head for returning evicted active queue groups.
 *
 * This function would disable the scheduling of GPU command queue groups active
 * on firmware slots from the given Kbase context. The affected groups are
 * added to the supplied list_head argument.
 */
void kbase_csf_scheduler_evict_ctx_slots(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct list_head *evicted_groups);

/**
 * kbase_csf_scheduler_context_init() - Initialize the context-specific part
 *                                      for CSF scheduler.
 *
 * @kctx: Pointer to kbase context that is being created.
 *
 * This function must be called during Kbase context creation.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_scheduler_context_init(struct kbase_context *kctx);

/**
 * kbase_csf_scheduler_init - Initialize the CSF scheduler
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * The scheduler does the arbitration for the CSG slots
 * provided by the firmware between the GPU command queue groups created
 * by the Clients.
 * This function must be called after loading firmware and parsing its capabilities.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_scheduler_init(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_early_init - Early initialization for the CSF scheduler
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Initialize necessary resources such as locks, workqueue for CSF scheduler.
 * This must be called at kbase probe.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_scheduler_early_init(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_context_term() - Terminate the context-specific part
 *                                      for CSF scheduler.
 *
 * @kctx: Pointer to kbase context that is being terminated.
 *
 * This function must be called during Kbase context termination.
 */
void kbase_csf_scheduler_context_term(struct kbase_context *kctx);

/**
 * kbase_csf_scheduler_term - Terminate the CSF scheduler.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This should be called when unload of firmware is done on device
 * termination.
 */
void kbase_csf_scheduler_term(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_early_term - Early termination of the CSF scheduler.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This should be called only when kbase probe fails or gets rmmoded.
 */
void kbase_csf_scheduler_early_term(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_reset - Reset the state of all active GPU command
 *                             queue groups.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function will first iterate through all the active/scheduled GPU
 * command queue groups and suspend them (to avoid losing work for groups
 * that are not stuck). The groups that could not get suspended would be
 * descheduled and marked as terminated (which will then lead to unbinding
 * of all the queues bound to them) and also no more work would be allowed
 * to execute for them.
 *
 * This is similar to the action taken in response to an unexpected OoM event.
 * No explicit re-initialization is done for CSG & CS interface I/O pages;
 * instead, that happens implicitly on firmware reload.
 *
 * Should be called only after initiating the GPU reset.
 */
void kbase_csf_scheduler_reset(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_enable_tick_timer - Enable the scheduler tick timer.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function will restart the scheduler tick so that regular scheduling can
 * be resumed without any explicit trigger (like kicking of GPU queues).
 */
void kbase_csf_scheduler_enable_tick_timer(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_group_copy_suspend_buf - Suspend a queue
 *		group and copy suspend buffer.
 *
 * This function is called to suspend a queue group and copy the suspend_buffer
 * contents to the input buffer provided.
 *
 * @group:	Pointer to the queue group to be suspended.
 * @sus_buf:	Pointer to the structure which contains details of the
 *		user buffer and its kernel pinned pages to which we need to copy
 *		the group suspend buffer.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_scheduler_group_copy_suspend_buf(struct kbase_queue_group *group,
		struct kbase_suspend_copy_buffer *sus_buf);

/**
 * kbase_csf_scheduler_lock - Acquire the global Scheduler lock.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function will take the global scheduler lock, in order to serialize
 * against the Scheduler actions, for access to CS IO pages.
 */
static inline void kbase_csf_scheduler_lock(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->csf.scheduler.lock);
}

/**
 * kbase_csf_scheduler_unlock - Release the global Scheduler lock.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
static inline void kbase_csf_scheduler_unlock(struct kbase_device *kbdev)
{
	mutex_unlock(&kbdev->csf.scheduler.lock);
}

/**
 * kbase_csf_scheduler_spin_lock - Acquire Scheduler interrupt spinlock.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @flags: Pointer to the memory location that would store the previous
 *         interrupt state.
 *
 * This function will take the global scheduler lock, in order to serialize
 * against the Scheduler actions, for access to CS IO pages.
 */
static inline void kbase_csf_scheduler_spin_lock(struct kbase_device *kbdev,
						 unsigned long *flags)
{
	spin_lock_irqsave(&kbdev->csf.scheduler.interrupt_lock, *flags);
}

/**
 * kbase_csf_scheduler_spin_unlock - Release Scheduler interrupt spinlock.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @flags: Previously stored interrupt state when Scheduler interrupt
 *         spinlock was acquired.
 */
static inline void kbase_csf_scheduler_spin_unlock(struct kbase_device *kbdev,
						   unsigned long flags)
{
	spin_unlock_irqrestore(&kbdev->csf.scheduler.interrupt_lock, flags);
}

/**
 * kbase_csf_scheduler_spin_lock_assert_held - Assert if the Scheduler
 *                                          interrupt spinlock is held.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
static inline void
kbase_csf_scheduler_spin_lock_assert_held(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->csf.scheduler.interrupt_lock);
}

/**
 * kbase_csf_scheduler_timer_is_enabled() - Check if the scheduler wakes up
 * automatically for periodic tasks.
 *
 * @kbdev: Pointer to the device
 *
 * Return: true if the scheduler is configured to wake up periodically
 */
bool kbase_csf_scheduler_timer_is_enabled(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_timer_set_enabled() - Enable/disable periodic
 * scheduler tasks.
 *
 * @kbdev:  Pointer to the device
 * @enable: Whether to enable periodic scheduler tasks
 */
void kbase_csf_scheduler_timer_set_enabled(struct kbase_device *kbdev,
		bool enable);

/**
 * kbase_csf_scheduler_kick - Perform pending scheduling tasks once.
 *
 * Note: This function is only effective if the scheduling timer is disabled.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_scheduler_kick(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_protected_mode_in_use() - Check if the scheduler is
 * running with protected mode tasks.
 *
 * @kbdev: Pointer to the device
 *
 * Return: true if the scheduler is running with protected mode tasks
 */
static inline bool kbase_csf_scheduler_protected_mode_in_use(
					struct kbase_device *kbdev)
{
	return (kbdev->csf.scheduler.active_protm_grp != NULL);
}

/**
 * kbase_csf_scheduler_pm_active - Perform scheduler power active operation
 *
 * Note: This function will increase the scheduler's internal pm_active_count
 * value, ensuring that both GPU and MCU are powered for access.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_scheduler_pm_active(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_pm_idle - Perform the scheduler power idle operation
 *
 * Note: This function will decrease the scheduler's internal pm_active_count
 * value. On reaching 0, the MCU and GPU could be powered off.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_scheduler_pm_idle(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_pm_resume - Reactivate the scheduler on system resume
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function will make the scheduler resume the scheduling of queue groups
 * and take the power managemenet reference, if there are any runnable groups.
 */
void kbase_csf_scheduler_pm_resume(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_pm_suspend - Idle the scheduler on system suspend
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function will make the scheduler suspend all the running queue groups
 * and drop its power managemenet reference.
 */
void kbase_csf_scheduler_pm_suspend(struct kbase_device *kbdev);

/**
 * kbase_csf_scheduler_all_csgs_idle() - Check if the scheduler internal
 * runtime used slots are all tagged as idle command queue groups.
 *
 * @kbdev: Pointer to the device
 *
 * Return: true if all the used slots are tagged as idle CSGs.
 */
static inline bool kbase_csf_scheduler_all_csgs_idle(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->csf.scheduler.interrupt_lock);
	return bitmap_equal(kbdev->csf.scheduler.csg_slots_idle_mask,
			    kbdev->csf.scheduler.csg_inuse_bitmap,
			    kbdev->csf.global_iface.group_num);
}

/**
 * kbase_csf_scheduler_advance_tick_nolock() - Advance the scheduling tick
 *
 * @kbdev: Pointer to the device
 *
 * This function advances the scheduling tick by enqueing the tick work item for
 * immediate execution, but only if the tick hrtimer is active. If the timer
 * is inactive then the tick work item is already in flight.
 * The caller must hold the interrupt lock.
 */
static inline void
kbase_csf_scheduler_advance_tick_nolock(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

	lockdep_assert_held(&scheduler->interrupt_lock);

	if (scheduler->tick_timer_active) {
		KBASE_KTRACE_ADD(kbdev, SCHEDULER_ADVANCE_TICK, NULL, 0u);
		scheduler->tick_timer_active = false;
		queue_work(scheduler->wq, &scheduler->tick_work);
	} else {
		KBASE_KTRACE_ADD(kbdev, SCHEDULER_NOADVANCE_TICK, NULL, 0u);
	}
}

/**
 * kbase_csf_scheduler_advance_tick() - Advance the scheduling tick
 *
 * @kbdev: Pointer to the device
 *
 * This function advances the scheduling tick by enqueing the tick work item for
 * immediate execution, but only if the tick hrtimer is active. If the timer
 * is inactive then the tick work item is already in flight.
 */
static inline void kbase_csf_scheduler_advance_tick(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	unsigned long flags;

	spin_lock_irqsave(&scheduler->interrupt_lock, flags);
	kbase_csf_scheduler_advance_tick_nolock(kbdev);
	spin_unlock_irqrestore(&scheduler->interrupt_lock, flags);
}

/**
 * kbase_csf_scheduler_queue_has_trace() - report whether the queue has been
 *                                         configured to operate with the
 *                                         cs_trace feature.
 *
 * @queue: Pointer to the queue.
 *
 * Return: True if the gpu queue is configured to operate with the cs_trace
 *         feature, otherwise false.
 */
static inline bool kbase_csf_scheduler_queue_has_trace(struct kbase_queue *queue)
{
	lockdep_assert_held(&queue->kctx->kbdev->csf.scheduler.lock);
	/* In the current arrangement, it is possible for the context to enable
	 * the cs_trace after some queues have been registered with cs_trace in
	 * disabled state. So each queue has its own enabled/disabled condition.
	 */
	return (queue->trace_buffer_size && queue->trace_buffer_base);
}

#endif /* _KBASE_CSF_SCHEDULER_H_ */
