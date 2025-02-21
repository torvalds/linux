/* SPDX-License-Identifier: MIT */

#ifndef _DRM_GPU_SCHEDULER_INTERNAL_H_
#define _DRM_GPU_SCHEDULER_INTERNAL_H_

/**
 * drm_sched_entity_queue_pop - Low level helper for popping queued jobs
 *
 * @entity: scheduler entity
 *
 * Low level helper for popping queued jobs.
 *
 * Returns: The job dequeued or NULL.
 */
static inline struct drm_sched_job *
drm_sched_entity_queue_pop(struct drm_sched_entity *entity)
{
	struct spsc_node *node;

	node = spsc_queue_pop(&entity->job_queue);
	if (!node)
		return NULL;

	return container_of(node, struct drm_sched_job, queue_node);
}

/**
 * drm_sched_entity_queue_peek - Low level helper for peeking at the job queue
 *
 * @entity: scheduler entity
 *
 * Low level helper for peeking at the job queue
 *
 * Returns: The job at the head of the queue or NULL.
 */
static inline struct drm_sched_job *
drm_sched_entity_queue_peek(struct drm_sched_entity *entity)
{
	struct spsc_node *node;

	node = spsc_queue_peek(&entity->job_queue);
	if (!node)
		return NULL;

	return container_of(node, struct drm_sched_job, queue_node);
}

#endif
