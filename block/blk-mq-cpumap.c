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

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"

static int cpu_to_queue_index(struct blk_mq_queue_map *qmap,
			      unsigned int nr_queues, const int cpu)
{
	return qmap->queue_offset + (cpu % nr_queues);
}

static int get_first_sibling(unsigned int cpu)
{
	unsigned int ret;

	ret = cpumask_first(topology_sibling_cpumask(cpu));
	if (ret < nr_cpu_ids)
		return ret;

	return cpu;
}

int blk_mq_map_queues(struct blk_mq_queue_map *qmap)
{
	unsigned int *map = qmap->mq_map;
	unsigned int nr_queues = qmap->nr_queues;
	unsigned int cpu, first_sibling;

	for_each_possible_cpu(cpu) {
		/*
		 * First do sequential mapping between CPUs and queues.
		 * In case we still have CPUs to map, and we have some number of
		 * threads per cores then map sibling threads to the same queue
		 * for performance optimizations.
		 */
		if (cpu < nr_queues) {
			map[cpu] = cpu_to_queue_index(qmap, nr_queues, cpu);
		} else {
			first_sibling = get_first_sibling(cpu);
			if (first_sibling == cpu)
				map[cpu] = cpu_to_queue_index(qmap, nr_queues, cpu);
			else
				map[cpu] = map[first_sibling];
		}
	}

	return 0;
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
			return local_memory_node(cpu_to_node(i));
	}

	return NUMA_NO_NODE;
}
