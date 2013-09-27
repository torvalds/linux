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

#include <linux/reservation.h>
#include "msm_drv.h"

struct msm_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	/* And object is either:
	 *  inactive - on priv->inactive_list
	 *  active   - on one one of the gpu's active_list..  well, at
	 *     least for now we don't have (I don't think) hw sync between
	 *     2d and 3d one devices which have both, meaning we need to
	 *     block on submit if a bo is already on other ring
	 *
	 */
	struct list_head mm_list;
	struct msm_gpu *gpu;     /* non-null if active */
	uint32_t fence;

	/* Transiently in the process of submit ioctl, objects associated
	 * with the submit are on submit->bo_list.. this only lasts for
	 * the duration of the ioctl, so one bo can never be on multiple
	 * submit lists.
	 */
	struct list_head submit_entry;

	/* work defered until bo is inactive: */
	struct list_head inactive_work;

	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	struct {
		// XXX
		uint32_t iova;
	} domain[NUM_DOMAINS];

	/* normally (resv == &_resv) except for imported bo's */
	struct reservation_object *resv;
	struct reservation_object _resv;
};
#define to_msm_bo(x) container_of(x, struct msm_gem_object, base)

static inline bool is_active(struct msm_gem_object *msm_obj)
{
	return msm_obj->gpu != NULL;
}

#define MAX_CMDS 4

/* Created per submit-ioctl, to track bo's and cmdstream bufs, etc,
 * associated with the cmdstream submission for synchronization (and
 * make it easier to unwind when things go wrong, etc).  This only
 * lasts for the duration of the submit-ioctl.
 */
struct msm_gem_submit {
	struct drm_device *dev;
	struct msm_gpu *gpu;
	struct list_head bo_list;
	struct ww_acquire_ctx ticket;
	uint32_t fence;
	bool valid;
	unsigned int nr_cmds;
	unsigned int nr_bos;
	struct {
		uint32_t type;
		uint32_t size;  /* in dwords */
		uint32_t iova;
	} cmd[MAX_CMDS];
	struct {
		uint32_t flags;
		struct msm_gem_object *obj;
		uint32_t iova;
	} bos[0];
};

#endif /* __MSM_GEM_H__ */
