// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "queueing.h"

struct multicore_worker __percpu *
wg_packet_percpu_multicore_worker_alloc(work_func_t function, void *ptr)
{
	int cpu;
	struct multicore_worker __percpu *worker = alloc_percpu(struct multicore_worker);

	if (!worker)
		return NULL;

	for_each_possible_cpu(cpu) {
		per_cpu_ptr(worker, cpu)->ptr = ptr;
		INIT_WORK(&per_cpu_ptr(worker, cpu)->work, function);
	}
	return worker;
}

int wg_packet_queue_init(struct crypt_queue *queue, work_func_t function,
			 unsigned int len)
{
	int ret;

	memset(queue, 0, sizeof(*queue));
	ret = ptr_ring_init(&queue->ring, len, GFP_KERNEL);
	if (ret)
		return ret;
	queue->worker = wg_packet_percpu_multicore_worker_alloc(function, queue);
	if (!queue->worker) {
		ptr_ring_cleanup(&queue->ring, NULL);
		return -ENOMEM;
	}
	return 0;
}

void wg_packet_queue_free(struct crypt_queue *queue, bool purge)
{
	free_percpu(queue->worker);
	WARN_ON(!purge && !__ptr_ring_empty(&queue->ring));
	ptr_ring_cleanup(&queue->ring, purge ? (void(*)(void*))kfree_skb : NULL);
}

#define NEXT(skb) ((skb)->prev)
#define STUB(queue) ((struct sk_buff *)&queue->empty)

void wg_prev_queue_init(struct prev_queue *queue)
{
	NEXT(STUB(queue)) = NULL;
	queue->head = queue->tail = STUB(queue);
	queue->peeked = NULL;
	atomic_set(&queue->count, 0);
	BUILD_BUG_ON(
		offsetof(struct sk_buff, next) != offsetof(struct prev_queue, empty.next) -
							offsetof(struct prev_queue, empty) ||
		offsetof(struct sk_buff, prev) != offsetof(struct prev_queue, empty.prev) -
							 offsetof(struct prev_queue, empty));
}

static void __wg_prev_queue_enqueue(struct prev_queue *queue, struct sk_buff *skb)
{
	WRITE_ONCE(NEXT(skb), NULL);
	WRITE_ONCE(NEXT(xchg_release(&queue->head, skb)), skb);
}

bool wg_prev_queue_enqueue(struct prev_queue *queue, struct sk_buff *skb)
{
	if (!atomic_add_unless(&queue->count, 1, MAX_QUEUED_PACKETS))
		return false;
	__wg_prev_queue_enqueue(queue, skb);
	return true;
}

struct sk_buff *wg_prev_queue_dequeue(struct prev_queue *queue)
{
	struct sk_buff *tail = queue->tail, *next = smp_load_acquire(&NEXT(tail));

	if (tail == STUB(queue)) {
		if (!next)
			return NULL;
		queue->tail = next;
		tail = next;
		next = smp_load_acquire(&NEXT(next));
	}
	if (next) {
		queue->tail = next;
		atomic_dec(&queue->count);
		return tail;
	}
	if (tail != READ_ONCE(queue->head))
		return NULL;
	__wg_prev_queue_enqueue(queue, STUB(queue));
	next = smp_load_acquire(&NEXT(tail));
	if (next) {
		queue->tail = next;
		atomic_dec(&queue->count);
		return tail;
	}
	return NULL;
}

#undef NEXT
#undef STUB
