/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_BO_H_
#define _XE_BO_H_

#include <drm/ttm/ttm_tt.h>

#include "xe_bo_types.h"
#include "xe_macros.h"
#include "xe_vm_types.h"
#include "xe_vm.h"

#define XE_DEFAULT_GTT_SIZE_MB          3072ULL /* 3GB by default */

#define XE_BO_FLAG_USER		BIT(0)
/* The bits below need to be contiguous, or things break */
#define XE_BO_FLAG_SYSTEM		BIT(1)
#define XE_BO_FLAG_VRAM0		BIT(2)
#define XE_BO_FLAG_VRAM1		BIT(3)
#define XE_BO_FLAG_VRAM_MASK		(XE_BO_FLAG_VRAM0 | XE_BO_FLAG_VRAM1)
/* -- */
#define XE_BO_FLAG_STOLEN		BIT(4)
#define XE_BO_FLAG_VRAM_IF_DGFX(tile)	(IS_DGFX(tile_to_xe(tile)) ? \
					 XE_BO_FLAG_VRAM0 << (tile)->id : \
					 XE_BO_FLAG_SYSTEM)
#define XE_BO_FLAG_GGTT			BIT(5)
#define XE_BO_FLAG_IGNORE_MIN_PAGE_SIZE BIT(6)
#define XE_BO_FLAG_PINNED		BIT(7)
#define XE_BO_FLAG_NO_RESV_EVICT	BIT(8)
#define XE_BO_FLAG_DEFER_BACKING	BIT(9)
#define XE_BO_FLAG_SCANOUT		BIT(10)
#define XE_BO_FLAG_FIXED_PLACEMENT	BIT(11)
#define XE_BO_FLAG_PAGETABLE		BIT(12)
#define XE_BO_FLAG_NEEDS_CPU_ACCESS	BIT(13)
#define XE_BO_FLAG_NEEDS_UC		BIT(14)
#define XE_BO_FLAG_NEEDS_64K		BIT(15)
#define XE_BO_FLAG_NEEDS_2M		BIT(16)
#define XE_BO_FLAG_GGTT_INVALIDATE	BIT(17)
/* this one is trigger internally only */
#define XE_BO_FLAG_INTERNAL_TEST	BIT(30)
#define XE_BO_FLAG_INTERNAL_64K		BIT(31)

#define XE_PTE_SHIFT			12
#define XE_PAGE_SIZE			(1 << XE_PTE_SHIFT)
#define XE_PTE_MASK			(XE_PAGE_SIZE - 1)
#define XE_PDE_SHIFT			(XE_PTE_SHIFT - 3)
#define XE_PDES				(1 << XE_PDE_SHIFT)
#define XE_PDE_MASK			(XE_PDES - 1)

#define XE_64K_PTE_SHIFT		16
#define XE_64K_PAGE_SIZE		(1 << XE_64K_PTE_SHIFT)
#define XE_64K_PTE_MASK			(XE_64K_PAGE_SIZE - 1)
#define XE_64K_PDE_MASK			(XE_PDE_MASK >> 4)

#define XE_PL_SYSTEM		TTM_PL_SYSTEM
#define XE_PL_TT		TTM_PL_TT
#define XE_PL_VRAM0		TTM_PL_VRAM
#define XE_PL_VRAM1		(XE_PL_VRAM0 + 1)
#define XE_PL_STOLEN		(TTM_NUM_MEM_TYPES - 1)

#define XE_BO_PROPS_INVALID	(-1)

struct sg_table;

struct xe_bo *xe_bo_alloc(void);
void xe_bo_free(struct xe_bo *bo);

struct xe_bo *___xe_bo_create_locked(struct xe_device *xe, struct xe_bo *bo,
				     struct xe_tile *tile, struct dma_resv *resv,
				     struct ttm_lru_bulk_move *bulk, size_t size,
				     u16 cpu_caching, enum ttm_bo_type type,
				     u32 flags);
struct xe_bo *
xe_bo_create_locked_range(struct xe_device *xe,
			  struct xe_tile *tile, struct xe_vm *vm,
			  size_t size, u64 start, u64 end,
			  enum ttm_bo_type type, u32 flags, u64 alignment);
struct xe_bo *xe_bo_create_locked(struct xe_device *xe, struct xe_tile *tile,
				  struct xe_vm *vm, size_t size,
				  enum ttm_bo_type type, u32 flags);
struct xe_bo *xe_bo_create(struct xe_device *xe, struct xe_tile *tile,
			   struct xe_vm *vm, size_t size,
			   enum ttm_bo_type type, u32 flags);
struct xe_bo *xe_bo_create_user(struct xe_device *xe, struct xe_tile *tile,
				struct xe_vm *vm, size_t size,
				u16 cpu_caching,
				u32 flags);
struct xe_bo *xe_bo_create_pin_map(struct xe_device *xe, struct xe_tile *tile,
				   struct xe_vm *vm, size_t size,
				   enum ttm_bo_type type, u32 flags);
struct xe_bo *xe_bo_create_pin_map_at(struct xe_device *xe, struct xe_tile *tile,
				      struct xe_vm *vm, size_t size, u64 offset,
				      enum ttm_bo_type type, u32 flags);
struct xe_bo *xe_bo_create_pin_map_at_aligned(struct xe_device *xe,
					      struct xe_tile *tile,
					      struct xe_vm *vm,
					      size_t size, u64 offset,
					      enum ttm_bo_type type, u32 flags,
					      u64 alignment);
struct xe_bo *xe_bo_create_from_data(struct xe_device *xe, struct xe_tile *tile,
				     const void *data, size_t size,
				     enum ttm_bo_type type, u32 flags);
struct xe_bo *xe_managed_bo_create_pin_map(struct xe_device *xe, struct xe_tile *tile,
					   size_t size, u32 flags);
struct xe_bo *xe_managed_bo_create_from_data(struct xe_device *xe, struct xe_tile *tile,
					     const void *data, size_t size, u32 flags);
int xe_managed_bo_reinit_in_vram(struct xe_device *xe, struct xe_tile *tile, struct xe_bo **src);

int xe_bo_placement_for_flags(struct xe_device *xe, struct xe_bo *bo,
			      u32 bo_flags);

static inline struct xe_bo *ttm_to_xe_bo(const struct ttm_buffer_object *bo)
{
	return container_of(bo, struct xe_bo, ttm);
}

static inline struct xe_bo *gem_to_xe_bo(const struct drm_gem_object *obj)
{
	return container_of(obj, struct xe_bo, ttm.base);
}

#define xe_bo_device(bo) ttm_to_xe_device((bo)->ttm.bdev)

static inline struct xe_bo *xe_bo_get(struct xe_bo *bo)
{
	if (bo)
		drm_gem_object_get(&bo->ttm.base);

	return bo;
}

void xe_bo_put(struct xe_bo *bo);

static inline void __xe_bo_unset_bulk_move(struct xe_bo *bo)
{
	if (bo)
		ttm_bo_set_bulk_move(&bo->ttm, NULL);
}

static inline void xe_bo_assert_held(struct xe_bo *bo)
{
	if (bo)
		dma_resv_assert_held((bo)->ttm.base.resv);
}

int xe_bo_lock(struct xe_bo *bo, bool intr);

void xe_bo_unlock(struct xe_bo *bo);

static inline void xe_bo_unlock_vm_held(struct xe_bo *bo)
{
	if (bo) {
		XE_WARN_ON(bo->vm && bo->ttm.base.resv != xe_vm_resv(bo->vm));
		if (bo->vm)
			xe_vm_assert_held(bo->vm);
		else
			dma_resv_unlock(bo->ttm.base.resv);
	}
}

int xe_bo_pin_external(struct xe_bo *bo);
int xe_bo_pin(struct xe_bo *bo);
void xe_bo_unpin_external(struct xe_bo *bo);
void xe_bo_unpin(struct xe_bo *bo);
int xe_bo_validate(struct xe_bo *bo, struct xe_vm *vm, bool allow_res_evict);

static inline bool xe_bo_is_pinned(struct xe_bo *bo)
{
	return bo->ttm.pin_count;
}

static inline void xe_bo_unpin_map_no_vm(struct xe_bo *bo)
{
	if (likely(bo)) {
		xe_bo_lock(bo, false);
		xe_bo_unpin(bo);
		xe_bo_unlock(bo);

		xe_bo_put(bo);
	}
}

bool xe_bo_is_xe_bo(struct ttm_buffer_object *bo);
dma_addr_t __xe_bo_addr(struct xe_bo *bo, u64 offset, size_t page_size);
dma_addr_t xe_bo_addr(struct xe_bo *bo, u64 offset, size_t page_size);

static inline dma_addr_t
xe_bo_main_addr(struct xe_bo *bo, size_t page_size)
{
	return xe_bo_addr(bo, 0, page_size);
}

static inline u32
xe_bo_ggtt_addr(struct xe_bo *bo)
{
	if (XE_WARN_ON(!bo->ggtt_node))
		return 0;

	XE_WARN_ON(bo->ggtt_node->base.size > bo->size);
	XE_WARN_ON(bo->ggtt_node->base.start + bo->ggtt_node->base.size > (1ull << 32));
	return bo->ggtt_node->base.start;
}

int xe_bo_vmap(struct xe_bo *bo);
void xe_bo_vunmap(struct xe_bo *bo);

bool mem_type_is_vram(u32 mem_type);
bool xe_bo_is_vram(struct xe_bo *bo);
bool xe_bo_is_stolen(struct xe_bo *bo);
bool xe_bo_is_stolen_devmem(struct xe_bo *bo);
bool xe_bo_has_single_placement(struct xe_bo *bo);
uint64_t vram_region_gpu_offset(struct ttm_resource *res);

bool xe_bo_can_migrate(struct xe_bo *bo, u32 mem_type);

int xe_bo_migrate(struct xe_bo *bo, u32 mem_type);
int xe_bo_evict(struct xe_bo *bo, bool force_alloc);

int xe_bo_evict_pinned(struct xe_bo *bo);
int xe_bo_restore_pinned(struct xe_bo *bo);

extern const struct ttm_device_funcs xe_ttm_funcs;
extern const char *const xe_mem_type_to_name[];

int xe_gem_create_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);
int xe_gem_mmap_offset_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file);
void xe_bo_runtime_pm_release_mmap_offset(struct xe_bo *bo);

int xe_bo_dumb_create(struct drm_file *file_priv,
		      struct drm_device *dev,
		      struct drm_mode_create_dumb *args);

bool xe_bo_needs_ccs_pages(struct xe_bo *bo);

static inline size_t xe_bo_ccs_pages_start(struct xe_bo *bo)
{
	return PAGE_ALIGN(bo->ttm.base.size);
}

static inline bool xe_bo_has_pages(struct xe_bo *bo)
{
	if ((bo->ttm.ttm && ttm_tt_is_populated(bo->ttm.ttm)) ||
	    xe_bo_is_vram(bo))
		return true;

	return false;
}

void __xe_bo_release_dummy(struct kref *kref);

/**
 * xe_bo_put_deferred() - Put a buffer object with delayed final freeing
 * @bo: The bo to put.
 * @deferred: List to which to add the buffer object if we cannot put, or
 * NULL if the function is to put unconditionally.
 *
 * Since the final freeing of an object includes both sleeping and (!)
 * memory allocation in the dma_resv individualization, it's not ok
 * to put an object from atomic context nor from within a held lock
 * tainted by reclaim. In such situations we want to defer the final
 * freeing until we've exited the restricting context, or in the worst
 * case to a workqueue.
 * This function either puts the object if possible without the refcount
 * reaching zero, or adds it to the @deferred list if that was not possible.
 * The caller needs to follow up with a call to xe_bo_put_commit() to actually
 * put the bo iff this function returns true. It's safe to always
 * follow up with a call to xe_bo_put_commit().
 * TODO: It's TTM that is the villain here. Perhaps TTM should add an
 * interface like this.
 *
 * Return: true if @bo was the first object put on the @freed list,
 * false otherwise.
 */
static inline bool
xe_bo_put_deferred(struct xe_bo *bo, struct llist_head *deferred)
{
	if (!deferred) {
		xe_bo_put(bo);
		return false;
	}

	if (!kref_put(&bo->ttm.base.refcount, __xe_bo_release_dummy))
		return false;

	return llist_add(&bo->freed, deferred);
}

void xe_bo_put_commit(struct llist_head *deferred);

struct sg_table *xe_bo_sg(struct xe_bo *bo);

/*
 * xe_sg_segment_size() - Provides upper limit for sg segment size.
 * @dev: device pointer
 *
 * Returns the maximum segment size for the 'struct scatterlist'
 * elements.
 */
static inline unsigned int xe_sg_segment_size(struct device *dev)
{
	struct scatterlist __maybe_unused sg;
	size_t max = BIT_ULL(sizeof(sg.length) * 8) - 1;

	max = min_t(size_t, max, dma_max_mapping_size(dev));

	/*
	 * The iommu_dma_map_sg() function ensures iova allocation doesn't
	 * cross dma segment boundary. It does so by padding some sg elements.
	 * This can cause overflow, ending up with sg->length being set to 0.
	 * Avoid this by ensuring maximum segment size is half of 'max'
	 * rounded down to PAGE_SIZE.
	 */
	return round_down(max / 2, PAGE_SIZE);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
/**
 * xe_bo_is_mem_type - Whether the bo currently resides in the given
 * TTM memory type
 * @bo: The bo to check.
 * @mem_type: The TTM memory type.
 *
 * Return: true iff the bo resides in @mem_type, false otherwise.
 */
static inline bool xe_bo_is_mem_type(struct xe_bo *bo, u32 mem_type)
{
	xe_bo_assert_held(bo);
	return bo->ttm.resource->mem_type == mem_type;
}
#endif
#endif
