/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_EXEC_QUEUE_H_
#define _XE_EXEC_QUEUE_H_

#include "xe_exec_queue_types.h"
#include "xe_vm_types.h"

struct drm_device;
struct drm_file;
struct xe_device;
struct xe_file;

#define for_each_tlb_inval(__i)	\
	for (__i = XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT; \
	     __i <= XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT; ++__i)

struct xe_exec_queue *xe_exec_queue_create(struct xe_device *xe, struct xe_vm *vm,
					   u32 logical_mask, u16 width,
					   struct xe_hw_engine *hw_engine, u32 flags,
					   u64 extensions);
struct xe_exec_queue *xe_exec_queue_create_class(struct xe_device *xe, struct xe_gt *gt,
						 struct xe_vm *vm,
						 enum xe_engine_class class,
						 u32 flags, u64 extensions);
struct xe_exec_queue *xe_exec_queue_create_bind(struct xe_device *xe,
						struct xe_tile *tile,
						u32 flags, u64 extensions);

void xe_exec_queue_fini(struct xe_exec_queue *q);
void xe_exec_queue_destroy(struct kref *ref);
void xe_exec_queue_assign_name(struct xe_exec_queue *q, u32 instance);

static inline struct xe_exec_queue *
xe_exec_queue_get_unless_zero(struct xe_exec_queue *q)
{
	if (kref_get_unless_zero(&q->refcount))
		return q;

	return NULL;
}

struct xe_exec_queue *xe_exec_queue_lookup(struct xe_file *xef, u32 id);

static inline struct xe_exec_queue *xe_exec_queue_get(struct xe_exec_queue *q)
{
	kref_get(&q->refcount);
	return q;
}

static inline void xe_exec_queue_put(struct xe_exec_queue *q)
{
	kref_put(&q->refcount, xe_exec_queue_destroy);
}

static inline bool xe_exec_queue_is_parallel(struct xe_exec_queue *q)
{
	return q->width > 1;
}

static inline bool xe_exec_queue_uses_pxp(struct xe_exec_queue *q)
{
	return q->pxp.type;
}

/**
 * xe_exec_queue_is_multi_queue() - Whether an exec_queue is part of a queue group.
 * @q: The exec_queue
 *
 * Return: True if the exec_queue is part of a queue group, false otherwise.
 */
static inline bool xe_exec_queue_is_multi_queue(struct xe_exec_queue *q)
{
	return q->multi_queue.valid;
}

/**
 * xe_exec_queue_is_multi_queue_primary() - Whether an exec_queue is primary queue
 * of a multi queue group.
 * @q: The exec_queue
 *
 * Return: True if @q is primary queue of a queue group, false otherwise.
 */
static inline bool xe_exec_queue_is_multi_queue_primary(struct xe_exec_queue *q)
{
	return q->multi_queue.is_primary;
}

/**
 * xe_exec_queue_is_multi_queue_secondary() - Whether an exec_queue is secondary queue
 * of a multi queue group.
 * @q: The exec_queue
 *
 * Return: True if @q is secondary queue of a queue group, false otherwise.
 */
static inline bool xe_exec_queue_is_multi_queue_secondary(struct xe_exec_queue *q)
{
	return xe_exec_queue_is_multi_queue(q) && !xe_exec_queue_is_multi_queue_primary(q);
}

/**
 * xe_exec_queue_multi_queue_primary() - Get multi queue group's primary queue
 * @q: The exec_queue
 *
 * If @q belongs to a multi queue group, then the primary queue of the group will
 * be returned. Otherwise, @q will be returned.
 */
static inline struct xe_exec_queue *xe_exec_queue_multi_queue_primary(struct xe_exec_queue *q)
{
	return xe_exec_queue_is_multi_queue(q) ? q->multi_queue.group->primary : q;
}

bool xe_exec_queue_is_lr(struct xe_exec_queue *q);

bool xe_exec_queue_is_idle(struct xe_exec_queue *q);

void xe_exec_queue_kill(struct xe_exec_queue *q);

int xe_exec_queue_create_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int xe_exec_queue_destroy_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file);
int xe_exec_queue_get_property_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file);
int xe_exec_queue_set_property_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file);
enum xe_exec_queue_priority xe_exec_queue_device_get_max_priority(struct xe_device *xe);

void xe_exec_queue_last_fence_put(struct xe_exec_queue *e, struct xe_vm *vm);
void xe_exec_queue_last_fence_put_unlocked(struct xe_exec_queue *e);
struct dma_fence *xe_exec_queue_last_fence_get(struct xe_exec_queue *e,
					       struct xe_vm *vm);
struct dma_fence *xe_exec_queue_last_fence_get_for_resume(struct xe_exec_queue *e,
							  struct xe_vm *vm);
void xe_exec_queue_last_fence_set(struct xe_exec_queue *e, struct xe_vm *vm,
				  struct dma_fence *fence);

void xe_exec_queue_tlb_inval_last_fence_put(struct xe_exec_queue *q,
					    struct xe_vm *vm,
					    unsigned int type);

void xe_exec_queue_tlb_inval_last_fence_put_unlocked(struct xe_exec_queue *q,
						     unsigned int type);

struct dma_fence *xe_exec_queue_tlb_inval_last_fence_get(struct xe_exec_queue *q,
							 struct xe_vm *vm,
							 unsigned int type);

void xe_exec_queue_tlb_inval_last_fence_set(struct xe_exec_queue *q,
					    struct xe_vm *vm,
					    struct dma_fence *fence,
					    unsigned int type);

void xe_exec_queue_update_run_ticks(struct xe_exec_queue *q);

int xe_exec_queue_contexts_hwsp_rebase(struct xe_exec_queue *q, void *scratch);

struct xe_lrc *xe_exec_queue_lrc(struct xe_exec_queue *q);

/**
 * xe_exec_queue_idle_skip_suspend() - Can exec queue skip suspend
 * @q: The exec_queue
 *
 * If an exec queue is not parallel and is idle, the suspend steps can be
 * skipped in the submission backend immediatley signaling the suspend fence.
 * Parallel queues cannot skip this step due to limitations in the submission
 * backend.
 *
 * Return: True if exec queue is idle and can skip suspend steps, False
 * otherwise
 */
static inline bool xe_exec_queue_idle_skip_suspend(struct xe_exec_queue *q)
{
	return !xe_exec_queue_is_parallel(q) && xe_exec_queue_is_idle(q);
}

#endif
