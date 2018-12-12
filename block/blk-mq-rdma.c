/*
 * Copyright (c) 2017 Sagi Grimberg.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/blk-mq.h>
#include <linux/blk-mq-rdma.h>
#include <rdma/ib_verbs.h>

/**
 * blk_mq_rdma_map_queues - provide a default queue mapping for rdma device
 * @set:	tagset to provide the mapping for
 * @dev:	rdma device associated with @set.
 * @first_vec:	first interrupt vectors to use for queues (usually 0)
 *
 * This function assumes the rdma device @dev has at least as many available
 * interrupt vetors as @set has queues.  It will then query it's affinity mask
 * and built queue mapping that maps a queue to the CPUs that have irq affinity
 * for the corresponding vector.
 *
 * In case either the driver passed a @dev with less vectors than
 * @set->nr_hw_queues, or @dev does not provide an affinity mask for a
 * vector, we fallback to the naive mapping.
 */
int blk_mq_rdma_map_queues(struct blk_mq_queue_map *map,
		struct ib_device *dev, int first_vec)
{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	for (queue = 0; queue < map->nr_queues; queue++) {
		mask = ib_get_vector_affinity(dev, first_vec + queue);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			map->mq_map[cpu] = map->queue_offset + queue;
	}

	return 0;

fallback:
	return blk_mq_map_queues(map);
}
EXPORT_SYMBOL_GPL(blk_mq_rdma_map_queues);
