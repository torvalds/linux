/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_GEM_H_
#define _AMDXDNA_GEM_H_

#include <linux/hmm.h>

struct amdxdna_umap {
	struct vm_area_struct		*vma;
	struct mmu_interval_notifier	notifier;
	struct hmm_range		range;
	struct work_struct		hmm_unreg_work;
	struct amdxdna_gem_obj		*abo;
	struct list_head		node;
	struct kref			refcnt;
	bool				invalid;
	bool				unmapped;
};

struct amdxdna_mem {
	u64				userptr;
	void				*kva;
	u64				dev_addr;
	size_t				size;
	struct page			**pages;
	u32				nr_pages;
	struct list_head		umap_list;
	bool				map_invalid;
};

struct amdxdna_gem_obj {
	struct drm_gem_shmem_object	base;
	struct amdxdna_client		*client;
	u8				type;
	bool				pinned;
	struct mutex			lock; /* Protects: pinned */
	struct amdxdna_mem		mem;

	/* Below members is uninitialized when needed */
	struct drm_mm			mm; /* For AMDXDNA_BO_DEV_HEAP */
	struct drm_mm_node		mm_node; /* For AMDXDNA_BO_DEV */
	u32				assigned_hwctx;
	struct dma_buf			*dma_buf;
	struct dma_buf_attachment	*attach;
};

#define to_gobj(obj)    (&(obj)->base.base)
#define is_import_bo(obj) ((obj)->attach)

static inline struct amdxdna_gem_obj *to_xdna_obj(struct drm_gem_object *gobj)
{
	return container_of(gobj, struct amdxdna_gem_obj, base.base);
}

struct amdxdna_gem_obj *amdxdna_gem_get_obj(struct amdxdna_client *client,
					    u32 bo_hdl, u8 bo_type);
static inline void amdxdna_gem_put_obj(struct amdxdna_gem_obj *abo)
{
	drm_gem_object_put(to_gobj(abo));
}

void amdxdna_umap_put(struct amdxdna_umap *mapp);

struct drm_gem_object *
amdxdna_gem_create_object_cb(struct drm_device *dev, size_t size);
struct drm_gem_object *
amdxdna_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf);
struct amdxdna_gem_obj *
amdxdna_drm_alloc_dev_bo(struct drm_device *dev,
			 struct amdxdna_drm_create_bo *args,
			 struct drm_file *filp);

int amdxdna_gem_pin_nolock(struct amdxdna_gem_obj *abo);
int amdxdna_gem_pin(struct amdxdna_gem_obj *abo);
void amdxdna_gem_unpin(struct amdxdna_gem_obj *abo);

int amdxdna_drm_create_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_get_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_sync_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

#endif /* _AMDXDNA_GEM_H_ */
