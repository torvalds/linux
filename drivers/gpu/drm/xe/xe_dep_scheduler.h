/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_DEP_SCHEDULER_H_
#define _XE_DEP_SCHEDULER_H_

#include <linux/types.h>

struct drm_sched_entity;
struct workqueue_struct;
struct xe_dep_scheduler;
struct xe_device;

struct xe_dep_scheduler *
xe_dep_scheduler_create(struct xe_device *xe,
			struct workqueue_struct *submit_wq,
			const char *name, u32 job_limit);

void xe_dep_scheduler_fini(struct xe_dep_scheduler *dep_scheduler);

struct drm_sched_entity *
xe_dep_scheduler_entity(struct xe_dep_scheduler *dep_scheduler);

#endif
