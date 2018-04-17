/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef __AMDGPU_OBJECT_H__
#define __AMDGPU_OBJECT_H__

#include <drm/amdgpu_drm.h>
#include "amdgpu.h"

#define AMDGPU_BO_INVALID_OFFSET	LONG_MAX

/* bo virtual addresses in a vm */
struct amdgpu_bo_va_mapping {
	struct amdgpu_bo_va		*bo_va;
	struct list_head		list;
	struct rb_node			rb;
	uint64_t			start;
	uint64_t			last;
	uint64_t			__subtree_last;
	uint64_t			offset;
	uint64_t			flags;
};

/* User space allocated BO in a VM */
struct amdgpu_bo_va {
	struct amdgpu_vm_bo_base	base;

	/* protected by bo being reserved */
	unsigned			ref_count;

	/* all other members protected by the VM PD being reserved */
	struct dma_fence	        *last_pt_update;

	/* mappings for this bo_va */
	struct list_head		invalids;
	struct list_head		valids;

	/* If the mappings are cleared or filled */
	bool				cleared;
};

struct amdgpu_bo {
	/* Protected by tbo.reserved */
	u32				preferred_domains;
	u32				allowed_domains;
	struct ttm_place		placements[AMDGPU_GEM_DOMAIN_MAX + 1];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	u64				flags;
	unsigned			pin_count;
	u64				tiling_flags;
	u64				metadata_flags;
	void				*metadata;
	u32				metadata_size;
	unsigned			prime_shared_count;
	/* list of all virtual address to which this bo is associated to */
	struct list_head		va;
	/* Constant after initialization */
	struct drm_gem_object		gem_base;
	struct amdgpu_bo		*parent;
	struct amdgpu_bo		*shadow;

	struct ttm_bo_kmap_obj		dma_buf_vmap;
	struct amdgpu_mn		*mn;

	union {
		struct list_head	mn_list;
		struct list_head	shadow_list;
	};
};

static inline struct amdgpu_bo *ttm_to_amdgpu_bo(struct ttm_buffer_object *tbo)
{
	return container_of(tbo, struct amdgpu_bo, tbo);
}

/**
 * amdgpu_mem_type_to_domain - return domain corresponding to mem_type
 * @mem_type:	ttm memory type
 *
 * Returns corresponding domain of the ttm mem_type
 */
static inline unsigned amdgpu_mem_type_to_domain(u32 mem_type)
{
	switch (mem_type) {
	case TTM_PL_VRAM:
		return AMDGPU_GEM_DOMAIN_VRAM;
	case TTM_PL_TT:
		return AMDGPU_GEM_DOMAIN_GTT;
	case TTM_PL_SYSTEM:
		return AMDGPU_GEM_DOMAIN_CPU;
	case AMDGPU_PL_GDS:
		return AMDGPU_GEM_DOMAIN_GDS;
	case AMDGPU_PL_GWS:
		return AMDGPU_GEM_DOMAIN_GWS;
	case AMDGPU_PL_OA:
		return AMDGPU_GEM_DOMAIN_OA;
	default:
		break;
	}
	return 0;
}

/**
 * amdgpu_bo_reserve - reserve bo
 * @bo:		bo structure
 * @no_intr:	don't return -ERESTARTSYS on pending signal
 *
 * Returns:
 * -ERESTARTSYS: A wait for the buffer to become unreserved was interrupted by
 * a signal. Release all buffer reservations and return to user-space.
 */
static inline int amdgpu_bo_reserve(struct amdgpu_bo *bo, bool no_intr)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	int r;

	r = ttm_bo_reserve(&bo->tbo, !no_intr, false, NULL);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(adev->dev, "%p reserve failed\n", bo);
		return r;
	}
	return 0;
}

static inline void amdgpu_bo_unreserve(struct amdgpu_bo *bo)
{
	ttm_bo_unreserve(&bo->tbo);
}

static inline unsigned long amdgpu_bo_size(struct amdgpu_bo *bo)
{
	return bo->tbo.num_pages << PAGE_SHIFT;
}

static inline unsigned amdgpu_bo_ngpu_pages(struct amdgpu_bo *bo)
{
	return (bo->tbo.num_pages << PAGE_SHIFT) / AMDGPU_GPU_PAGE_SIZE;
}

static inline unsigned amdgpu_bo_gpu_page_alignment(struct amdgpu_bo *bo)
{
	return (bo->tbo.mem.page_alignment << PAGE_SHIFT) / AMDGPU_GPU_PAGE_SIZE;
}

/**
 * amdgpu_bo_mmap_offset - return mmap offset of bo
 * @bo:	amdgpu object for which we query the offset
 *
 * Returns mmap offset of the object.
 */
static inline u64 amdgpu_bo_mmap_offset(struct amdgpu_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->tbo.vma_node);
}

/**
 * amdgpu_bo_gpu_accessible - return whether the bo is currently in memory that
 * is accessible to the GPU.
 */
static inline bool amdgpu_bo_gpu_accessible(struct amdgpu_bo *bo)
{
	switch (bo->tbo.mem.mem_type) {
	case TTM_PL_TT: return amdgpu_ttm_is_bound(bo->tbo.ttm);
	case TTM_PL_VRAM: return true;
	default: return false;
	}
}

/**
 * amdgpu_bo_explicit_sync - return whether the bo is explicitly synced
 */
static inline bool amdgpu_bo_explicit_sync(struct amdgpu_bo *bo)
{
	return bo->flags & AMDGPU_GEM_CREATE_EXPLICIT_SYNC;
}

int amdgpu_bo_create(struct amdgpu_device *adev,
			    unsigned long size, int byte_align,
			    bool kernel, u32 domain, u64 flags,
			    struct sg_table *sg,
			    struct reservation_object *resv,
			    uint64_t init_value,
			    struct amdgpu_bo **bo_ptr);
int amdgpu_bo_create_reserved(struct amdgpu_device *adev,
			      unsigned long size, int align,
			      u32 domain, struct amdgpu_bo **bo_ptr,
			      u64 *gpu_addr, void **cpu_addr);
int amdgpu_bo_create_kernel(struct amdgpu_device *adev,
			    unsigned long size, int align,
			    u32 domain, struct amdgpu_bo **bo_ptr,
			    u64 *gpu_addr, void **cpu_addr);
void amdgpu_bo_free_kernel(struct amdgpu_bo **bo, u64 *gpu_addr,
			   void **cpu_addr);
int amdgpu_bo_kmap(struct amdgpu_bo *bo, void **ptr);
void *amdgpu_bo_kptr(struct amdgpu_bo *bo);
void amdgpu_bo_kunmap(struct amdgpu_bo *bo);
struct amdgpu_bo *amdgpu_bo_ref(struct amdgpu_bo *bo);
void amdgpu_bo_unref(struct amdgpu_bo **bo);
int amdgpu_bo_pin(struct amdgpu_bo *bo, u32 domain, u64 *gpu_addr);
int amdgpu_bo_pin_restricted(struct amdgpu_bo *bo, u32 domain,
			     u64 min_offset, u64 max_offset,
			     u64 *gpu_addr);
int amdgpu_bo_unpin(struct amdgpu_bo *bo);
int amdgpu_bo_evict_vram(struct amdgpu_device *adev);
int amdgpu_bo_init(struct amdgpu_device *adev);
void amdgpu_bo_fini(struct amdgpu_device *adev);
int amdgpu_bo_fbdev_mmap(struct amdgpu_bo *bo,
				struct vm_area_struct *vma);
int amdgpu_bo_set_tiling_flags(struct amdgpu_bo *bo, u64 tiling_flags);
void amdgpu_bo_get_tiling_flags(struct amdgpu_bo *bo, u64 *tiling_flags);
int amdgpu_bo_set_metadata (struct amdgpu_bo *bo, void *metadata,
			    uint32_t metadata_size, uint64_t flags);
int amdgpu_bo_get_metadata(struct amdgpu_bo *bo, void *buffer,
			   size_t buffer_size, uint32_t *metadata_size,
			   uint64_t *flags);
void amdgpu_bo_move_notify(struct ttm_buffer_object *bo,
			   bool evict,
			   struct ttm_mem_reg *new_mem);
int amdgpu_bo_fault_reserve_notify(struct ttm_buffer_object *bo);
void amdgpu_bo_fence(struct amdgpu_bo *bo, struct dma_fence *fence,
		     bool shared);
u64 amdgpu_bo_gpu_offset(struct amdgpu_bo *bo);
int amdgpu_bo_backup_to_shadow(struct amdgpu_device *adev,
			       struct amdgpu_ring *ring,
			       struct amdgpu_bo *bo,
			       struct reservation_object *resv,
			       struct dma_fence **fence, bool direct);
int amdgpu_bo_validate(struct amdgpu_bo *bo);
int amdgpu_bo_restore_from_shadow(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring,
				  struct amdgpu_bo *bo,
				  struct reservation_object *resv,
				  struct dma_fence **fence,
				  bool direct);


/*
 * sub allocation
 */

static inline uint64_t amdgpu_sa_bo_gpu_addr(struct amdgpu_sa_bo *sa_bo)
{
	return sa_bo->manager->gpu_addr + sa_bo->soffset;
}

static inline void * amdgpu_sa_bo_cpu_addr(struct amdgpu_sa_bo *sa_bo)
{
	return sa_bo->manager->cpu_ptr + sa_bo->soffset;
}

int amdgpu_sa_bo_manager_init(struct amdgpu_device *adev,
				     struct amdgpu_sa_manager *sa_manager,
				     unsigned size, u32 align, u32 domain);
void amdgpu_sa_bo_manager_fini(struct amdgpu_device *adev,
				      struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_manager_start(struct amdgpu_device *adev,
				      struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_manager_suspend(struct amdgpu_device *adev,
					struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_new(struct amdgpu_sa_manager *sa_manager,
		     struct amdgpu_sa_bo **sa_bo,
		     unsigned size, unsigned align);
void amdgpu_sa_bo_free(struct amdgpu_device *adev,
			      struct amdgpu_sa_bo **sa_bo,
			      struct dma_fence *fence);
#if defined(CONFIG_DEBUG_FS)
void amdgpu_sa_bo_dump_debug_info(struct amdgpu_sa_manager *sa_manager,
					 struct seq_file *m);
#endif


#endif
