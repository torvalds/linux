/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Etnaviv Project
 * Copyright (C) 2017 Zodiac Inflight Innovations
 */

#ifndef __ETNAVIV_PERFMON_H__
#define __ETNAVIV_PERFMON_H__

struct etnaviv_gpu;
struct drm_etnaviv_pm_domain;
struct drm_etnaviv_pm_signal;

struct etnaviv_perfmon_request
{
	u32 flags;
	u8 domain;
	u8 signal;
	u32 sequence;

	/* bo to store a value */
	u32 *bo_vma;
	u32 offset;
};

int etnaviv_pm_query_dom(struct etnaviv_gpu *gpu,
	struct drm_etnaviv_pm_domain *domain);

int etnaviv_pm_query_sig(struct etnaviv_gpu *gpu,
	struct drm_etnaviv_pm_signal *signal);

int etnaviv_pm_req_validate(const struct drm_etnaviv_gem_submit_pmr *r,
	u32 exec_state);

void etnaviv_perfmon_process(struct etnaviv_gpu *gpu,
	const struct etnaviv_perfmon_request *pmr, u32 exec_state);

#endif /* __ETNAVIV_PERFMON_H__ */
