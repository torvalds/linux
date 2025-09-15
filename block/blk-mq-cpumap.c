// SPDX-License-Identifier: GPL-2.0
/*
 * CPU <-> hardware queue mapping helpers
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/group_cpus.h>
#include <linux/device/bus.h>
#include <linux/sched/isolation.h>

#include "blk.h"
#include "blk-mq.h"

static unsigned int blk_mq_num_queues(const struct cpumask *mask,
				      unsigned int max_queues)
{
	unsigned int num;

	num = cpumask_weight(mask);
	return min_not_zero(num, max_queues);
}

/**
 * blk_mq_num_possible_queues - Calc nr of queues for multiqueue devices
 * @max_queues:	The maximum number of queues the hardware/driver
 *		supports. If max_queues is 0, the argument is
 *		ignored.
 *
 * Calculates the number of queues to be used for a multiqueue
 * device based on the number of possible CPUs.
 */
unsigned int blk_mq_num_possible_queues(unsigned int max_queues)
{
	return blk_mq_num_queues(cpu_possible_mask, max_queues);
}
EXPORT_SYMBOL_GPL(blk_mq_num_possible_queues);

/**
 * blk_mq_num_online_queues - Calc nr of queues for multiqueue devices
 * @max_queues:	The maximum number of queues the hardware/driver
 *		supports. If max_queues is 0, the argument is
 *		ignored.
 *
 * Calculates the number of queues to be used for a multiqueue
 * device based on the number of online CPUs.
 */
unsigned int blk_mq_num_online_queues(unsigned int max_queues)
{
	return blk_mq_num_queues(cpu_online_mask, max_queues);
}
EXPORT_SYMBOL_GPL(blk_mq_num_online_queues);

void blk_mq_map_queues(struct blk_mq_queue_map *qmap)
{
	const struct cpumask *masks;
	unsigned int queue, cpu, nr_masks;

	masks = group_cpus_evenly(qmap->nr_queues, &nr_masks);
	if (!masks) {
		for_each_possible_cpu(cpu)
			qmap->mq_map[cpu] = qmap->queue_offset;
		return;
	}

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		for_each_cpu(cpu, &masks[queue % nr_masks])
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}
	kfree(masks);
}
EXPORT_SYMBOL_GPL(blk_mq_map_queues);

/**
 * blk_mq_hw_queue_to_node - Look up the memory node for a hardware queue index
 * @qmap: CPU to hardware queue map.
 * @index: hardware queue index.
 *
 * We have no quick way of doing reverse lookups. This is only used at
 * queue init time, so runtime isn't important.
 */
int blk_mq_hw_queue_to_node(struct blk_mq_queue_map *qmap, unsigned int index)
{
	int i;

	for_each_possible_cpu(i) {
		if (index == qmap->mq_map[i])
			return cpu_to_node(i);
	}

	return NUMA_NO_NODE;
}

/**
 * blk_mq_map_hw_queues - Create CPU to hardware queue mapping
 * @qmap:	CPU to hardware queue map
 * @dev:	The device to map queues
 * @offset:	Queue offset to use for the device
 *
 * Create a CPU to hardware queue mapping in @qmap. The struct bus_type
 * irq_get_affinity callback will be used to retrieve the affinity.
 */
void blk_mq_map_hw_queues(struct blk_mq_queue_map *qmap,
			  struct device *dev, unsigned int offset)

{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	if (!dev->bus->irq_get_affinity)
		goto fallback;

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		mask = dev->bus->irq_get_affinity(dev, queue + offset);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}

	return;

fallback:
	blk_mq_map_queues(qmap);
}
EXPORT_SYMBOL_GPL(blk_mq_map_hw_queues);
