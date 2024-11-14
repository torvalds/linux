/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DEVCOREDUMP_H_
#define _XE_DEVCOREDUMP_H_

#include <linux/types.h>

struct drm_printer;
struct xe_device;
struct xe_exec_queue;
struct xe_sched_job;

#ifdef CONFIG_DEV_COREDUMP
void xe_devcoredump(struct xe_exec_queue *q, struct xe_sched_job *job);
int xe_devcoredump_init(struct xe_device *xe);
#else
static inline void xe_devcoredump(struct xe_exec_queue *q,
				  struct xe_sched_job *job)
{
}

static inline int xe_devcoredump_init(struct xe_device *xe)
{
	return 0;
}
#endif

void xe_print_blob_ascii85(struct drm_printer *p, const char *prefix,
			   const void *blob, size_t offset, size_t size);

#endif
