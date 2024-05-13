/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#ifndef __ETNAVIV_GEM_H__
#define __ETNAVIV_GEM_H__

#include <linux/dma-resv.h>
#include "etnaviv_cmdbuf.h"
#include "etnaviv_drv.h"

struct dma_fence;
struct etnaviv_gem_ops;
struct etnaviv_gem_object;

struct etnaviv_gem_userptr {
	uintptr_t ptr;
	struct mm_struct *mm;
	bool ro;
};

struct etnaviv_vram_mapping {
	struct list_head obj_node;
	struct list_head scan_node;
	struct list_head mmu_node;
	struct etnaviv_gem_object *object;
	struct etnaviv_iommu_context *context;
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
	u64 va;
	struct etnaviv_gem_object *obj;
	struct etnaviv_vram_mapping *mapping;
};

/* Created per submit-ioctl, to track bo's and cmdstream bufs, etc,
 * associated with the cmdstream submission for synchronization (and
 * make it easier to unwind when things go wrong, etc).
 */
struct etnaviv_gem_submit {
	struct drm_sched_job sched_job;
	struct kref refcount;
	struct etnaviv_file_private *ctx;
	struct etnaviv_gpu *gpu;
	struct etnaviv_iommu_context *mmu_context, *prev_mmu_context;
	struct dma_fence *out_fence;
	int out_fence_id;
	struct list_head node; /* GPU active submit list */
	struct etnaviv_cmdbuf cmdbuf;
	struct pid *pid;       /* submitting process */
	u32 exec_state;
	u32 flags;
	unsigned int nr_pmrs;
	struct etnaviv_perfmon_request *pmrs;
	unsigned int nr_bos;
	struct etnaviv_gem_submit_bo bos[];
	/* No new members here, the previous one is variable-length! */
};

void etnaviv_submit_put(struct etnaviv_gem_submit * submit);

int etnaviv_gem_wait_bo(struct etnaviv_gpu *gpu, struct drm_gem_object *obj,
	struct drm_etnaviv_timespec *timeout);
int etnaviv_gem_new_private(struct drm_device *dev, size_t size, u32 flags,
	const struct etnaviv_gem_ops *ops, struct etnaviv_gem_object **res);
void etnaviv_gem_obj_add(struct drm_device *dev, struct drm_gem_object *obj);
struct page **etnaviv_gem_get_pages(struct etnaviv_gem_object *obj);
void etnaviv_gem_put_pages(struct etnaviv_gem_object *obj);

struct etnaviv_vram_mapping *etnaviv_gem_mapping_get(
	struct drm_gem_object *obj, struct etnaviv_iommu_context *mmu_context,
	u64 va);
void etnaviv_gem_mapping_unreference(struct etnaviv_vram_mapping *mapping);

#endif /* __ETNAVIV_GEM_H__ */
