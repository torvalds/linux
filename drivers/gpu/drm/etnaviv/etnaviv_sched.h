/*
 * Copyright (C) 2017 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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
