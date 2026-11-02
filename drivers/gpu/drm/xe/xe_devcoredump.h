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
struct xe_gt;
struct xe_sched_job;

#ifdef CONFIG_DEV_COREDUMP
void __xe_devcoredump(struct xe_gt *gt, struct xe_exec_queue *q,
		      struct xe_sched_job *job, const char *fmt, ...);
int xe_devcoredump_init(struct xe_device *xe);
#else
static inline void __xe_devcoredump(struct xe_gt *gt, struct xe_exec_queue *q,
				    struct xe_sched_job *job,
				    const char *fmt, ...)
{
}

static inline int xe_devcoredump_init(struct xe_device *xe)
{
	return 0;
}
#endif

#define xe_devcoredump(_q, _job, _fmt, ...) \
	__xe_devcoredump((_q)->gt, _q, _job, _fmt, ##__VA_ARGS__)
#define xe_devcoredump_gt(_gt, _fmt, ...) \
	__xe_devcoredump(_gt, NULL, NULL, _fmt, ##__VA_ARGS__)

void xe_print_blob_ascii85(struct drm_printer *p, const char *prefix, char suffix,
			   const void *blob, size_t offset, size_t size);

#endif
