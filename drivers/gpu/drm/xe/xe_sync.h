/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_SYNC_H_
#define _XE_SYNC_H_

#include "xe_sync_types.h"

struct xe_device;
struct xe_exec_queue;
struct xe_file;
struct xe_sched_job;
struct xe_vm;

#define SYNC_PARSE_FLAG_EXEC			BIT(0)
#define SYNC_PARSE_FLAG_LR_MODE			BIT(1)
#define SYNC_PARSE_FLAG_DISALLOW_USER_FENCE	BIT(2)

int xe_sync_entry_parse(struct xe_device *xe, struct xe_file *xef,
			struct xe_sync_entry *sync,
			struct drm_xe_sync __user *sync_user,
			unsigned int flags);
int xe_sync_entry_wait(struct xe_sync_entry *sync);
int xe_sync_entry_add_deps(struct xe_sync_entry *sync,
			   struct xe_sched_job *job);
void xe_sync_entry_signal(struct xe_sync_entry *sync,
			  struct dma_fence *fence);
void xe_sync_entry_cleanup(struct xe_sync_entry *sync);
struct dma_fence *
xe_sync_in_fence_get(struct xe_sync_entry *sync, int num_sync,
		     struct xe_exec_queue *q, struct xe_vm *vm);

static inline bool xe_sync_is_ufence(struct xe_sync_entry *sync)
{
	return !!sync->ufence;
}

struct xe_user_fence *xe_sync_ufence_get(struct xe_sync_entry *sync);
void xe_sync_ufence_put(struct xe_user_fence *ufence);
int xe_sync_ufence_get_status(struct xe_user_fence *ufence);

#endif
