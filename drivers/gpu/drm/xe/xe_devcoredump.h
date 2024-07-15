/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DEVCOREDUMP_H_
#define _XE_DEVCOREDUMP_H_

struct xe_device;
struct xe_sched_job;

#ifdef CONFIG_DEV_COREDUMP
void xe_devcoredump(struct xe_sched_job *job);
#else
static inline void xe_devcoredump(struct xe_sched_job *job)
{
}
#endif

#endif
