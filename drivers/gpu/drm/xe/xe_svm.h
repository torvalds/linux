/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_SVM_H_
#define _XE_SVM_H_

#include <drm/drm_pagemap.h>
#include <drm/drm_gpusvm.h>

#define XE_INTERCONNECT_VRAM DRM_INTERCONNECT_DRIVER

struct xe_bo;
struct xe_vram_region;
struct xe_tile;
struct xe_vm;
struct xe_vma;

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
	/**
	 * @skip_migrate: Skip migration to VRAM, protected by GPU fault handler
	 * locking.
	 */
	u8 skip_migrate	:1;
};

#if IS_ENABLED(CONFIG_DRM_GPUSVM)
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
			    struct xe_tile *tile, u64 fault_addr,
			    bool atomic);

bool xe_svm_has_mapping(struct xe_vm *vm, u64 start, u64 end);

int xe_svm_bo_evict(struct xe_bo *bo);

void xe_svm_range_debug(struct xe_svm_range *range, const char *operation);
#else
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
	return 0;
}

static inline
void xe_svm_fini(struct xe_vm *vm)
{
}

static inline
void xe_svm_close(struct xe_vm *vm)
{
}

static inline
int xe_svm_handle_pagefault(struct xe_vm *vm, struct xe_vma *vma,
			    struct xe_tile *tile, u64 fault_addr,
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
#endif

/**
 * xe_svm_range_has_dma_mapping() - SVM range has DMA mapping
 * @range: SVM range
 *
 * Return: True if SVM range has a DMA mapping, False otherwise
 */
static inline bool xe_svm_range_has_dma_mapping(struct xe_svm_range *range)
{
	lockdep_assert_held(&range->base.gpusvm->notifier_lock);
	return range->base.flags.has_dma_mapping;
}

#define xe_svm_assert_in_notifier(vm__) \
	lockdep_assert_held_write(&(vm__)->svm.gpusvm.notifier_lock)

#define xe_svm_notifier_lock(vm__)	\
	drm_gpusvm_notifier_lock(&(vm__)->svm.gpusvm)

#define xe_svm_notifier_unlock(vm__)	\
	drm_gpusvm_notifier_unlock(&(vm__)->svm.gpusvm)

#endif
