/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_WAIT_QUEUE_H
#define VDO_WAIT_QUEUE_H

#include <linux/compiler.h>
#include <linux/types.h>

/**
 * A vdo_wait_queue is a circular singly linked list of entries waiting to be notified
 * of a change in a condition. Keeping a circular list allows the vdo_wait_queue
 * structure to simply be a pointer to the tail (newest) entry, supporting
 * constant-time enqueue and dequeue operations. A null pointer is an empty waitq.
 *
 *   An empty waitq:
 *     waitq0.last_waiter -> NULL
 *
 *   A singleton waitq:
 *     waitq1.last_waiter -> entry1 -> entry1 -> [...]
 *
 *   A three-element waitq:
 *     waitq2.last_waiter -> entry3 -> entry1 -> entry2 -> entry3 -> [...]
 *
 *   linux/wait.h's wait_queue_head is _not_ used because vdo_wait_queue's
 *   interface is much less complex (doesn't need locking, priorities or timers).
 *   Made possible by vdo's thread-based resource allocation and locking; and
 *   the polling nature of vdo_wait_queue consumers.
 *
 *   FIXME: could be made to use a linux/list.h's list_head but its extra barriers
 *   really aren't needed. Nor is a doubly linked list, but vdo_wait_queue could
 *   make use of __list_del_clearprev() -- but that would compromise the ability
 *   to make full use of linux's list interface.
 */

struct vdo_waiter;

struct vdo_wait_queue {
	/* The tail of the queue, the last (most recently added) entry */
	struct vdo_waiter *last_waiter;
	/* The number of waiters currently in the queue */
	size_t length;
};

/**
 * vdo_waiter_callback_fn - Callback type that will be called to resume processing
 *                          of a waiter after it has been removed from its wait queue.
 */
typedef void (*vdo_waiter_callback_fn)(struct vdo_waiter *waiter, void *context);

/**
 * vdo_waiter_match_fn - Method type for waiter matching methods.
 *
 * Returns false if the waiter does not match.
 */
typedef bool (*vdo_waiter_match_fn)(struct vdo_waiter *waiter, void *context);

/* The structure for entries in a vdo_wait_queue. */
struct vdo_waiter {
	/*
	 * The next waiter in the waitq. If this entry is the last waiter, then this
	 * is actually a pointer back to the head of the waitq.
	 */
	struct vdo_waiter *next_waiter;

	/* Optional waiter-specific callback to invoke when dequeuing this waiter. */
	vdo_waiter_callback_fn callback;
};

/**
 * vdo_waiter_is_waiting() - Check whether a waiter is waiting.
 * @waiter: The waiter to check.
 *
 * Return: true if the waiter is on some vdo_wait_queue.
 */
static inline bool vdo_waiter_is_waiting(struct vdo_waiter *waiter)
{
	return (waiter->next_waiter != NULL);
}

/**
 * vdo_waitq_init() - Initialize a vdo_wait_queue.
 * @waitq: The vdo_wait_queue to initialize.
 */
static inline void vdo_waitq_init(struct vdo_wait_queue *waitq)
{
	*waitq = (struct vdo_wait_queue) {
		.last_waiter = NULL,
		.length = 0,
	};
}

/**
 * vdo_waitq_has_waiters() - Check whether a vdo_wait_queue has any entries waiting.
 * @waitq: The vdo_wait_queue to query.
 *
 * Return: true if there are any waiters in the waitq.
 */
static inline bool __must_check vdo_waitq_has_waiters(const struct vdo_wait_queue *waitq)
{
	return (waitq->last_waiter != NULL);
}

void vdo_waitq_enqueue_waiter(struct vdo_wait_queue *waitq,
			      struct vdo_waiter *waiter);

struct vdo_waiter *vdo_waitq_dequeue_waiter(struct vdo_wait_queue *waitq);

void vdo_waitq_notify_all_waiters(struct vdo_wait_queue *waitq,
				  vdo_waiter_callback_fn callback, void *context);

bool vdo_waitq_notify_next_waiter(struct vdo_wait_queue *waitq,
				  vdo_waiter_callback_fn callback, void *context);

void vdo_waitq_transfer_all_waiters(struct vdo_wait_queue *from_waitq,
				    struct vdo_wait_queue *to_waitq);

struct vdo_waiter *vdo_waitq_get_first_waiter(const struct vdo_wait_queue *waitq);

void vdo_waitq_dequeue_matching_waiters(struct vdo_wait_queue *waitq,
					vdo_waiter_match_fn waiter_match,
					void *match_context,
					struct vdo_wait_queue *matched_waitq);

/**
 * vdo_waitq_num_waiters() - Return the number of waiters in a vdo_wait_queue.
 * @waitq: The vdo_wait_queue to query.
 *
 * Return: The number of waiters in the waitq.
 */
static inline size_t __must_check vdo_waitq_num_waiters(const struct vdo_wait_queue *waitq)
{
	return waitq->length;
}

#endif /* VDO_WAIT_QUEUE_H */
