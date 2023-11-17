/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_WAIT_QUEUE_H
#define VDO_WAIT_QUEUE_H

#include <linux/compiler.h>
#include <linux/types.h>

/**
 * DOC: Wait queues.
 *
 * A wait queue is a circular list of entries waiting to be notified of a change in a condition.
 * Keeping a circular list allows the queue structure to simply be a pointer to the tail (newest)
 * entry in the queue, supporting constant-time enqueue and dequeue operations. A null pointer is
 * an empty queue.
 *
 *   An empty queue:
 *     queue0.last_waiter -> NULL
 *
 *   A singleton queue:
 *     queue1.last_waiter -> entry1 -> entry1 -> [...]
 *
 *   A three-element queue:
 *     queue2.last_waiter -> entry3 -> entry1 -> entry2 -> entry3 -> [...]
 */

struct waiter;

struct wait_queue {
	/* The tail of the queue, the last (most recently added) entry */
	struct waiter *last_waiter;
	/* The number of waiters currently in the queue */
	size_t queue_length;
};

/**
 * typedef waiter_callback_fn - Callback type for functions which will be called to resume
 *                              processing of a waiter after it has been removed from its wait
 *                              queue.
 */
typedef void (*waiter_callback_fn)(struct waiter *waiter, void *context);

/**
 * typedef waiter_match_fn - Method type for waiter matching methods.
 *
 * A waiter_match_fn method returns false if the waiter does not match.
 */
typedef bool (*waiter_match_fn)(struct waiter *waiter, void *context);

/* The queue entry structure for entries in a wait_queue. */
struct waiter {
	/*
	 * The next waiter in the queue. If this entry is the last waiter, then this is actually a
	 * pointer back to the head of the queue.
	 */
	struct waiter *next_waiter;

	/* Optional waiter-specific callback to invoke when waking this waiter. */
	waiter_callback_fn callback;
};

/**
 * is_waiting() - Check whether a waiter is waiting.
 * @waiter: The waiter to check.
 *
 * Return: true if the waiter is on some wait_queue.
 */
static inline bool vdo_is_waiting(struct waiter *waiter)
{
	return (waiter->next_waiter != NULL);
}

/**
 * initialize_wait_queue() - Initialize a wait queue.
 * @queue: The queue to initialize.
 */
static inline void vdo_initialize_wait_queue(struct wait_queue *queue)
{
	*queue = (struct wait_queue) {
		.last_waiter = NULL,
		.queue_length = 0,
	};
}

/**
 * has_waiters() - Check whether a wait queue has any entries waiting in it.
 * @queue: The queue to query.
 *
 * Return: true if there are any waiters in the queue.
 */
static inline bool __must_check vdo_has_waiters(const struct wait_queue *queue)
{
	return (queue->last_waiter != NULL);
}

void vdo_enqueue_waiter(struct wait_queue *queue, struct waiter *waiter);

void vdo_notify_all_waiters(struct wait_queue *queue, waiter_callback_fn callback,
			    void *context);

bool vdo_notify_next_waiter(struct wait_queue *queue, waiter_callback_fn callback,
			    void *context);

void vdo_transfer_all_waiters(struct wait_queue *from_queue,
			      struct wait_queue *to_queue);

struct waiter *vdo_get_first_waiter(const struct wait_queue *queue);

void vdo_dequeue_matching_waiters(struct wait_queue *queue, waiter_match_fn match_method,
				  void *match_context, struct wait_queue *matched_queue);

struct waiter *vdo_dequeue_next_waiter(struct wait_queue *queue);

/**
 * count_waiters() - Count the number of waiters in a wait queue.
 * @queue: The wait queue to query.
 *
 * Return: The number of waiters in the queue.
 */
static inline size_t __must_check vdo_count_waiters(const struct wait_queue *queue)
{
	return queue->queue_length;
}

const struct waiter * __must_check vdo_get_next_waiter(const struct wait_queue *queue,
						       const struct waiter *waiter);

#endif /* VDO_WAIT_QUEUE_H */
