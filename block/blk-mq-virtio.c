// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Christoph Hellwig.
 */
#include <linux/device.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-virtio.h>
#include <linux/virtio_config.h>
#include <linux/module.h>
#include "blk-mq.h"

/**
 * blk_mq_virtio_map_queues - provide a default queue mapping for virtio device
 * @set:	tagset to provide the mapping for
 * @vdev:	virtio device associated with @set.
 * @first_vec:	first interrupt vectors to use for queues (usually 0)
 *
 * This function assumes the virtio device @vdev has at least as many available
 * interrupt vetors as @set has queues.  It will then queuery the vector
 * corresponding to each queue for it's affinity mask and built queue mapping
 * that maps a queue to the CPUs that have irq affinity for the corresponding
 * vector.
 */
int blk_mq_virtio_map_queues(struct blk_mq_queue_map *qmap,
		struct virtio_device *vdev, int first_vec)
{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	if (!vdev->config->get_vq_affinity)
		goto fallback;

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		mask = vdev->config->get_vq_affinity(vdev, first_vec + queue);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}

	return 0;
fallback:
	return blk_mq_map_queues(qmap);
}
EXPORT_SYMBOL_GPL(blk_mq_virtio_map_queues);
