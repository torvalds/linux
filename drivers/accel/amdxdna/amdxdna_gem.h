/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_GEM_H_
#define _AMDXDNA_GEM_H_

#include <drm/drm_gem_shmem_helper.h>
#include <linux/hmm.h>
#include <linux/iova.h>
#include "amdxdna_pci_drv.h"

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
	void				*kva;
	u64				dma_addr;
	size_t				size;
	struct list_head		umap_list;
	bool				map_invalid;
	/*
	 * Cache the first mmap uva as PASID addr, which can be accessed by driver
	 * without taking notifier_lock.
	 */
	u64				uva;
};

struct amdxdna_gem_obj {
	struct drm_gem_shmem_object	base;
	struct amdxdna_client		*client;
	u8				type;
	bool				pinned;
	struct mutex			lock; /* Protects: pinned, mem.kva, open_ref */
	struct amdxdna_mem		mem;
	int				open_ref;

	/* Below members are initialized when needed */
	struct drm_mm			mm; /* For AMDXDNA_BO_DEV_HEAP */
	struct drm_mm_node		mm_node; /* For AMDXDNA_BO_DEV */
	u32				assigned_hwctx;
	struct dma_buf			*dma_buf;
	struct dma_buf_attachment	*attach;

	/* True, if BO is managed by XRT, not application */
	bool				internal;
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

void *amdxdna_gem_vmap(struct amdxdna_gem_obj *abo);
u64 amdxdna_gem_uva(struct amdxdna_gem_obj *abo);
u64 amdxdna_gem_dev_addr(struct amdxdna_gem_obj *abo);

static inline u64 amdxdna_dev_bo_offset(struct amdxdna_gem_obj *abo)
{
	return amdxdna_gem_dev_addr(abo) - amdxdna_gem_dev_addr(abo->client->dev_heap);
}

static inline u64 amdxdna_obj_dma_addr(struct amdxdna_gem_obj *abo)
{
	return amdxdna_pasid_on(abo->client) ? amdxdna_gem_uva(abo) : abo->mem.dma_addr;
}

void amdxdna_umap_put(struct amdxdna_umap *mapp);

struct drm_gem_object *
amdxdna_gem_create_shmem_object_cb(struct drm_device *dev, size_t size);
struct drm_gem_object *
amdxdna_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf);
struct amdxdna_gem_obj *
amdxdna_drm_create_dev_bo(struct drm_device *dev,
			  struct amdxdna_drm_create_bo *args, struct drm_file *filp);

int amdxdna_gem_pin_nolock(struct amdxdna_gem_obj *abo);
int amdxdna_gem_pin(struct amdxdna_gem_obj *abo);
void amdxdna_gem_unpin(struct amdxdna_gem_obj *abo);

int amdxdna_drm_create_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_get_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_sync_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_get_bo_usage(struct drm_device *dev, struct amdxdna_drm_get_array *args);

#endif /* _AMDXDNA_GEM_H_ */
