/**
 * blk_mq_virtio_map_queues - provide a default queue mapping for virtio device
 * @qmap:	CPU to hardware queue map.
 * @vdev:	virtio device to provide a mapping for.
 * @first_vec:	first interrupt vectors to use for queues (usually 0)
 *
 * This function assumes the virtio device @vdev has at least as many available
 * interrupt vectors as @set has queues.  It will then query the vector
 * corresponding to each queue for its affinity mask and build a queue mapping
 * that maps a queue to the CPUs that have IRQ affinity for the corresponding
 * vector. If the number of available vectors is less than the number of queues,
 * it dynamically allocates vectors and assigns queues accordingly.
 */
void blk_mq_virtio_map_queues(struct blk_mq_queue_map *qmap,
		struct virtio_device *vdev, int first_vec)
{
	const struct cpumask *mask;
	unsigned int queue, cpu;
	unsigned int nr_vecs_needed = qmap->nr_queues;

	if (!vdev->config->get_vq_affinity)
		goto fallback;

	// Ensure the virtio device has at least nr_vecs_needed interrupt vectors
	if (vdev->config->num_vectors < nr_vecs_needed)
		nr_vecs_needed = vdev->config->num_vectors;

	for (queue = 0; queue < nr_vecs_needed; queue++) {
		mask = vdev->config->get_vq_affinity(vdev, first_vec + queue);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}

	return;

fallback:
	blk_mq_map_queues(qmap);
}
EXPORT_SYMBOL_GPL(blk_mq_virtio_map_queues);
