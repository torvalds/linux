// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "queueing.h"

struct multicore_worker __percpu *
wg_packet_percpu_multicore_worker_alloc(work_func_t function, void *ptr)
{
	int cpu;
	struct multicore_worker __percpu *worker =
		alloc_percpu(struct multicore_worker);

	if (!worker)
		return NULL;

	for_each_possible_cpu(cpu) {
		per_cpu_ptr(worker, cpu)->ptr = ptr;
		INIT_WORK(&per_cpu_ptr(worker, cpu)->work, function);
	}
	return worker;
}

int wg_packet_queue_init(struct crypt_queue *queue, work_func_t function,
			 bool multicore, unsigned int len)
{
	int ret;

	memset(queue, 0, sizeof(*queue));
	ret = ptr_ring_init(&queue->ring, len, GFP_KERNEL);
	if (ret)
		return ret;
	if (function) {
		if (multicore) {
			queue->worker = wg_packet_percpu_multicore_worker_alloc(
				function, queue);
			if (!queue->worker) {
				ptr_ring_cleanup(&queue->ring, NULL);
				return -ENOMEM;
			}
		} else {
			INIT_WORK(&queue->work, function);
		}
	}
	return 0;
}

void wg_packet_queue_free(struct crypt_queue *queue, bool multicore)
{
	if (multicore)
		free_percpu(queue->worker);
	WARN_ON(!__ptr_ring_empty(&queue->ring));
	ptr_ring_cleanup(&queue->ring, NULL);
}
