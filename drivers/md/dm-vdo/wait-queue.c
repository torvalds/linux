// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "wait-queue.h"

#include <linux/device-mapper.h>

#include "permassert.h"

#include "status-codes.h"

/**
 * vdo_waitq_enqueue_waiter() - Add a waiter to the tail end of a waitq.
 * @waitq: The vdo_wait_queue to which to add the waiter.
 * @waiter: The waiter to add to the waitq.
 *
 * The waiter must not already be waiting in a waitq.
 */
void vdo_waitq_enqueue_waiter(struct vdo_wait_queue *waitq, struct vdo_waiter *waiter)
{
	BUG_ON(waiter->next_waiter != NULL);

	if (waitq->last_waiter == NULL) {
		/*
		 * The waitq is empty, so form the initial circular list by self-linking the
		 * initial waiter.
		 */
		waiter->next_waiter = waiter;
	} else {
		/* Splice the new waiter in at the end of the waitq. */
		waiter->next_waiter = waitq->last_waiter->next_waiter;
		waitq->last_waiter->next_waiter = waiter;
	}

	/* In both cases, the waiter we added to the list becomes the last waiter. */
	waitq->last_waiter = waiter;
	waitq->length += 1;
}

/**
 * vdo_waitq_transfer_all_waiters() - Transfer all waiters from one waitq to
 *                                    a second waitq, emptying the first waitq.
 * @from_waitq: The waitq containing the waiters to move.
 * @to_waitq: The waitq that will receive the waiters from the first waitq.
 */
void vdo_waitq_transfer_all_waiters(struct vdo_wait_queue *from_waitq,
				    struct vdo_wait_queue *to_waitq)
{
	/* If the source waitq is empty, there's nothing to do. */
	if (!vdo_waitq_has_waiters(from_waitq))
		return;

	if (vdo_waitq_has_waiters(to_waitq)) {
		/*
		 * Both are non-empty. Splice the two circular lists together
		 * by swapping the next (head) pointers in the list tails.
		 */
		struct vdo_waiter *from_head = from_waitq->last_waiter->next_waiter;
		struct vdo_waiter *to_head = to_waitq->last_waiter->next_waiter;

		to_waitq->last_waiter->next_waiter = from_head;
		from_waitq->last_waiter->next_waiter = to_head;
	}

	to_waitq->last_waiter = from_waitq->last_waiter;
	to_waitq->length += from_waitq->length;
	vdo_waitq_init(from_waitq);
}

/**
 * vdo_waitq_notify_all_waiters() - Notify all the entries waiting in a waitq.
 * @waitq: The vdo_wait_queue containing the waiters to notify.
 * @callback: The function to call to notify each waiter, or NULL to invoke the callback field
 *            registered in each waiter.
 * @context: The context to pass to the callback function.
 *
 * Notifies all the entries waiting in a waitq to continue execution by invoking a callback
 * function on each of them in turn. The waitq is copied and emptied before invoking any callbacks,
 * and only the waiters that were in the waitq at the start of the call will be notified.
 */
void vdo_waitq_notify_all_waiters(struct vdo_wait_queue *waitq,
				  vdo_waiter_callback_fn callback, void *context)
{
	/*
	 * Copy and empty the waitq first, avoiding the possibility of an infinite
	 * loop if entries are returned to the waitq by the callback function.
	 */
	struct vdo_wait_queue waiters;

	vdo_waitq_init(&waiters);
	vdo_waitq_transfer_all_waiters(waitq, &waiters);

	/* Drain the copied waitq, invoking the callback on every entry. */
	while (vdo_waitq_has_waiters(&waiters))
		vdo_waitq_notify_next_waiter(&waiters, callback, context);
}

/**
 * vdo_waitq_get_first_waiter() - Return the waiter that is at the head end of a waitq.
 * @waitq: The vdo_wait_queue from which to get the first waiter.
 *
 * Return: The first (oldest) waiter in the waitq, or NULL if the waitq is empty.
 */
struct vdo_waiter *vdo_waitq_get_first_waiter(const struct vdo_wait_queue *waitq)
{
	struct vdo_waiter *last_waiter = waitq->last_waiter;

	if (last_waiter == NULL) {
		/* There are no waiters, so we're done. */
		return NULL;
	}

	/* The waitq is circular, so the last entry links to the head of the waitq. */
	return last_waiter->next_waiter;
}

/**
 * vdo_waitq_dequeue_matching_waiters() - Remove all waiters that match based on the specified
 *                                        matching method and append them to a vdo_wait_queue.
 * @waitq: The vdo_wait_queue to process.
 * @waiter_match: The method to determine matching.
 * @match_context: Contextual info for the match method.
 * @matched_waitq: A wait_waitq to store matches.
 */
void vdo_waitq_dequeue_matching_waiters(struct vdo_wait_queue *waitq,
					vdo_waiter_match_fn waiter_match,
					void *match_context,
					struct vdo_wait_queue *matched_waitq)
{
	struct vdo_wait_queue iteration_waitq;

	vdo_waitq_init(&iteration_waitq);
	vdo_waitq_transfer_all_waiters(waitq, &iteration_waitq);

	while (vdo_waitq_has_waiters(&iteration_waitq)) {
		struct vdo_waiter *waiter = vdo_waitq_dequeue_waiter(&iteration_waitq);

		vdo_waitq_enqueue_waiter((waiter_match(waiter, match_context) ?
					  matched_waitq : waitq), waiter);
	}
}

/**
 * vdo_waitq_dequeue_waiter() - Remove the first (oldest) waiter from a waitq.
 * @waitq: The vdo_wait_queue from which to remove the first entry.
 *
 * The caller will be responsible for waking the waiter by continuing its
 * execution appropriately.
 *
 * Return: The first (oldest) waiter in the waitq, or NULL if the waitq is empty.
 */
struct vdo_waiter *vdo_waitq_dequeue_waiter(struct vdo_wait_queue *waitq)
{
	struct vdo_waiter *first_waiter = vdo_waitq_get_first_waiter(waitq);
	struct vdo_waiter *last_waiter = waitq->last_waiter;

	if (first_waiter == NULL)
		return NULL;

	if (first_waiter == last_waiter) {
		/* The waitq has a single entry, so empty it by nulling the tail. */
		waitq->last_waiter = NULL;
	} else {
		/*
		 * The waitq has multiple waiters, so splice the first waiter out
		 * of the circular waitq.
		 */
		last_waiter->next_waiter = first_waiter->next_waiter;
	}

	/* The waiter is no longer in a waitq. */
	first_waiter->next_waiter = NULL;
	waitq->length -= 1;

	return first_waiter;
}

/**
 * vdo_waitq_notify_next_waiter() - Notify the next entry waiting in a waitq.
 * @waitq: The vdo_wait_queue containing the waiter to notify.
 * @callback: The function to call to notify the waiter, or NULL to invoke the callback field
 *            registered in the waiter.
 * @context: The context to pass to the callback function.
 *
 * Notifies the next entry waiting in a waitq to continue execution by invoking a callback function
 * on it after removing it from the waitq.
 *
 * Return: true if there was a waiter in the waitq.
 */
bool vdo_waitq_notify_next_waiter(struct vdo_wait_queue *waitq,
				  vdo_waiter_callback_fn callback, void *context)
{
	struct vdo_waiter *waiter = vdo_waitq_dequeue_waiter(waitq);

	if (waiter == NULL)
		return false;

	if (callback == NULL)
		callback = waiter->callback;
	callback(waiter, context);

	return true;
}
