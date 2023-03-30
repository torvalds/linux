/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_SYNC_H_
#define _XE_SYNC_H_

#include "xe_sync_types.h"

struct xe_device;
struct xe_file;
struct xe_sched_job;

int xe_sync_entry_parse(struct xe_device *xe, struct xe_file *xef,
			struct xe_sync_entry *sync,
			struct drm_xe_sync __user *sync_user,
			bool exec, bool compute_mode);
int xe_sync_entry_wait(struct xe_sync_entry *sync);
int xe_sync_entry_add_deps(struct xe_sync_entry *sync,
			   struct xe_sched_job *job);
bool xe_sync_entry_signal(struct xe_sync_entry *sync,
			  struct xe_sched_job *job,
			  struct dma_fence *fence);
void xe_sync_entry_cleanup(struct xe_sync_entry *sync);

#endif
