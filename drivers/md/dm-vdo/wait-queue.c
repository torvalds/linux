// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "wait-queue.h"

#include <linux/device-mapper.h>

#include "permassert.h"

#include "status-codes.h"

/**
 * vdo_enqueue_waiter() - Add a waiter to the tail end of a wait queue.
 * @queue: The queue to which to add the waiter.
 * @waiter: The waiter to add to the queue.
 *
 * The waiter must not already be waiting in a queue.
 *
 * Return: VDO_SUCCESS or an error code.
 */
void vdo_enqueue_waiter(struct wait_queue *queue, struct waiter *waiter)
{
	BUG_ON(waiter->next_waiter != NULL);

	if (queue->last_waiter == NULL) {
		/*
		 * The queue is empty, so form the initial circular list by self-linking the
		 * initial waiter.
		 */
		waiter->next_waiter = waiter;
	} else {
		/* Splice the new waiter in at the end of the queue. */
		waiter->next_waiter = queue->last_waiter->next_waiter;
		queue->last_waiter->next_waiter = waiter;
	}

	/* In both cases, the waiter we added to the ring becomes the last waiter. */
	queue->last_waiter = waiter;
	queue->queue_length += 1;
}

/**
 * vdo_transfer_all_waiters() - Transfer all waiters from one wait queue to a second queue,
 *                              emptying the first queue.
 * @from_queue: The queue containing the waiters to move.
 * @to_queue: The queue that will receive the waiters from the first queue.
 */
void vdo_transfer_all_waiters(struct wait_queue *from_queue, struct wait_queue *to_queue)
{
	/* If the source queue is empty, there's nothing to do. */
	if (!vdo_has_waiters(from_queue))
		return;

	if (vdo_has_waiters(to_queue)) {
		/*
		 * Both queues are non-empty. Splice the two circular lists together by swapping
		 * the next (head) pointers in the list tails.
		 */
		struct waiter *from_head = from_queue->last_waiter->next_waiter;
		struct waiter *to_head = to_queue->last_waiter->next_waiter;

		to_queue->last_waiter->next_waiter = from_head;
		from_queue->last_waiter->next_waiter = to_head;
	}

	to_queue->last_waiter = from_queue->last_waiter;
	to_queue->queue_length += from_queue->queue_length;
	vdo_initialize_wait_queue(from_queue);
}

/**
 * vdo_notify_all_waiters() - Notify all the entries waiting in a queue.
 * @queue: The wait queue containing the waiters to notify.
 * @callback: The function to call to notify each waiter, or NULL to invoke the callback field
 *            registered in each waiter.
 * @context: The context to pass to the callback function.
 *
 * Notifies all the entries waiting in a queue to continue execution by invoking a callback
 * function on each of them in turn. The queue is copied and emptied before invoking any callbacks,
 * and only the waiters that were in the queue at the start of the call will be notified.
 */
void vdo_notify_all_waiters(struct wait_queue *queue, waiter_callback_fn callback,
			    void *context)
{
	/*
	 * Copy and empty the queue first, avoiding the possibility of an infinite loop if entries
	 * are returned to the queue by the callback function.
	 */
	struct wait_queue waiters;

	vdo_initialize_wait_queue(&waiters);
	vdo_transfer_all_waiters(queue, &waiters);

	/* Drain the copied queue, invoking the callback on every entry. */
	while (vdo_has_waiters(&waiters))
		vdo_notify_next_waiter(&waiters, callback, context);
}

/**
 * vdo_get_first_waiter() - Return the waiter that is at the head end of a wait queue.
 * @queue: The queue from which to get the first waiter.
 *
 * Return: The first (oldest) waiter in the queue, or NULL if the queue is empty.
 */
struct waiter *vdo_get_first_waiter(const struct wait_queue *queue)
{
	struct waiter *last_waiter = queue->last_waiter;

	if (last_waiter == NULL) {
		/* There are no waiters, so we're done. */
		return NULL;
	}

	/* The queue is circular, so the last entry links to the head of the queue. */
	return last_waiter->next_waiter;
}

/**
 * vdo_dequeue_matching_waiters() - Remove all waiters that match based on the specified matching
 *                                  method and append them to a wait_queue.
 * @queue: The wait queue to process.
 * @match_method: The method to determine matching.
 * @match_context: Contextual info for the match method.
 * @matched_queue: A wait_queue to store matches.
 */
void vdo_dequeue_matching_waiters(struct wait_queue *queue, waiter_match_fn match_method,
				  void *match_context, struct wait_queue *matched_queue)
{
	struct wait_queue matched_waiters, iteration_queue;

	vdo_initialize_wait_queue(&matched_waiters);

	vdo_initialize_wait_queue(&iteration_queue);
	vdo_transfer_all_waiters(queue, &iteration_queue);
	while (vdo_has_waiters(&iteration_queue)) {
		struct waiter *waiter = vdo_dequeue_next_waiter(&iteration_queue);

		vdo_enqueue_waiter((match_method(waiter, match_context) ?
				    &matched_waiters : queue), waiter);
	}

	vdo_transfer_all_waiters(&matched_waiters, matched_queue);
}

/**
 * vdo_dequeue_next_waiter() - Remove the first waiter from the head end of a wait queue.
 * @queue: The wait queue from which to remove the first entry.
 *
 * The caller will be responsible for waking the waiter by invoking the correct callback function
 * to resume its execution.
 *
 * Return: The first (oldest) waiter in the queue, or NULL if the queue is empty.
 */
struct waiter *vdo_dequeue_next_waiter(struct wait_queue *queue)
{
	struct waiter *first_waiter = vdo_get_first_waiter(queue);
	struct waiter *last_waiter = queue->last_waiter;

	if (first_waiter == NULL)
		return NULL;

	if (first_waiter == last_waiter) {
		/* The queue has a single entry, so just empty it out by nulling the tail. */
		queue->last_waiter = NULL;
	} else {
		/*
		 * The queue has more than one entry, so splice the first waiter out of the
		 * circular queue.
		 */
		last_waiter->next_waiter = first_waiter->next_waiter;
	}

	/* The waiter is no longer in a wait queue. */
	first_waiter->next_waiter = NULL;
	queue->queue_length -= 1;

	return first_waiter;
}

/**
 * vdo_notify_next_waiter() - Notify the next entry waiting in a queue.
 * @queue: The wait queue containing the waiter to notify.
 * @callback: The function to call to notify the waiter, or NULL to invoke the callback field
 *            registered in the waiter.
 * @context: The context to pass to the callback function.
 *
 * Notifies the next entry waiting in a queue to continue execution by invoking a callback function
 * on it after removing it from the queue.
 *
 * Return: true if there was a waiter in the queue.
 */
bool vdo_notify_next_waiter(struct wait_queue *queue, waiter_callback_fn callback,
			    void *context)
{
	struct waiter *waiter = vdo_dequeue_next_waiter(queue);

	if (waiter == NULL)
		return false;

	if (callback == NULL)
		callback = waiter->callback;
	(*callback)(waiter, context);

	return true;
}

/**
 * vdo_get_next_waiter() - Get the waiter after this one, for debug iteration.
 * @queue: The wait queue.
 * @waiter: A waiter.
 *
 * Return: The next waiter, or NULL.
 */
const struct waiter *vdo_get_next_waiter(const struct wait_queue *queue,
					 const struct waiter *waiter)
{
	struct waiter *first_waiter = vdo_get_first_waiter(queue);

	if (waiter == NULL)
		return first_waiter;

	return ((waiter->next_waiter != first_waiter) ? waiter->next_waiter : NULL);
}
