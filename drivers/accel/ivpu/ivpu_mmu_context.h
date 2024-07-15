/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_MMU_CONTEXT_H__
#define __IVPU_MMU_CONTEXT_H__

#include <drm/drm_mm.h>

struct ivpu_device;
struct ivpu_file_priv;
struct ivpu_addr_range;

#define IVPU_MMU_PGTABLE_ENTRIES	512ull

struct ivpu_mmu_pgtable {
	u64		***pte_ptrs[IVPU_MMU_PGTABLE_ENTRIES];
	u64		**pmd_ptrs[IVPU_MMU_PGTABLE_ENTRIES];
	u64		*pud_ptrs[IVPU_MMU_PGTABLE_ENTRIES];
	u64		*pgd_dma_ptr;
	dma_addr_t	pgd_dma;
};

struct ivpu_mmu_context {
	struct mutex lock; /* Protects: mm, pgtable */
	struct drm_mm mm;
	struct ivpu_mmu_pgtable pgtable;
	u32 id;
};

int ivpu_mmu_global_context_init(struct ivpu_device *vdev);
void ivpu_mmu_global_context_fini(struct ivpu_device *vdev);
int ivpu_mmu_reserved_context_init(struct ivpu_device *vdev);
void ivpu_mmu_reserved_context_fini(struct ivpu_device *vdev);

int ivpu_mmu_user_context_init(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx, u32 ctx_id);
void ivpu_mmu_user_context_fini(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx);
void ivpu_mmu_user_context_mark_invalid(struct ivpu_device *vdev, u32 ssid);

int ivpu_mmu_context_insert_node(struct ivpu_mmu_context *ctx, const struct ivpu_addr_range *range,
				 u64 size, struct drm_mm_node *node);
void ivpu_mmu_context_remove_node(struct ivpu_mmu_context *ctx, struct drm_mm_node *node);

int ivpu_mmu_context_map_sgt(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			     u64 vpu_addr, struct sg_table *sgt, bool llc_coherent);
void ivpu_mmu_context_unmap_sgt(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
				u64 vpu_addr, struct sg_table *sgt);

#endif /* __IVPU_MMU_CONTEXT_H__ */
