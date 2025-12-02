/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_SVM_H_
#define _XE_SVM_H_

struct xe_device;

/**
 * xe_svm_devm_owner() - Return the owner of device private memory
 * @xe: The xe device.
 *
 * Return: The owner of this device's device private memory to use in
 * hmm_range_fault()-
 */
static inline void *xe_svm_devm_owner(struct xe_device *xe)
{
	return xe;
}

#if IS_ENABLED(CONFIG_DRM_XE_GPUSVM)

#include <drm/drm_pagemap.h>
#include <drm/drm_gpusvm.h>

#define XE_INTERCONNECT_VRAM DRM_INTERCONNECT_DRIVER

struct xe_bo;
struct xe_gt;
struct xe_tile;
struct xe_vm;
struct xe_vma;
struct xe_vram_region;

/** struct xe_svm_range - SVM range */
struct xe_svm_range {
	/** @base: base drm_gpusvm_range */
	struct drm_gpusvm_range base;
	/**
	 * @garbage_collector_link: Link into VM's garbage collect SVM range
	 * list. Protected by VM's garbage collect lock.
	 */
	struct list_head garbage_collector_link;
	/**
	 * @tile_present: Tile mask of binding is present for this range.
	 * Protected by GPU SVM notifier lock.
	 */
	u8 tile_present;
	/**
	 * @tile_invalidated: Tile mask of binding is invalidated for this
	 * range. Protected by GPU SVM notifier lock.
	 */
	u8 tile_invalidated;
};

/**
 * xe_svm_range_pages_valid() - SVM range pages valid
 * @range: SVM range
 *
 * Return: True if SVM range pages are valid, False otherwise
 */
static inline bool xe_svm_range_pages_valid(struct xe_svm_range *range)
{
	return drm_gpusvm_range_pages_valid(range->base.gpusvm, &range->base);
}

int xe_devm_add(struct xe_tile *tile, struct xe_vram_region *vr);

int xe_svm_init(struct xe_vm *vm);

void xe_svm_fini(struct xe_vm *vm);

void xe_svm_close(struct xe_vm *vm);

int xe_svm_handle_pagefault(struct xe_vm *vm, struct xe_vma *vma,
			    struct xe_gt *gt, u64 fault_addr,
			    bool atomic);

bool xe_svm_has_mapping(struct xe_vm *vm, u64 start, u64 end);

int xe_svm_bo_evict(struct xe_bo *bo);

void xe_svm_range_debug(struct xe_svm_range *range, const char *operation);

int xe_svm_alloc_vram(struct xe_tile *tile, struct xe_svm_range *range,
		      const struct drm_gpusvm_ctx *ctx);

struct xe_svm_range *xe_svm_range_find_or_insert(struct xe_vm *vm, u64 addr,
						 struct xe_vma *vma, struct drm_gpusvm_ctx *ctx);

int xe_svm_range_get_pages(struct xe_vm *vm, struct xe_svm_range *range,
			   struct drm_gpusvm_ctx *ctx);

bool xe_svm_range_needs_migrate_to_vram(struct xe_svm_range *range, struct xe_vma *vma,
					bool preferred_region_is_vram);

void xe_svm_range_migrate_to_smem(struct xe_vm *vm, struct xe_svm_range *range);

bool xe_svm_range_validate(struct xe_vm *vm,
			   struct xe_svm_range *range,
			   u8 tile_mask, bool devmem_preferred);

u64 xe_svm_find_vma_start(struct xe_vm *vm, u64 addr, u64 end,  struct xe_vma *vma);

void xe_svm_unmap_address_range(struct xe_vm *vm, u64 start, u64 end);

u8 xe_svm_ranges_zap_ptes_in_range(struct xe_vm *vm, u64 start, u64 end);

struct drm_pagemap *xe_vma_resolve_pagemap(struct xe_vma *vma, struct xe_tile *tile);

/**
 * xe_svm_range_has_dma_mapping() - SVM range has DMA mapping
 * @range: SVM range
 *
 * Return: True if SVM range has a DMA mapping, False otherwise
 */
static inline bool xe_svm_range_has_dma_mapping(struct xe_svm_range *range)
{
	lockdep_assert_held(&range->base.gpusvm->notifier_lock);
	return range->base.pages.flags.has_dma_mapping;
}

/**
 * to_xe_range - Convert a drm_gpusvm_range pointer to a xe_svm_range
 * @r: Pointer to the drm_gpusvm_range structure
 *
 * This function takes a pointer to a drm_gpusvm_range structure and
 * converts it to a pointer to the containing xe_svm_range structure.
 *
 * Return: Pointer to the xe_svm_range structure
 */
static inline struct xe_svm_range *to_xe_range(struct drm_gpusvm_range *r)
{
	return container_of(r, struct xe_svm_range, base);
}

/**
 * xe_svm_range_start() - SVM range start address
 * @range: SVM range
 *
 * Return: start address of range.
 */
static inline unsigned long xe_svm_range_start(struct xe_svm_range *range)
{
	return drm_gpusvm_range_start(&range->base);
}

/**
 * xe_svm_range_end() - SVM range end address
 * @range: SVM range
 *
 * Return: end address of range.
 */
static inline unsigned long xe_svm_range_end(struct xe_svm_range *range)
{
	return drm_gpusvm_range_end(&range->base);
}

/**
 * xe_svm_range_size() - SVM range size
 * @range: SVM range
 *
 * Return: Size of range.
 */
static inline unsigned long xe_svm_range_size(struct xe_svm_range *range)
{
	return drm_gpusvm_range_size(&range->base);
}

void xe_svm_flush(struct xe_vm *vm);

#else
#include <linux/interval_tree.h>
#include "xe_vm.h"

struct drm_pagemap_addr;
struct drm_gpusvm_ctx;
struct drm_gpusvm_range;
struct xe_bo;
struct xe_gt;
struct xe_vm;
struct xe_vma;
struct xe_tile;
struct xe_vram_region;

#define XE_INTERCONNECT_VRAM 1

struct xe_svm_range {
	struct {
		struct interval_tree_node itree;
		struct {
			const struct drm_pagemap_addr *dma_addr;
		} pages;
	} base;
	u32 tile_present;
	u32 tile_invalidated;
};

static inline bool xe_svm_range_pages_valid(struct xe_svm_range *range)
{
	return false;
}

static inline
int xe_devm_add(struct xe_tile *tile, struct xe_vram_region *vr)
{
	return 0;
}

static inline
int xe_svm_init(struct xe_vm *vm)
{
#if IS_ENABLED(CONFIG_DRM_GPUSVM)
	return drm_gpusvm_init(&vm->svm.gpusvm, "Xe SVM (simple)", &vm->xe->drm,
			       NULL, NULL, 0, 0, 0, NULL, NULL, 0);
#else
	return 0;
#endif
}

static inline
void xe_svm_fini(struct xe_vm *vm)
{
#if IS_ENABLED(CONFIG_DRM_GPUSVM)
	xe_assert(vm->xe, xe_vm_is_closed(vm));
	drm_gpusvm_fini(&vm->svm.gpusvm);
#endif
}

static inline
void xe_svm_close(struct xe_vm *vm)
{
}

static inline
int xe_svm_handle_pagefault(struct xe_vm *vm, struct xe_vma *vma,
			    struct xe_gt *gt, u64 fault_addr,
			    bool atomic)
{
	return 0;
}

static inline
bool xe_svm_has_mapping(struct xe_vm *vm, u64 start, u64 end)
{
	return false;
}

static inline
int xe_svm_bo_evict(struct xe_bo *bo)
{
	return 0;
}

static inline
void xe_svm_range_debug(struct xe_svm_range *range, const char *operation)
{
}

static inline int
xe_svm_alloc_vram(struct xe_tile *tile, struct xe_svm_range *range,
		  const struct drm_gpusvm_ctx *ctx)
{
	return -EOPNOTSUPP;
}

static inline
struct xe_svm_range *xe_svm_range_find_or_insert(struct xe_vm *vm, u64 addr,
						 struct xe_vma *vma, struct drm_gpusvm_ctx *ctx)
{
	return ERR_PTR(-EINVAL);
}

static inline
int xe_svm_range_get_pages(struct xe_vm *vm, struct xe_svm_range *range,
			   struct drm_gpusvm_ctx *ctx)
{
	return -EINVAL;
}

static inline struct xe_svm_range *to_xe_range(struct drm_gpusvm_range *r)
{
	return NULL;
}

static inline unsigned long xe_svm_range_start(struct xe_svm_range *range)
{
	return 0;
}

static inline unsigned long xe_svm_range_end(struct xe_svm_range *range)
{
	return 0;
}

static inline unsigned long xe_svm_range_size(struct xe_svm_range *range)
{
	return 0;
}

static inline
bool xe_svm_range_needs_migrate_to_vram(struct xe_svm_range *range, struct xe_vma *vma,
					u32 region)
{
	return false;
}

static inline
void xe_svm_range_migrate_to_smem(struct xe_vm *vm, struct xe_svm_range *range)
{
}

static inline
bool xe_svm_range_validate(struct xe_vm *vm,
			   struct xe_svm_range *range,
			   u8 tile_mask, bool devmem_preferred)
{
	return false;
}

static inline
u64 xe_svm_find_vma_start(struct xe_vm *vm, u64 addr, u64 end, struct xe_vma *vma)
{
	return ULONG_MAX;
}

static inline
void xe_svm_unmap_address_range(struct xe_vm *vm, u64 start, u64 end)
{
}

static inline
u8 xe_svm_ranges_zap_ptes_in_range(struct xe_vm *vm, u64 start, u64 end)
{
	return 0;
}

static inline
struct drm_pagemap *xe_vma_resolve_pagemap(struct xe_vma *vma, struct xe_tile *tile)
{
	return NULL;
}

static inline void xe_svm_flush(struct xe_vm *vm)
{
}
#define xe_svm_range_has_dma_mapping(...) false
#endif /* CONFIG_DRM_XE_GPUSVM */

#if IS_ENABLED(CONFIG_DRM_GPUSVM) /* Need to support userptr without XE_GPUSVM */
#define xe_svm_assert_in_notifier(vm__) \
	lockdep_assert_held_write(&(vm__)->svm.gpusvm.notifier_lock)

#define xe_svm_assert_held_read(vm__) \
	lockdep_assert_held_read(&(vm__)->svm.gpusvm.notifier_lock)

#define xe_svm_notifier_lock(vm__)	\
	drm_gpusvm_notifier_lock(&(vm__)->svm.gpusvm)

#define xe_svm_notifier_lock_interruptible(vm__)	\
	down_read_interruptible(&(vm__)->svm.gpusvm.notifier_lock)

#define xe_svm_notifier_unlock(vm__)	\
	drm_gpusvm_notifier_unlock(&(vm__)->svm.gpusvm)

#else
#define xe_svm_assert_in_notifier(...) do {} while (0)

static inline void xe_svm_assert_held_read(struct xe_vm *vm)
{
}

static inline void xe_svm_notifier_lock(struct xe_vm *vm)
{
}

static inline int xe_svm_notifier_lock_interruptible(struct xe_vm *vm)
{
	return 0;
}

static inline void xe_svm_notifier_unlock(struct xe_vm *vm)
{
}
#endif /* CONFIG_DRM_GPUSVM */

#endif
