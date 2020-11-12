/*
 *
 * (C) COPYRIGHT 2018-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_CSF_H_
#define _KBASE_CSF_H_

#include "mali_kbase_csf_kcpu.h"
#include "mali_kbase_csf_scheduler.h"
#include "mali_kbase_csf_firmware.h"
#include "mali_kbase_csf_protected_memory.h"

/* Indicate invalid command stream h/w interface
 */
#define KBASEP_IF_NR_INVALID ((s8)-1)

/* Indicate invalid command stream group number for a GPU command queue group
 */
#define KBASEP_CSG_NR_INVALID ((s8)-1)

/* Indicate invalid user doorbell number for a GPU command queue
 */
#define KBASEP_USER_DB_NR_INVALID ((s8)-1)

/* Waiting timeout for global request completion acknowledgment */
#define GLB_REQ_WAIT_TIMEOUT_MS (300) /* 300 milliseconds */

#define CSG_REQ_EP_CFG (0x1 << CSG_REQ_EP_CFG_SHIFT)
#define CSG_REQ_SYNC_UPDATE (0x1 << CSG_REQ_SYNC_UPDATE_SHIFT)
#define FIRMWARE_PING_INTERVAL_MS (2000) /* 2 seconds */

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
 * kbase_csf_read_error - Read command stream fatal error
 *
 * This function takes the command stream fatal error from context's ordered
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
 * a given Kbase context, and also signals the thread of userspace driver
 * (front-end), waiting for the CSF event.
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
 * kbase_csf_ctx_init - Initialize the command-stream front-end for a GPU
 *                      address space.
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
 * kbase_csf_ctx_term - Terminate the command-stream front-end for a GPU
 *                      address space.
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

/** Find if given the queue group handle is valid.
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
 * @size:		The size in bytes for the user provided buffer.
 * @group_handle:	Handle for the group which uniquely identifies it within
 *			the context within which it was created.
 *
 * Return:		0 on success or negative value if failed to suspend
 *			queue group and copy suspend buffer contents.
 */
int kbase_csf_queue_group_suspend(struct kbase_context *kctx,
	struct kbase_suspend_copy_buffer *sus_buf, u8 group_handle);

/**
 * kbase_csf_interrupt - Handle interrupts issued by CSF firmware.
 *
 * @kbdev: The kbase device to handle an IRQ for
 * @val:   The value of JOB IRQ status register which triggered the interrupt
 */
void kbase_csf_interrupt(struct kbase_device *kbdev, u32 val);

/**
 * kbase_csf_doorbell_mapping_init - Initialize the bitmap of Hw doorbell pages
 *                           used to track their availability.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 */
int kbase_csf_doorbell_mapping_init(struct kbase_device *kbdev);

void kbase_csf_doorbell_mapping_term(struct kbase_device *kbdev);

/**
 * kbase_csf_ring_csg_doorbell - ring the doorbell for a command stream group
 *                               interface.
 *
 * The function kicks a notification on the command stream group interface to
 * firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 * @slot: Index of command stream group interface for ringing the door-bell.
 */
void kbase_csf_ring_csg_doorbell(struct kbase_device *kbdev, int slot);

/**
 * kbase_csf_ring_csg_slots_doorbell - ring the doorbell for a set of command
 *                                     stream group interfaces.
 *
 * The function kicks a notification on a set of command stream group
 * interfaces to firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 * @slot_bitmap: bitmap for the given slots, slot-0 on bit-0, etc.
 */
void kbase_csf_ring_csg_slots_doorbell(struct kbase_device *kbdev,
				       u32 slot_bitmap);

/**
 * kbase_csf_ring_cs_kernel_doorbell - ring the kernel doorbell for a queue
 *
 * The function kicks a notification to the firmware for the command stream
 * interface to which the queue is bound.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 * @queue: Pointer to the queue for ringing the door-bell.
 */
void kbase_csf_ring_cs_kernel_doorbell(struct kbase_device *kbdev,
			struct kbase_queue *queue);

/**
 * kbase_csf_ring_cs_user_doorbell - ring the user doorbell allocated for a
 *                                   queue.
 *
 * The function kicks a notification to the firmware on the doorbell assigned
 * to the queue.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 * @queue: Pointer to the queue for ringing the door-bell.
 */
void kbase_csf_ring_cs_user_doorbell(struct kbase_device *kbdev,
			struct kbase_queue *queue);

/**
 * kbase_csf_active_queue_groups_reset - Reset the state of all active GPU
 *                            command queue groups associated with the context.
 *
 * @kbdev:     Instance of a GPU platform device that implements a command
 *             stream front-end interface.
 * @kctx:      The kbase context.
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

#endif /* _KBASE_CSF_H_ */
