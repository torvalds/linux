/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#ifndef __MSM_GEM_H__
#define __MSM_GEM_H__

#include "msm_drv.h"

struct msm_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	struct list_head mm_list;

	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	struct {
		// XXX
		uint32_t iova;
	} domain[NUM_DOMAINS];
};
#define to_msm_bo(x) container_of(x, struct msm_gem_object, base)

#endif /* __MSM_GEM_H__ */
