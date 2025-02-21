/* SPDX-License-Identifier: MIT */

#ifndef _DRM_GPU_SCHEDULER_INTERNAL_H_
#define _DRM_GPU_SCHEDULER_INTERNAL_H_


/* Used to choose between FIFO and RR job-scheduling */
extern int drm_sched_policy;

#define DRM_SCHED_POLICY_RR    0
#define DRM_SCHED_POLICY_FIFO  1

void drm_sched_wakeup(struct drm_gpu_scheduler *sched);

void drm_sched_rq_add_entity(struct drm_sched_rq *rq,
			     struct drm_sched_entity *entity);
void drm_sched_rq_remove_entity(struct drm_sched_rq *rq,
				struct drm_sched_entity *entity);

void drm_sched_rq_update_fifo_locked(struct drm_sched_entity *entity,
				     struct drm_sched_rq *rq, ktime_t ts);

void drm_sched_entity_select_rq(struct drm_sched_entity *entity);
struct drm_sched_job *drm_sched_entity_pop_job(struct drm_sched_entity *entity);

struct drm_sched_fence *drm_sched_fence_alloc(struct drm_sched_entity *s_entity,
					      void *owner);
void drm_sched_fence_init(struct drm_sched_fence *fence,
			  struct drm_sched_entity *entity);
void drm_sched_fence_free(struct drm_sched_fence *fence);

void drm_sched_fence_scheduled(struct drm_sched_fence *fence,
			       struct dma_fence *parent);
void drm_sched_fence_finished(struct drm_sched_fence *fence, int result);

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

/* Return true if entity could provide a job. */
static inline bool
drm_sched_entity_is_ready(struct drm_sched_entity *entity)
{
	if (!spsc_queue_count(&entity->job_queue))
		return false;

	if (READ_ONCE(entity->dependency))
		return false;

	return true;
}

#endif
