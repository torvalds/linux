/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_H_
#define _KBASE_CSF_H_

#include "mali_kbase_csf_kcpu.h"
#include "mali_kbase_csf_scheduler.h"
#include "mali_kbase_csf_firmware.h"
#include "mali_kbase_csf_protected_memory.h"

/* Indicate invalid CS h/w interface
 */
#define KBASEP_IF_NR_INVALID ((s8)-1)

/* Indicate invalid CSG number for a GPU command queue group
 */
#define KBASEP_CSG_NR_INVALID ((s8)-1)

/* Indicate invalid user doorbell number for a GPU command queue
 */
#define KBASEP_USER_DB_NR_INVALID ((s8)-1)

#define FIRMWARE_PING_INTERVAL_MS (4000) /* 4 seconds */

#define FIRMWARE_IDLE_HYSTERESIS_TIME_MS (10) /* Default 10 milliseconds */

/**
 * enum kbase_csf_event_callback_action - return type for CSF event callbacks.
 *
 * @KBASE_CSF_EVENT_CALLBACK_FIRST: Never set explicitly.
 * It doesn't correspond to any action or type of event callback.
 *
 * @KBASE_CSF_EVENT_CALLBACK_KEEP: The callback will remain registered.
 *
 * @KBASE_CSF_EVENT_CALLBACK_REMOVE: The callback will be removed
 * immediately upon return.
 *
 * @KBASE_CSF_EVENT_CALLBACK_LAST: Never set explicitly.
 * It doesn't correspond to any action or type of event callback.
 */
enum kbase_csf_event_callback_action {
	KBASE_CSF_EVENT_CALLBACK_FIRST = 0,
	KBASE_CSF_EVENT_CALLBACK_KEEP,
	KBASE_CSF_EVENT_CALLBACK_REMOVE,
	KBASE_CSF_EVENT_CALLBACK_LAST,
};

/**
 * kbase_csf_event_callback_action - type for callback functions to be
 *                                   called upon CSF events.
 *
 * This is the type of callback functions that can be registered
 * for CSF events. These function calls shall be triggered by any call
 * to kbase_csf_event_signal.
 *
 * @param:   Generic parameter to pass to the callback function.
 *
 * Return: KBASE_CSF_EVENT_CALLBACK_KEEP if the callback should remain
 * registered, or KBASE_CSF_EVENT_CALLBACK_REMOVE if it should be removed.
 */
typedef enum kbase_csf_event_callback_action kbase_csf_event_callback(void *param);

/**
 * kbase_csf_event_wait_add - Add a CSF event callback
 *
 * This function adds an event callback to the list of CSF event callbacks
 * belonging to a given Kbase context, to be triggered when a CSF event is
 * signalled by kbase_csf_event_signal.
 *
 * @kctx:      The Kbase context the @callback should be registered to.
 * @callback:  The callback function to register.
 * @param:     Custom parameter to be passed to the @callback function.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_event_wait_add(struct kbase_context *kctx,
		kbase_csf_event_callback *callback, void *param);

/**
 * kbase_csf_event_wait_remove - Remove a CSF event callback
 *
 * This function removes an event callback from the list of CSF event callbacks
 * belonging to a given Kbase context.
 *
 * @kctx:      The kbase context the @callback should be removed from.
 * @callback:  The callback function to remove.
 * @param:     Custom parameter that would have been passed to the @p callback
 *             function.
 */
void kbase_csf_event_wait_remove(struct kbase_context *kctx,
		kbase_csf_event_callback *callback, void *param);

/**
 * kbase_csf_event_wait_remove_all - Removes all CSF event callbacks
 *
 * This function empties the list of CSF event callbacks belonging to a given
 * Kbase context.
 *
 * @kctx:  The kbase context for which CSF event callbacks have to be removed.
 */
void kbase_csf_event_wait_remove_all(struct kbase_context *kctx);

/**
 * kbase_csf_read_error - Read CS fatal error
 *
 * This function takes the CS fatal error from context's ordered
 * error_list, copies its contents to @event_data.
 *
 * @kctx:       The kbase context to read fatal error from
 * @event_data: Caller-provided buffer to copy the fatal error to
 *
 * Return: true if fatal error is read successfully.
 */
bool kbase_csf_read_error(struct kbase_context *kctx,
		struct base_csf_notification *event_data);

/**
 * kbase_csf_error_pending - Check whether fatal error is pending
 *
 * @kctx:  The kbase context to check fatal error upon.
 *
 * Return: true if fatal error is pending.
 */
bool kbase_csf_error_pending(struct kbase_context *kctx);

/**
 * kbase_csf_event_signal - Signal a CSF event
 *
 * This function triggers all the CSF event callbacks that are registered to
 * a given Kbase context, and also signals the event handling thread of
 * userspace driver waiting for the CSF event.
 *
 * @kctx:  The kbase context whose CSF event callbacks shall be triggered.
 * @notify_gpu: Flag to indicate if CSF firmware should be notified of the
 *              signaling of event that happened on the Driver side, either
 *              the signal came from userspace or from kcpu queues.
 */
void kbase_csf_event_signal(struct kbase_context *kctx, bool notify_gpu);

static inline void kbase_csf_event_signal_notify_gpu(struct kbase_context *kctx)
{
	kbase_csf_event_signal(kctx, true);
}

static inline void kbase_csf_event_signal_cpu_only(struct kbase_context *kctx)
{
	kbase_csf_event_signal(kctx, false);
}

/**
 * kbase_csf_ctx_init - Initialize the CSF interface for a GPU address space.
 *
 * @kctx:	Pointer to the kbase context which is being initialized.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_csf_ctx_init(struct kbase_context *kctx);

/**
 * kbase_csf_ctx_handle_fault - Terminate queue groups & notify fault upon
 *                              GPU bus fault, MMU page fault or similar.
 *
 * This function terminates all GPU command queue groups in the context and
 * notifies the event notification thread of the fault.
 *
 * @kctx:       Pointer to faulty kbase context.
 * @fault:      Pointer to the fault.
 */
void kbase_csf_ctx_handle_fault(struct kbase_context *kctx,
		struct kbase_fault *fault);

/**
 * kbase_csf_ctx_term - Terminate the CSF interface for a GPU address space.
 *
 * This function terminates any remaining CSGs and CSs which weren't destroyed
 * before context termination.
 *
 * @kctx:	Pointer to the kbase context which is being terminated.
 */
void kbase_csf_ctx_term(struct kbase_context *kctx);

/**
 * kbase_csf_queue_register - Register a GPU command queue.
 *
 * @kctx:	Pointer to the kbase context within which the
 *		queue is to be registered.
 * @reg:	Pointer to the structure which contains details of the
 *		queue to be registered within the provided
 *		context.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_queue_register(struct kbase_context *kctx,
			     struct kbase_ioctl_cs_queue_register *reg);

/**
 * kbase_csf_queue_register_ex - Register a GPU command queue with
 *                               extended format.
 *
 * @kctx:	Pointer to the kbase context within which the
 *		queue is to be registered.
 * @reg:	Pointer to the structure which contains details of the
 *		queue to be registered within the provided
 *		context, together with the extended parameter fields
 *              for supporting cs trace command.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_queue_register_ex(struct kbase_context *kctx,
			     struct kbase_ioctl_cs_queue_register_ex *reg);

/**
 * kbase_csf_queue_terminate - Terminate a GPU command queue.
 *
 * @kctx:	Pointer to the kbase context within which the
 *		queue is to be terminated.
 * @term:	Pointer to the structure which identifies which
 *		queue is to be terminated.
 */
void kbase_csf_queue_terminate(struct kbase_context *kctx,
			      struct kbase_ioctl_cs_queue_terminate *term);

/**
 * kbase_csf_alloc_command_stream_user_pages - Allocate resources for a
 *                                             GPU command queue.
 *
 * This function allocates a pair of User mode input/output pages for a
 * GPU command queue and maps them in the shared interface segment of MCU
 * firmware address space. Also reserves a hardware doorbell page for the queue.
 *
 * @kctx:	Pointer to the kbase context within which the resources
 *		for the queue are being allocated.
 * @queue:	Pointer to the queue for which to allocate resources.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_alloc_command_stream_user_pages(struct kbase_context *kctx,
			struct kbase_queue *queue);

/**
 * kbase_csf_queue_bind - Bind a GPU command queue to a queue group.
 *
 * @kctx:	The kbase context.
 * @bind:	Pointer to the union which specifies a queue group and a
 *		queue to be bound to that group.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_queue_bind(struct kbase_context *kctx,
			 union kbase_ioctl_cs_queue_bind *bind);

/**
 * kbase_csf_queue_unbind - Unbind a GPU command queue from a queue group
 *			    to which it has been bound and free
 *			    resources allocated for this queue if there
 *			    are any.
 *
 * @queue:	Pointer to queue to be unbound.
 */
void kbase_csf_queue_unbind(struct kbase_queue *queue);

/**
 * kbase_csf_queue_unbind_stopped - Unbind a GPU command queue in the case
 *                                  where it was never started.
 * @queue:      Pointer to queue to be unbound.
 *
 * Variant of kbase_csf_queue_unbind() for use on error paths for cleaning up
 * queues that failed to fully bind.
 */
void kbase_csf_queue_unbind_stopped(struct kbase_queue *queue);

/**
 * kbase_csf_queue_kick - Schedule a GPU command queue on the firmware
 *
 * @kctx:	The kbase context.
 * @kick:	Pointer to the struct which specifies the queue
 *		that needs to be scheduled.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_queue_kick(struct kbase_context *kctx,
			 struct kbase_ioctl_cs_queue_kick *kick);

/**
 * kbase_csf_queue_group_handle_is_valid - Find if the given queue group handle
 *                                         is valid.
 *
 * This function is used to determine if the queue group handle is valid.
 *
 * @kctx:		The kbase context under which the queue group exists.
 * @group_handle:	Handle for the group which uniquely identifies it within
 *			the context with which it was created.
 *
 * Return:		0 on success, or negative on failure.
 */
int kbase_csf_queue_group_handle_is_valid(struct kbase_context *kctx,
	u8 group_handle);

/**
 * kbase_csf_queue_group_create - Create a GPU command queue group.
 *
 * @kctx:	Pointer to the kbase context within which the
 *		queue group is to be created.
 * @create:	Pointer to the structure which contains details of the
 *		queue group which is to be created within the
 *		provided kbase context.
 *
 * Return:	0 on success, or negative on failure.
 */
int kbase_csf_queue_group_create(struct kbase_context *kctx,
	union kbase_ioctl_cs_queue_group_create *create);

/**
 * kbase_csf_queue_group_terminate - Terminate a GPU command queue group.
 *
 * @kctx:		Pointer to the kbase context within which the
 *			queue group is to be terminated.
 * @group_handle:	Pointer to the structure which identifies the queue
 *			group which is to be terminated.
 */
void kbase_csf_queue_group_terminate(struct kbase_context *kctx,
	u8 group_handle);

/**
 * kbase_csf_term_descheduled_queue_group - Terminate a GPU command queue
 *                                          group that is not operational
 *                                          inside the scheduler.
 *
 * @group:	Pointer to the structure which identifies the queue
 *		group to be terminated. The function assumes that the caller
 *		is sure that the given group is not operational inside the
 *		scheduler. If in doubt, use its alternative:
 *		@ref kbase_csf_queue_group_terminate().
 */
void kbase_csf_term_descheduled_queue_group(struct kbase_queue_group *group);

/**
 * kbase_csf_queue_group_suspend - Suspend a GPU command queue group
 *
 * This function is used to suspend a queue group and copy the suspend buffer.
 *
 * @kctx:		The kbase context for which the queue group is to be
 *			suspended.
 * @sus_buf:		Pointer to the structure which contains details of the
 *			user buffer and its kernel pinned pages.
 * @group_handle:	Handle for the group which uniquely identifies it within
 *			the context within which it was created.
 *
 * Return:		0 on success or negative value if failed to suspend
 *			queue group and copy suspend buffer contents.
 */
int kbase_csf_queue_group_suspend(struct kbase_context *kctx,
	struct kbase_suspend_copy_buffer *sus_buf, u8 group_handle);

/**
 * kbase_csf_add_group_fatal_error - Report a fatal group error to userspace
 *
 * @group:       GPU command queue group.
 * @err_payload: Error payload to report.
 */
void kbase_csf_add_group_fatal_error(
	struct kbase_queue_group *const group,
	struct base_gpu_queue_group_error const *const err_payload);

/**
 * kbase_csf_interrupt - Handle interrupts issued by CSF firmware.
 *
 * @kbdev: The kbase device to handle an IRQ for
 * @val:   The value of JOB IRQ status register which triggered the interrupt
 */
void kbase_csf_interrupt(struct kbase_device *kbdev, u32 val);

/**
 * kbase_csf_doorbell_mapping_init - Initialize the fields that facilitates
 *                                   the update of userspace mapping of HW
 *                                   doorbell page.
 *
 * The function creates a file and allocates a dummy page to facilitate the
 * update of userspace mapping to point to the dummy page instead of the real
 * HW doorbell page after the suspend of queue group.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_doorbell_mapping_init(struct kbase_device *kbdev);

/**
 * kbase_csf_doorbell_mapping_term - Free the dummy page & close the file used
 *                         to update the userspace mapping of HW doorbell page
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_doorbell_mapping_term(struct kbase_device *kbdev);

/**
 * kbase_csf_setup_dummy_user_reg_page - Setup the dummy page that is accessed
 *                                       instead of the User register page after
 *                                       the GPU power down.
 *
 * The function allocates a dummy page which is used to replace the User
 * register page in the userspace mapping after the power down of GPU.
 * On the power up of GPU, the mapping is updated to point to the real
 * User register page. The mapping is used to allow access to LATEST_FLUSH
 * register from userspace.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_setup_dummy_user_reg_page(struct kbase_device *kbdev);

/**
 * kbase_csf_free_dummy_user_reg_page - Free the dummy page that was used
 *                                 used to replace the User register page
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_free_dummy_user_reg_page(struct kbase_device *kbdev);

/**
 * kbase_csf_ring_csg_doorbell - ring the doorbell for a CSG interface.
 *
 * The function kicks a notification on the CSG interface to firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @slot: Index of CSG interface for ringing the door-bell.
 */
void kbase_csf_ring_csg_doorbell(struct kbase_device *kbdev, int slot);

/**
 * kbase_csf_ring_csg_slots_doorbell - ring the doorbell for a set of CSG
 *                                     interfaces.
 *
 * The function kicks a notification on a set of CSG interfaces to firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @slot_bitmap: bitmap for the given slots, slot-0 on bit-0, etc.
 */
void kbase_csf_ring_csg_slots_doorbell(struct kbase_device *kbdev,
				       u32 slot_bitmap);

/**
 * kbase_csf_ring_cs_kernel_doorbell - ring the kernel doorbell for a CSI
 *                                     assigned to a GPU queue
 *
 * The function sends a doorbell interrupt notification to the firmware for
 * a CSI assigned to a GPU queue.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @csi_index: ID of the CSI assigned to the GPU queue.
 * @csg_nr:    Index of the CSG slot assigned to the queue
 *             group to which the GPU queue is bound.
 * @ring_csg_doorbell: Flag to indicate if the CSG doorbell needs to be rung
 *                     after updating the CSG_DB_REQ. So if this flag is false
 *                     the doorbell interrupt will not be sent to FW.
 *                     The flag is supposed be false only when the input page
 *                     for bound GPU queues is programmed at the time of
 *                     starting/resuming the group on a CSG slot.
 */
void kbase_csf_ring_cs_kernel_doorbell(struct kbase_device *kbdev,
				       int csi_index, int csg_nr,
				       bool ring_csg_doorbell);

/**
 * kbase_csf_ring_cs_user_doorbell - ring the user doorbell allocated for a
 *                                   queue.
 *
 * The function kicks a notification to the firmware on the doorbell assigned
 * to the queue.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @queue: Pointer to the queue for ringing the door-bell.
 */
void kbase_csf_ring_cs_user_doorbell(struct kbase_device *kbdev,
			struct kbase_queue *queue);

/**
 * kbase_csf_active_queue_groups_reset - Reset the state of all active GPU
 *                            command queue groups associated with the context.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @kctx:  The kbase context.
 *
 * This function will iterate through all the active/scheduled GPU command
 * queue groups associated with the context, deschedule and mark them as
 * terminated (which will then lead to unbinding of all the queues bound to
 * them) and also no more work would be allowed to execute for them.
 *
 * This is similar to the action taken in response to an unexpected OoM event.
 */
void kbase_csf_active_queue_groups_reset(struct kbase_device *kbdev,
			struct kbase_context *kctx);

/**
 * kbase_csf_priority_check - Check the priority requested
 *
 * @kbdev:        Device pointer
 * @req_priority: Requested priority
 *
 * This will determine whether the requested priority can be satisfied.
 *
 * Return: The same or lower priority than requested.
 */
u8 kbase_csf_priority_check(struct kbase_device *kbdev, u8 req_priority);

extern const u8 kbasep_csf_queue_group_priority_to_relative[BASE_QUEUE_GROUP_PRIORITY_COUNT];
extern const u8 kbasep_csf_relative_to_queue_group_priority[KBASE_QUEUE_GROUP_PRIORITY_COUNT];

/**
 * kbase_csf_priority_relative_to_queue_group_priority - Convert relative to base priority
 *
 * @priority: kbase relative priority
 *
 * This will convert the monotonically increasing realtive priority to the
 * fixed base priority list.
 *
 * Return: base_queue_group_priority priority.
 */
static inline u8 kbase_csf_priority_relative_to_queue_group_priority(u8 priority)
{
	if (priority >= KBASE_QUEUE_GROUP_PRIORITY_COUNT)
		priority = KBASE_QUEUE_GROUP_PRIORITY_LOW;
	return kbasep_csf_relative_to_queue_group_priority[priority];
}

/**
 * kbase_csf_priority_queue_group_priority_to_relative - Convert base priority to relative
 *
 * @priority: base_queue_group_priority priority
 *
 * This will convert the fixed base priority list to monotonically increasing realtive priority.
 *
 * Return: kbase relative priority.
 */
static inline u8 kbase_csf_priority_queue_group_priority_to_relative(u8 priority)
{
	/* Apply low priority in case of invalid priority */
	if (priority >= BASE_QUEUE_GROUP_PRIORITY_COUNT)
		priority = BASE_QUEUE_GROUP_PRIORITY_LOW;
	return kbasep_csf_queue_group_priority_to_relative[priority];
}


#endif /* _KBASE_CSF_H_ */
