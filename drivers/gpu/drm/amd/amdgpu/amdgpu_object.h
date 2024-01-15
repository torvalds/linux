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
#include "amdgpu_res_cursor.h"

#ifdef CONFIG_MMU_NOTIFIER
#include <linux/mmu_notifier.h>
#endif

#define AMDGPU_BO_INVALID_OFFSET	LONG_MAX
#define AMDGPU_BO_MAX_PLACEMENTS	3

/* BO flag to indicate a KFD userptr BO */
#define AMDGPU_AMDKFD_CREATE_USERPTR_BO	(1ULL << 63)

#define to_amdgpu_bo_user(abo) container_of((abo), struct amdgpu_bo_user, bo)
#define to_amdgpu_bo_vm(abo) container_of((abo), struct amdgpu_bo_vm, bo)

struct amdgpu_bo_param {
	unsigned long			size;
	int				byte_align;
	u32				bo_ptr_size;
	u32				domain;
	u32				preferred_domain;
	u64				flags;
	enum ttm_bo_type		type;
	bool				no_wait_gpu;
	struct dma_resv			*resv;
	void				(*destroy)(struct ttm_buffer_object *bo);
	/* xcp partition number plus 1, 0 means any partition */
	int8_t				xcp_id_plus1;
};

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

	bool				is_xgmi;
};

struct amdgpu_bo {
	/* Protected by tbo.reserved */
	u32				preferred_domains;
	u32				allowed_domains;
	struct ttm_place		placements[AMDGPU_BO_MAX_PLACEMENTS];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	u64				flags;
	/* per VM structure for page tables and with virtual addresses */
	struct amdgpu_vm_bo_base	*vm_bo;
	/* Constant after initialization */
	struct amdgpu_bo		*parent;

#ifdef CONFIG_MMU_NOTIFIER
	struct mmu_interval_notifier	notifier;
#endif
	struct kgd_mem                  *kfd_bo;

	/*
	 * For GPUs with spatial partitioning, xcp partition number, -1 means
	 * any partition. For other ASICs without spatial partition, always 0
	 * for memory accounting.
	 */
	int8_t				xcp_id;
};

struct amdgpu_bo_user {
	struct amdgpu_bo		bo;
	u64				tiling_flags;
	u64				metadata_flags;
	void				*metadata;
	u32				metadata_size;

};

struct amdgpu_bo_vm {
	struct amdgpu_bo		bo;
	struct amdgpu_bo		*shadow;
	struct list_head		shadow_list;
	struct amdgpu_vm_bo_base        entries[];
};

struct amdgpu_mem_stats {
	/* current VRAM usage, includes visible VRAM */
	uint64_t vram;
	/* current visible VRAM usage */
	uint64_t visible_vram;
	/* current GTT usage */
	uint64_t gtt;
	/* current system memory usage */
	uint64_t cpu;
	/* sum of evicted buffers, includes visible VRAM */
	uint64_t evicted_vram;
	/* sum of evicted buffers due to CPU access */
	uint64_t evicted_visible_vram;
	/* how much userspace asked for, includes vis.VRAM */
	uint64_t requested_vram;
	/* how much userspace asked for */
	uint64_t requested_visible_vram;
	/* how much userspace asked for */
	uint64_t requested_gtt;
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
	case AMDGPU_PL_DOORBELL:
		return AMDGPU_GEM_DOMAIN_DOORBELL;
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
	return bo->tbo.base.size;
}

static inline unsigned amdgpu_bo_ngpu_pages(struct amdgpu_bo *bo)
{
	return bo->tbo.base.size / AMDGPU_GPU_PAGE_SIZE;
}

static inline unsigned amdgpu_bo_gpu_page_alignment(struct amdgpu_bo *bo)
{
	return (bo->tbo.page_alignment << PAGE_SHIFT) / AMDGPU_GPU_PAGE_SIZE;
}

/**
 * amdgpu_bo_mmap_offset - return mmap offset of bo
 * @bo:	amdgpu object for which we query the offset
 *
 * Returns mmap offset of the object.
 */
static inline u64 amdgpu_bo_mmap_offset(struct amdgpu_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->tbo.base.vma_node);
}

/**
 * amdgpu_bo_in_cpu_visible_vram - check if BO is (partly) in visible VRAM
 */
static inline bool amdgpu_bo_in_cpu_visible_vram(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_res_cursor cursor;

	if (!bo->tbo.resource || bo->tbo.resource->mem_type != TTM_PL_VRAM)
		return false;

	amdgpu_res_first(bo->tbo.resource, 0, amdgpu_bo_size(bo), &cursor);
	while (cursor.remaining) {
		if (cursor.start < adev->gmc.visible_vram_size)
			return true;

		amdgpu_res_next(&cursor, cursor.size);
	}

	return false;
}

/**
 * amdgpu_bo_explicit_sync - return whether the bo is explicitly synced
 */
static inline bool amdgpu_bo_explicit_sync(struct amdgpu_bo *bo)
{
	return bo->flags & AMDGPU_GEM_CREATE_EXPLICIT_SYNC;
}

/**
 * amdgpu_bo_encrypted - test if the BO is encrypted
 * @bo: pointer to a buffer object
 *
 * Return true if the buffer object is encrypted, false otherwise.
 */
static inline bool amdgpu_bo_encrypted(struct amdgpu_bo *bo)
{
	return bo->flags & AMDGPU_GEM_CREATE_ENCRYPTED;
}

/**
 * amdgpu_bo_shadowed - check if the BO is shadowed
 *
 * @bo: BO to be tested.
 *
 * Returns:
 * NULL if not shadowed or else return a BO pointer.
 */
static inline struct amdgpu_bo *amdgpu_bo_shadowed(struct amdgpu_bo *bo)
{
	if (bo->tbo.type == ttm_bo_type_kernel)
		return to_amdgpu_bo_vm(bo)->shadow;

	return NULL;
}

bool amdgpu_bo_is_amdgpu_bo(struct ttm_buffer_object *bo);
void amdgpu_bo_placement_from_domain(struct amdgpu_bo *abo, u32 domain);

int amdgpu_bo_create(struct amdgpu_device *adev,
		     struct amdgpu_bo_param *bp,
		     struct amdgpu_bo **bo_ptr);
int amdgpu_bo_create_reserved(struct amdgpu_device *adev,
			      unsigned long size, int align,
			      u32 domain, struct amdgpu_bo **bo_ptr,
			      u64 *gpu_addr, void **cpu_addr);
int amdgpu_bo_create_kernel(struct amdgpu_device *adev,
			    unsigned long size, int align,
			    u32 domain, struct amdgpu_bo **bo_ptr,
			    u64 *gpu_addr, void **cpu_addr);
int amdgpu_bo_create_kernel_at(struct amdgpu_device *adev,
			       uint64_t offset, uint64_t size,
			       struct amdgpu_bo **bo_ptr, void **cpu_addr);
int amdgpu_bo_create_user(struct amdgpu_device *adev,
			  struct amdgpu_bo_param *bp,
			  struct amdgpu_bo_user **ubo_ptr);
int amdgpu_bo_create_vm(struct amdgpu_device *adev,
			struct amdgpu_bo_param *bp,
			struct amdgpu_bo_vm **ubo_ptr);
void amdgpu_bo_free_kernel(struct amdgpu_bo **bo, u64 *gpu_addr,
			   void **cpu_addr);
int amdgpu_bo_kmap(struct amdgpu_bo *bo, void **ptr);
void *amdgpu_bo_kptr(struct amdgpu_bo *bo);
void amdgpu_bo_kunmap(struct amdgpu_bo *bo);
struct amdgpu_bo *amdgpu_bo_ref(struct amdgpu_bo *bo);
void amdgpu_bo_unref(struct amdgpu_bo **bo);
int amdgpu_bo_pin(struct amdgpu_bo *bo, u32 domain);
int amdgpu_bo_pin_restricted(struct amdgpu_bo *bo, u32 domain,
			     u64 min_offset, u64 max_offset);
void amdgpu_bo_unpin(struct amdgpu_bo *bo);
int amdgpu_bo_init(struct amdgpu_device *adev);
void amdgpu_bo_fini(struct amdgpu_device *adev);
int amdgpu_bo_set_tiling_flags(struct amdgpu_bo *bo, u64 tiling_flags);
void amdgpu_bo_get_tiling_flags(struct amdgpu_bo *bo, u64 *tiling_flags);
int amdgpu_bo_set_metadata (struct amdgpu_bo *bo, void *metadata,
			    uint32_t metadata_size, uint64_t flags);
int amdgpu_bo_get_metadata(struct amdgpu_bo *bo, void *buffer,
			   size_t buffer_size, uint32_t *metadata_size,
			   uint64_t *flags);
void amdgpu_bo_move_notify(struct ttm_buffer_object *bo, bool evict);
void amdgpu_bo_release_notify(struct ttm_buffer_object *bo);
vm_fault_t amdgpu_bo_fault_reserve_notify(struct ttm_buffer_object *bo);
void amdgpu_bo_fence(struct amdgpu_bo *bo, struct dma_fence *fence,
		     bool shared);
int amdgpu_bo_sync_wait_resv(struct amdgpu_device *adev, struct dma_resv *resv,
			     enum amdgpu_sync_mode sync_mode, void *owner,
			     bool intr);
int amdgpu_bo_sync_wait(struct amdgpu_bo *bo, void *owner, bool intr);
u64 amdgpu_bo_gpu_offset(struct amdgpu_bo *bo);
u64 amdgpu_bo_gpu_offset_no_check(struct amdgpu_bo *bo);
void amdgpu_bo_get_memory(struct amdgpu_bo *bo,
			  struct amdgpu_mem_stats *stats);
void amdgpu_bo_add_to_shadow_list(struct amdgpu_bo_vm *vmbo);
int amdgpu_bo_restore_shadow(struct amdgpu_bo *shadow,
			     struct dma_fence **fence);
uint32_t amdgpu_bo_get_preferred_domain(struct amdgpu_device *adev,
					    uint32_t domain);

/*
 * sub allocation
 */
static inline struct amdgpu_sa_manager *
to_amdgpu_sa_manager(struct drm_suballoc_manager *manager)
{
	return container_of(manager, struct amdgpu_sa_manager, base);
}

static inline uint64_t amdgpu_sa_bo_gpu_addr(struct drm_suballoc *sa_bo)
{
	return to_amdgpu_sa_manager(sa_bo->manager)->gpu_addr +
		drm_suballoc_soffset(sa_bo);
}

static inline void *amdgpu_sa_bo_cpu_addr(struct drm_suballoc *sa_bo)
{
	return to_amdgpu_sa_manager(sa_bo->manager)->cpu_ptr +
		drm_suballoc_soffset(sa_bo);
}

int amdgpu_sa_bo_manager_init(struct amdgpu_device *adev,
				     struct amdgpu_sa_manager *sa_manager,
				     unsigned size, u32 align, u32 domain);
void amdgpu_sa_bo_manager_fini(struct amdgpu_device *adev,
				      struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_manager_start(struct amdgpu_device *adev,
				      struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_new(struct amdgpu_sa_manager *sa_manager,
		     struct drm_suballoc **sa_bo,
		     unsigned int size);
void amdgpu_sa_bo_free(struct amdgpu_device *adev,
		       struct drm_suballoc **sa_bo,
		       struct dma_fence *fence);
#if defined(CONFIG_DEBUG_FS)
void amdgpu_sa_bo_dump_debug_info(struct amdgpu_sa_manager *sa_manager,
					 struct seq_file *m);
u64 amdgpu_bo_print_info(int id, struct amdgpu_bo *bo, struct seq_file *m);
#endif
void amdgpu_debugfs_sa_init(struct amdgpu_device *adev);

bool amdgpu_bo_support_uswc(u64 bo_flags);


#endif
