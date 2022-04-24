/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_EVENT_H_
#define _KBASE_CSF_EVENT_H_

#include <linux/types.h>
#include <linux/wait.h>

struct kbase_context;
struct kbase_csf_event;
enum kbase_csf_event_callback_action;

/**
 * kbase_csf_event_callback - type for callback functions to be
 *                            called upon CSF events.
 * @param:   Generic parameter to pass to the callback function.
 *
 * This is the type of callback functions that can be registered
 * for CSF events. These function calls shall be triggered by any call
 * to kbase_csf_event_signal.
 *
 * Return: KBASE_CSF_EVENT_CALLBACK_KEEP if the callback should remain
 * registered, or KBASE_CSF_EVENT_CALLBACK_REMOVE if it should be removed.
 */
typedef enum kbase_csf_event_callback_action kbase_csf_event_callback(void *param);

/**
 * kbase_csf_event_wait_add - Add a CSF event callback
 *
 * @kctx:      The Kbase context the @callback should be registered to.
 * @callback:  The callback function to register.
 * @param:     Custom parameter to be passed to the @callback function.
 *
 * This function adds an event callback to the list of CSF event callbacks
 * belonging to a given Kbase context, to be triggered when a CSF event is
 * signalled by kbase_csf_event_signal.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_event_wait_add(struct kbase_context *kctx,
		kbase_csf_event_callback *callback, void *param);

/**
 * kbase_csf_event_wait_remove - Remove a CSF event callback
 *
 * @kctx:      The kbase context the @callback should be removed from.
 * @callback:  The callback function to remove.
 * @param:     Custom parameter that would have been passed to the @p callback
 *             function.
 *
 * This function removes an event callback from the list of CSF event callbacks
 * belonging to a given Kbase context.
 */
void kbase_csf_event_wait_remove(struct kbase_context *kctx,
		kbase_csf_event_callback *callback, void *param);

/**
 * kbase_csf_event_term - Removes all CSF event callbacks
 *
 * @kctx:  The kbase context for which CSF event callbacks have to be removed.
 *
 * This function empties the list of CSF event callbacks belonging to a given
 * Kbase context.
 */
void kbase_csf_event_term(struct kbase_context *kctx);

/**
 * kbase_csf_event_signal - Signal a CSF event
 *
 * @kctx:  The kbase context whose CSF event callbacks shall be triggered.
 * @notify_gpu: Flag to indicate if CSF firmware should be notified of the
 *              signaling of event that happened on the Driver side, either
 *              the signal came from userspace or from kcpu queues.
 *
 * This function triggers all the CSF event callbacks that are registered to
 * a given Kbase context, and also signals the event handling thread of
 * userspace driver waiting for the CSF event.
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
 * kbase_csf_event_init - Initialize event object
 *
 * @kctx: The kbase context whose event object will be initialized.
 *
 * This function initializes the event object.
 */
void kbase_csf_event_init(struct kbase_context *const kctx);

struct kbase_csf_notification;
struct base_csf_notification;
/**
 * kbase_csf_event_read_error - Read and remove an error from error list in event
 *
 * @kctx: The kbase context.
 * @event_data: Caller-provided buffer to copy the fatal error to
 *
 * This function takes the CS fatal error from context's ordered
 * error_list, copies its contents to @event_data.
 *
 * Return: true if error is read out or false if there is no error in error list.
 */
bool kbase_csf_event_read_error(struct kbase_context *kctx,
				struct base_csf_notification *event_data);

/**
 * kbase_csf_event_add_error - Add an error into event error list
 *
 * @kctx:  Address of a base context associated with a GPU address space.
 * @error: Address of the item to be added to the context's pending error list.
 * @data:  Error data to be returned to userspace.
 *
 * Does not wake up the event queue blocking a user thread in kbase_poll. This
 * is to make it more efficient to add multiple errors.
 *
 * The added error must not already be on the context's list of errors waiting
 * to be reported (e.g. because a previous error concerning the same object has
 * not yet been reported).
 *
 */
void kbase_csf_event_add_error(struct kbase_context *const kctx,
			struct kbase_csf_notification *const error,
			struct base_csf_notification const *const data);

/**
 * kbase_csf_event_remove_error - Remove an error from event error list
 *
 * @kctx:  Address of a base context associated with a GPU address space.
 * @error: Address of the item to be removed from the context's event error list.
 */
void kbase_csf_event_remove_error(struct kbase_context *kctx,
				  struct kbase_csf_notification *error);

/**
 * kbase_csf_event_error_pending - Check the error pending status
 *
 * @kctx: The kbase context to check fatal error upon.
 *
 * Return: true if there is error in the list.
 */
bool kbase_csf_event_error_pending(struct kbase_context *kctx);
#endif /* _KBASE_CSF_EVENT_H_ */
