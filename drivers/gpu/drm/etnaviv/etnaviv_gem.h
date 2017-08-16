/*
 * Copyright (C) 2015 Etnaviv Project
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

#ifndef __ETNAVIV_GEM_H__
#define __ETNAVIV_GEM_H__

#include <linux/reservation.h>
#include "etnaviv_drv.h"

struct dma_fence;
struct etnaviv_gem_ops;
struct etnaviv_gem_object;

struct etnaviv_gem_userptr {
	uintptr_t ptr;
	struct task_struct *task;
	struct work_struct *work;
	bool ro;
};

struct etnaviv_vram_mapping {
	struct list_head obj_node;
	struct list_head scan_node;
	struct list_head mmu_node;
	struct etnaviv_gem_object *object;
	struct etnaviv_iommu *mmu;
	struct drm_mm_node vram_node;
	unsigned int use;
	u32 iova;
};

struct etnaviv_gem_object {
	struct drm_gem_object base;
	const struct etnaviv_gem_ops *ops;
	struct mutex lock;

	u32 flags;

	struct list_head gem_node;
	struct etnaviv_gpu *gpu;     /* non-null if active */
	atomic_t gpu_active;
	u32 access;

	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	/* normally (resv == &_resv) except for imported bo's */
	struct reservation_object *resv;
	struct reservation_object _resv;

	struct list_head vram_list;

	/* cache maintenance */
	u32 last_cpu_prep_op;

	struct etnaviv_gem_userptr userptr;
};

static inline
struct etnaviv_gem_object *to_etnaviv_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct etnaviv_gem_object, base);
}

struct etnaviv_gem_ops {
	int (*get_pages)(struct etnaviv_gem_object *);
	void (*release)(struct etnaviv_gem_object *);
	void *(*vmap)(struct etnaviv_gem_object *);
	int (*mmap)(struct etnaviv_gem_object *, struct vm_area_struct *);
};

static inline bool is_active(struct etnaviv_gem_object *etnaviv_obj)
{
	return atomic_read(&etnaviv_obj->gpu_active) != 0;
}

#define MAX_CMDS 4

struct etnaviv_gem_submit_bo {
	u32 flags;
	struct etnaviv_gem_object *obj;
	struct etnaviv_vram_mapping *mapping;
};

/* Created per submit-ioctl, to track bo's and cmdstream bufs, etc,
 * associated with the cmdstream submission for synchronization (and
 * make it easier to unwind when things go wrong, etc).  This only
 * lasts for the duration of the submit-ioctl.
 */
struct etnaviv_gem_submit {
	struct drm_device *dev;
	struct etnaviv_gpu *gpu;
	struct ww_acquire_ctx ticket;
	struct dma_fence *fence;
	u32 flags;
	unsigned int nr_bos;
	struct etnaviv_gem_submit_bo bos[0];
	/* No new members here, the previous one is variable-length! */
};

int etnaviv_gem_wait_bo(struct etnaviv_gpu *gpu, struct drm_gem_object *obj,
	struct timespec *timeout);
int etnaviv_gem_new_private(struct drm_device *dev, size_t size, u32 flags,
	struct reservation_object *robj, const struct etnaviv_gem_ops *ops,
	struct etnaviv_gem_object **res);
int etnaviv_gem_obj_add(struct drm_device *dev, struct drm_gem_object *obj);
struct page **etnaviv_gem_get_pages(struct etnaviv_gem_object *obj);
void etnaviv_gem_put_pages(struct etnaviv_gem_object *obj);

struct etnaviv_vram_mapping *etnaviv_gem_mapping_get(
	struct drm_gem_object *obj, struct etnaviv_gpu *gpu);
void etnaviv_gem_mapping_reference(struct etnaviv_vram_mapping *mapping);
void etnaviv_gem_mapping_unreference(struct etnaviv_vram_mapping *mapping);

#endif /* __ETNAVIV_GEM_H__ */
