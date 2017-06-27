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

#include <linux/kref.h>
#include <linux/reservation.h>
#include "msm_drv.h"

/* Additional internal-use only BO flags: */
#define MSM_BO_STOLEN        0x10000000    /* try to use stolen/splash memory */

struct msm_gem_address_space {
	const char *name;
	/* NOTE: mm managed at the page level, size is in # of pages
	 * and position mm_node->start is in # of pages:
	 */
	struct drm_mm mm;
	spinlock_t lock; /* Protects drm_mm node allocation/removal */
	struct msm_mmu *mmu;
	struct kref kref;
};

struct msm_gem_vma {
	struct drm_mm_node node;
	uint64_t iova;
	struct msm_gem_address_space *aspace;
	struct list_head list;    /* node in msm_gem_object::vmas */
};

struct msm_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	/**
	 * Advice: are the backing pages purgeable?
	 */
	uint8_t madv;

	/**
	 * count of active vmap'ing
	 */
	uint8_t vmap_count;

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

	/* Transiently in the process of submit ioctl, objects associated
	 * with the submit are on submit->bo_list.. this only lasts for
	 * the duration of the ioctl, so one bo can never be on multiple
	 * submit lists.
	 */
	struct list_head submit_entry;

	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	struct list_head vmas;    /* list of msm_gem_vma */

	/* normally (resv == &_resv) except for imported bo's */
	struct reservation_object *resv;
	struct reservation_object _resv;

	/* For physically contiguous buffers.  Used when we don't have
	 * an IOMMU.  Also used for stolen/splashscreen buffer.
	 */
	struct drm_mm_node *vram_node;
	struct mutex lock; /* Protects resources associated with bo */
};
#define to_msm_bo(x) container_of(x, struct msm_gem_object, base)

static inline bool is_active(struct msm_gem_object *msm_obj)
{
	return msm_obj->gpu != NULL;
}

static inline bool is_purgeable(struct msm_gem_object *msm_obj)
{
	WARN_ON(!mutex_is_locked(&msm_obj->base.dev->struct_mutex));
	return (msm_obj->madv == MSM_MADV_DONTNEED) && msm_obj->sgt &&
			!msm_obj->base.dma_buf && !msm_obj->base.import_attach;
}

static inline bool is_vunmapable(struct msm_gem_object *msm_obj)
{
	return (msm_obj->vmap_count == 0) && msm_obj->vaddr;
}

/* The shrinker can be triggered while we hold objA->lock, and need
 * to grab objB->lock to purge it.  Lockdep just sees these as a single
 * class of lock, so we use subclasses to teach it the difference.
 *
 * OBJ_LOCK_NORMAL is implicit (ie. normal mutex_lock() call), and
 * OBJ_LOCK_SHRINKER is used by shrinker.
 *
 * It is *essential* that we never go down paths that could trigger the
 * shrinker for a purgable object.  This is ensured by checking that
 * msm_obj->madv == MSM_MADV_WILLNEED.
 */
enum msm_gem_lock {
	OBJ_LOCK_NORMAL,
	OBJ_LOCK_SHRINKER,
};

void msm_gem_purge(struct drm_gem_object *obj, enum msm_gem_lock subclass);
void msm_gem_vunmap(struct drm_gem_object *obj, enum msm_gem_lock subclass);

/* Created per submit-ioctl, to track bo's and cmdstream bufs, etc,
 * associated with the cmdstream submission for synchronization (and
 * make it easier to unwind when things go wrong, etc).  This only
 * lasts for the duration of the submit-ioctl.
 */
struct msm_gem_submit {
	struct drm_device *dev;
	struct msm_gpu *gpu;
	struct list_head node;   /* node in gpu submit_list */
	struct list_head bo_list;
	struct ww_acquire_ctx ticket;
	struct dma_fence *fence;
	struct pid *pid;    /* submitting process */
	bool valid;         /* true if no cmdstream patching needed */
	unsigned int nr_cmds;
	unsigned int nr_bos;
	struct {
		uint32_t type;
		uint32_t size;  /* in dwords */
		uint64_t iova;
		uint32_t idx;   /* cmdstream buffer idx in bos[] */
	} *cmd;  /* array of size nr_cmds */
	struct {
		uint32_t flags;
		struct msm_gem_object *obj;
		uint64_t iova;
	} bos[0];
};

#endif /* __MSM_GEM_H__ */
