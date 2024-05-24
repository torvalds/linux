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

struct xe_exec_queue *xe_exec_queue_create(struct xe_device *xe, struct xe_vm *vm,
					   u32 logical_mask, u16 width,
					   struct xe_hw_engine *hw_engine, u32 flags,
					   u64 extensions);
struct xe_exec_queue *xe_exec_queue_create_class(struct xe_device *xe, struct xe_gt *gt,
						 struct xe_vm *vm,
						 enum xe_engine_class class, u32 flags);

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

bool xe_exec_queue_is_lr(struct xe_exec_queue *q);

bool xe_exec_queue_ring_full(struct xe_exec_queue *q);

bool xe_exec_queue_is_idle(struct xe_exec_queue *q);

void xe_exec_queue_kill(struct xe_exec_queue *q);

int xe_exec_queue_create_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int xe_exec_queue_destroy_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file);
int xe_exec_queue_get_property_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file);
enum xe_exec_queue_priority xe_exec_queue_device_get_max_priority(struct xe_device *xe);

void xe_exec_queue_last_fence_put(struct xe_exec_queue *e, struct xe_vm *vm);
void xe_exec_queue_last_fence_put_unlocked(struct xe_exec_queue *e);
struct dma_fence *xe_exec_queue_last_fence_get(struct xe_exec_queue *e,
					       struct xe_vm *vm);
void xe_exec_queue_last_fence_set(struct xe_exec_queue *e, struct xe_vm *vm,
				  struct dma_fence *fence);
void xe_exec_queue_update_run_ticks(struct xe_exec_queue *q);

#endif
