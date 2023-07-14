// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/highmem.h>

#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_mmu.h"
#include "ivpu_mmu_context.h"

#define IVPU_MMU_PGD_INDEX_MASK          GENMASK(38, 30)
#define IVPU_MMU_PMD_INDEX_MASK          GENMASK(29, 21)
#define IVPU_MMU_PTE_INDEX_MASK          GENMASK(20, 12)
#define IVPU_MMU_ENTRY_FLAGS_MASK        GENMASK(11, 0)
#define IVPU_MMU_ENTRY_FLAG_NG           BIT(11)
#define IVPU_MMU_ENTRY_FLAG_AF           BIT(10)
#define IVPU_MMU_ENTRY_FLAG_USER         BIT(6)
#define IVPU_MMU_ENTRY_FLAG_LLC_COHERENT BIT(2)
#define IVPU_MMU_ENTRY_FLAG_TYPE_PAGE    BIT(1)
#define IVPU_MMU_ENTRY_FLAG_VALID        BIT(0)

#define IVPU_MMU_PAGE_SIZE    SZ_4K
#define IVPU_MMU_PTE_MAP_SIZE (IVPU_MMU_PGTABLE_ENTRIES * IVPU_MMU_PAGE_SIZE)
#define IVPU_MMU_PMD_MAP_SIZE (IVPU_MMU_PGTABLE_ENTRIES * IVPU_MMU_PTE_MAP_SIZE)
#define IVPU_MMU_PGTABLE_SIZE (IVPU_MMU_PGTABLE_ENTRIES * sizeof(u64))

#define IVPU_MMU_DUMMY_ADDRESS 0xdeadb000
#define IVPU_MMU_ENTRY_VALID   (IVPU_MMU_ENTRY_FLAG_TYPE_PAGE | IVPU_MMU_ENTRY_FLAG_VALID)
#define IVPU_MMU_ENTRY_INVALID (IVPU_MMU_DUMMY_ADDRESS & ~IVPU_MMU_ENTRY_FLAGS_MASK)
#define IVPU_MMU_ENTRY_MAPPED  (IVPU_MMU_ENTRY_FLAG_AF | IVPU_MMU_ENTRY_FLAG_USER | \
				IVPU_MMU_ENTRY_FLAG_NG | IVPU_MMU_ENTRY_VALID)

static int ivpu_mmu_pgtable_init(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable)
{
	dma_addr_t pgd_dma;
	u64 *pgd;

	pgd = dma_alloc_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE, &pgd_dma, GFP_KERNEL);
	if (!pgd)
		return -ENOMEM;

	pgtable->pgd = pgd;
	pgtable->pgd_dma = pgd_dma;

	return 0;
}

static void ivpu_mmu_pgtable_free(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable)
{
	int pgd_index, pmd_index;

	for (pgd_index = 0; pgd_index < IVPU_MMU_PGTABLE_ENTRIES; ++pgd_index) {
		u64 **pmd_entries = pgtable->pgd_cpu_entries[pgd_index];
		u64 *pmd = pgtable->pgd_entries[pgd_index];

		if (!pmd_entries)
			continue;

		for (pmd_index = 0; pmd_index < IVPU_MMU_PGTABLE_ENTRIES; ++pmd_index) {
			if (pmd_entries[pmd_index])
				dma_free_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE,
					    pmd_entries[pmd_index],
					    pmd[pmd_index] & ~IVPU_MMU_ENTRY_FLAGS_MASK);
		}

		kfree(pmd_entries);
		dma_free_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE, pgtable->pgd_entries[pgd_index],
			    pgtable->pgd[pgd_index] & ~IVPU_MMU_ENTRY_FLAGS_MASK);
	}

	dma_free_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE, pgtable->pgd,
		    pgtable->pgd_dma & ~IVPU_MMU_ENTRY_FLAGS_MASK);
}

static u64*
ivpu_mmu_ensure_pmd(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable, u64 pgd_index)
{
	u64 **pmd_entries;
	dma_addr_t pmd_dma;
	u64 *pmd;

	if (pgtable->pgd_entries[pgd_index])
		return pgtable->pgd_entries[pgd_index];

	pmd = dma_alloc_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE, &pmd_dma, GFP_KERNEL);
	if (!pmd)
		return NULL;

	pmd_entries = kzalloc(IVPU_MMU_PGTABLE_SIZE, GFP_KERNEL);
	if (!pmd_entries)
		goto err_free_pgd;

	pgtable->pgd_entries[pgd_index] = pmd;
	pgtable->pgd_cpu_entries[pgd_index] = pmd_entries;
	pgtable->pgd[pgd_index] = pmd_dma | IVPU_MMU_ENTRY_VALID;

	return pmd;

err_free_pgd:
	dma_free_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE, pmd, pmd_dma);
	return NULL;
}

static u64*
ivpu_mmu_ensure_pte(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable,
		    int pgd_index, int pmd_index)
{
	dma_addr_t pte_dma;
	u64 *pte;

	if (pgtable->pgd_cpu_entries[pgd_index][pmd_index])
		return pgtable->pgd_cpu_entries[pgd_index][pmd_index];

	pte = dma_alloc_wc(vdev->drm.dev, IVPU_MMU_PGTABLE_SIZE, &pte_dma, GFP_KERNEL);
	if (!pte)
		return NULL;

	pgtable->pgd_cpu_entries[pgd_index][pmd_index] = pte;
	pgtable->pgd_entries[pgd_index][pmd_index] = pte_dma | IVPU_MMU_ENTRY_VALID;

	return pte;
}

static int
ivpu_mmu_context_map_page(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			  u64 vpu_addr, dma_addr_t dma_addr, int prot)
{
	u64 *pte;
	int pgd_index = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
	int pmd_index = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
	int pte_index = FIELD_GET(IVPU_MMU_PTE_INDEX_MASK, vpu_addr);

	/* Allocate PMD - second level page table if needed */
	if (!ivpu_mmu_ensure_pmd(vdev, &ctx->pgtable, pgd_index))
		return -ENOMEM;

	/* Allocate PTE - third level page table if needed */
	pte = ivpu_mmu_ensure_pte(vdev, &ctx->pgtable, pgd_index, pmd_index);
	if (!pte)
		return -ENOMEM;

	/* Update PTE - third level page table with DMA address */
	pte[pte_index] = dma_addr | prot;

	return 0;
}

static void ivpu_mmu_context_unmap_page(struct ivpu_mmu_context *ctx, u64 vpu_addr)
{
	int pgd_index = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
	int pmd_index = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
	int pte_index = FIELD_GET(IVPU_MMU_PTE_INDEX_MASK, vpu_addr);

	/* Update PTE with dummy physical address and clear flags */
	ctx->pgtable.pgd_cpu_entries[pgd_index][pmd_index][pte_index] = IVPU_MMU_ENTRY_INVALID;
}

static void
ivpu_mmu_context_flush_page_tables(struct ivpu_mmu_context *ctx, u64 vpu_addr, size_t size)
{
	u64 end_addr = vpu_addr + size;
	u64 *pgd = ctx->pgtable.pgd;

	/* Align to PMD entry (2 MB) */
	vpu_addr &= ~(IVPU_MMU_PTE_MAP_SIZE - 1);

	while (vpu_addr < end_addr) {
		int pgd_index = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
		u64 pmd_end = (pgd_index + 1) * (u64)IVPU_MMU_PMD_MAP_SIZE;
		u64 *pmd = ctx->pgtable.pgd_entries[pgd_index];

		while (vpu_addr < end_addr && vpu_addr < pmd_end) {
			int pmd_index = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
			u64 *pte = ctx->pgtable.pgd_cpu_entries[pgd_index][pmd_index];

			clflush_cache_range(pte, IVPU_MMU_PGTABLE_SIZE);
			vpu_addr += IVPU_MMU_PTE_MAP_SIZE;
		}
		clflush_cache_range(pmd, IVPU_MMU_PGTABLE_SIZE);
	}
	clflush_cache_range(pgd, IVPU_MMU_PGTABLE_SIZE);
}

static int
ivpu_mmu_context_map_pages(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			   u64 vpu_addr, dma_addr_t dma_addr, size_t size, int prot)
{
	while (size) {
		int ret = ivpu_mmu_context_map_page(vdev, ctx, vpu_addr, dma_addr, prot);

		if (ret)
			return ret;

		vpu_addr += IVPU_MMU_PAGE_SIZE;
		dma_addr += IVPU_MMU_PAGE_SIZE;
		size -= IVPU_MMU_PAGE_SIZE;
	}

	return 0;
}

static void ivpu_mmu_context_unmap_pages(struct ivpu_mmu_context *ctx, u64 vpu_addr, size_t size)
{
	while (size) {
		ivpu_mmu_context_unmap_page(ctx, vpu_addr);
		vpu_addr += IVPU_MMU_PAGE_SIZE;
		size -= IVPU_MMU_PAGE_SIZE;
	}
}

int
ivpu_mmu_context_map_sgt(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			 u64 vpu_addr, struct sg_table *sgt,  bool llc_coherent)
{
	struct scatterlist *sg;
	int prot;
	int ret;
	u64 i;

	if (!IS_ALIGNED(vpu_addr, IVPU_MMU_PAGE_SIZE))
		return -EINVAL;
	/*
	 * VPU is only 32 bit, but DMA engine is 38 bit
	 * Ranges < 2 GB are reserved for VPU internal registers
	 * Limit range to 8 GB
	 */
	if (vpu_addr < SZ_2G || vpu_addr > SZ_8G)
		return -EINVAL;

	prot = IVPU_MMU_ENTRY_MAPPED;
	if (llc_coherent)
		prot |= IVPU_MMU_ENTRY_FLAG_LLC_COHERENT;

	mutex_lock(&ctx->lock);

	for_each_sgtable_dma_sg(sgt, sg, i) {
		u64 dma_addr = sg_dma_address(sg) - sg->offset;
		size_t size = sg_dma_len(sg) + sg->offset;

		ret = ivpu_mmu_context_map_pages(vdev, ctx, vpu_addr, dma_addr, size, prot);
		if (ret) {
			ivpu_err(vdev, "Failed to map context pages\n");
			mutex_unlock(&ctx->lock);
			return ret;
		}
		ivpu_mmu_context_flush_page_tables(ctx, vpu_addr, size);
		vpu_addr += size;
	}

	mutex_unlock(&ctx->lock);

	ret = ivpu_mmu_invalidate_tlb(vdev, ctx->id);
	if (ret)
		ivpu_err(vdev, "Failed to invalidate TLB for ctx %u: %d\n", ctx->id, ret);
	return ret;
}

void
ivpu_mmu_context_unmap_sgt(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			   u64 vpu_addr, struct sg_table *sgt)
{
	struct scatterlist *sg;
	int ret;
	u64 i;

	if (!IS_ALIGNED(vpu_addr, IVPU_MMU_PAGE_SIZE))
		ivpu_warn(vdev, "Unaligned vpu_addr: 0x%llx\n", vpu_addr);

	mutex_lock(&ctx->lock);

	for_each_sgtable_dma_sg(sgt, sg, i) {
		size_t size = sg_dma_len(sg) + sg->offset;

		ivpu_mmu_context_unmap_pages(ctx, vpu_addr, size);
		ivpu_mmu_context_flush_page_tables(ctx, vpu_addr, size);
		vpu_addr += size;
	}

	mutex_unlock(&ctx->lock);

	ret = ivpu_mmu_invalidate_tlb(vdev, ctx->id);
	if (ret)
		ivpu_warn(vdev, "Failed to invalidate TLB for ctx %u: %d\n", ctx->id, ret);
}

int
ivpu_mmu_context_insert_node_locked(struct ivpu_mmu_context *ctx,
				    const struct ivpu_addr_range *range,
				    u64 size, struct drm_mm_node *node)
{
	lockdep_assert_held(&ctx->lock);

	return drm_mm_insert_node_in_range(&ctx->mm, node, size, IVPU_MMU_PAGE_SIZE,
					  0, range->start, range->end, DRM_MM_INSERT_BEST);
}

void
ivpu_mmu_context_remove_node_locked(struct ivpu_mmu_context *ctx, struct drm_mm_node *node)
{
	lockdep_assert_held(&ctx->lock);

	drm_mm_remove_node(node);
}

static int
ivpu_mmu_context_init(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx, u32 context_id)
{
	u64 start, end;
	int ret;

	mutex_init(&ctx->lock);
	INIT_LIST_HEAD(&ctx->bo_list);

	ret = ivpu_mmu_pgtable_init(vdev, &ctx->pgtable);
	if (ret)
		return ret;

	if (!context_id) {
		start = vdev->hw->ranges.global_low.start;
		end = vdev->hw->ranges.global_high.end;
	} else {
		start = vdev->hw->ranges.user_low.start;
		end = vdev->hw->ranges.user_high.end;
	}

	drm_mm_init(&ctx->mm, start, end - start);
	ctx->id = context_id;

	return 0;
}

static void ivpu_mmu_context_fini(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx)
{
	drm_WARN_ON(&vdev->drm, !ctx->pgtable.pgd);

	mutex_destroy(&ctx->lock);
	ivpu_mmu_pgtable_free(vdev, &ctx->pgtable);
	drm_mm_takedown(&ctx->mm);
}

int ivpu_mmu_global_context_init(struct ivpu_device *vdev)
{
	return ivpu_mmu_context_init(vdev, &vdev->gctx, IVPU_GLOBAL_CONTEXT_MMU_SSID);
}

void ivpu_mmu_global_context_fini(struct ivpu_device *vdev)
{
	return ivpu_mmu_context_fini(vdev, &vdev->gctx);
}

void ivpu_mmu_user_context_mark_invalid(struct ivpu_device *vdev, u32 ssid)
{
	struct ivpu_file_priv *file_priv;

	xa_lock(&vdev->context_xa);

	file_priv = xa_load(&vdev->context_xa, ssid);
	if (file_priv)
		file_priv->has_mmu_faults = true;

	xa_unlock(&vdev->context_xa);
}

int ivpu_mmu_user_context_init(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx, u32 ctx_id)
{
	int ret;

	drm_WARN_ON(&vdev->drm, !ctx_id);

	ret = ivpu_mmu_context_init(vdev, ctx, ctx_id);
	if (ret) {
		ivpu_err(vdev, "Failed to initialize context: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_set_pgtable(vdev, ctx_id, &ctx->pgtable);
	if (ret) {
		ivpu_err(vdev, "Failed to set page table: %d\n", ret);
		goto err_context_fini;
	}

	return 0;

err_context_fini:
	ivpu_mmu_context_fini(vdev, ctx);
	return ret;
}

void ivpu_mmu_user_context_fini(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx)
{
	drm_WARN_ON(&vdev->drm, !ctx->id);

	ivpu_mmu_clear_pgtable(vdev, ctx->id);
	ivpu_mmu_context_fini(vdev, ctx);
}
