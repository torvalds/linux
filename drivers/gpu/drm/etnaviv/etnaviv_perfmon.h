/*
 * Copyright (C) 2017 Etnaviv Project
 * Copyright (C) 2017 Zodiac Inflight Innovations
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

#endif /* __ETNAVIV_PERFMON_H__ */
