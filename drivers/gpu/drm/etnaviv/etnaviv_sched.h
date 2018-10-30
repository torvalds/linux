/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Etnaviv Project
 */

#ifndef __ETNAVIV_SCHED_H__
#define __ETNAVIV_SCHED_H__

#include <drm/gpu_scheduler.h>

struct etnaviv_gpu;

static inline
struct etnaviv_gem_submit *to_etnaviv_submit(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct etnaviv_gem_submit, sched_job);
}

int etnaviv_sched_init(struct etnaviv_gpu *gpu);
void etnaviv_sched_fini(struct etnaviv_gpu *gpu);
int etnaviv_sched_push_job(struct drm_sched_entity *sched_entity,
			   struct etnaviv_gem_submit *submit);

#endif /* __ETNAVIV_SCHED_H__ */
