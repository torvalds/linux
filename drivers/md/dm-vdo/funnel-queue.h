/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_FUNNEL_QUEUE_H
#define VDO_FUNNEL_QUEUE_H

#include <linux/atomic.h>
#include <linux/cache.h>

/*
 * A funnel queue is a simple (almost) lock-free queue that accepts entries from multiple threads
 * (multi-producer) and delivers them to a single thread (single-consumer). "Funnel" is an attempt
 * to evoke the image of requests from more than one producer being "funneled down" to a single
 * consumer.
 *
 * This is an unsynchronized but thread-safe data structure when used as intended. There is no
 * mechanism to ensure that only one thread is consuming from the queue. If more than one thread
 * attempts to consume from the queue, the resulting behavior is undefined. Clients must not
 * directly access or manipulate the internals of the queue, which are only exposed for the purpose
 * of allowing the very simple enqueue operation to be inlined.
 *
 * The implementation requires that a funnel_queue_entry structure (a link pointer) is embedded in
 * the queue entries, and pointers to those structures are used exclusively by the queue. No macros
 * are defined to template the queue, so the offset of the funnel_queue_entry in the records placed
 * in the queue must all be the same so the client can derive their structure pointer from the
 * entry pointer returned by vdo_funnel_queue_poll().
 *
 * Callers are wholly responsible for allocating and freeing the entries. Entries may be freed as
 * soon as they are returned since this queue is not susceptible to the "ABA problem" present in
 * many lock-free data structures. The queue is dynamically allocated to ensure cache-line
 * alignment, but no other dynamic allocation is used.
 *
 * The algorithm is not actually 100% lock-free. There is a single point in vdo_funnel_queue_put()
 * at which a preempted producer will prevent the consumers from seeing items added to the queue by
 * later producers, and only if the queue is short enough or the consumer fast enough for it to
 * reach what was the end of the queue at the time of the preemption.
 *
 * The consumer function, vdo_funnel_queue_poll(), will return NULL when the queue is empty. To
 * wait for data to consume, spin (if safe) or combine the queue with a struct event_count to
 * signal the presence of new entries.
 */

/* This queue link structure must be embedded in client entries. */
struct funnel_queue_entry {
	/* The next (newer) entry in the queue. */
	struct funnel_queue_entry *next;
};

/*
 * The dynamically allocated queue structure, which is allocated on a cache line boundary so the
 * producer and consumer fields in the structure will land on separate cache lines. This should be
 * consider opaque but it is exposed here so vdo_funnel_queue_put() can be inlined.
 */
struct __aligned(L1_CACHE_BYTES) funnel_queue {
	/*
	 * The producers' end of the queue, an atomically exchanged pointer that will never be
	 * NULL.
	 */
	struct funnel_queue_entry *newest;

	/* The consumer's end of the queue, which is owned by the consumer and never NULL. */
	struct funnel_queue_entry *oldest __aligned(L1_CACHE_BYTES);

	/* A dummy entry used to provide the non-NULL invariants above. */
	struct funnel_queue_entry stub;
};

int __must_check vdo_make_funnel_queue(struct funnel_queue **queue_ptr);

void vdo_free_funnel_queue(struct funnel_queue *queue);

/*
 * Put an entry on the end of the queue.
 *
 * The entry pointer must be to the struct funnel_queue_entry embedded in the caller's data
 * structure. The caller must be able to derive the address of the start of their data structure
 * from the pointer that passed in here, so every entry in the queue must have the struct
 * funnel_queue_entry at the same offset within the client's structure.
 */
static inline void vdo_funnel_queue_put(struct funnel_queue *queue,
					struct funnel_queue_entry *entry)
{
	struct funnel_queue_entry *previous;

	/*
	 * Barrier requirements: All stores relating to the entry ("next" pointer, containing data
	 * structure fields) must happen before the previous->next store making it visible to the
	 * consumer. Also, the entry's "next" field initialization to NULL must happen before any
	 * other producer threads can see the entry (the xchg) and try to update the "next" field.
	 *
	 * xchg implements a full barrier.
	 */
	WRITE_ONCE(entry->next, NULL);
	previous = xchg(&queue->newest, entry);
	/*
	 * Preemptions between these two statements hide the rest of the queue from the consumer,
	 * preventing consumption until the following assignment runs.
	 */
	WRITE_ONCE(previous->next, entry);
}

struct funnel_queue_entry *__must_check vdo_funnel_queue_poll(struct funnel_queue *queue);

bool __must_check vdo_is_funnel_queue_empty(struct funnel_queue *queue);

bool __must_check vdo_is_funnel_queue_idle(struct funnel_queue *queue);

#endif /* VDO_FUNNEL_QUEUE_H */
