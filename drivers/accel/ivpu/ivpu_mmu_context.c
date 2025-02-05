// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/highmem.h>
#include <linux/set_memory.h>
#include <linux/vmalloc.h>

#include <drm/drm_cache.h>

#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_mmu.h"
#include "ivpu_mmu_context.h"

#define IVPU_MMU_VPU_ADDRESS_MASK        GENMASK(47, 12)
#define IVPU_MMU_PGD_INDEX_MASK          GENMASK(47, 39)
#define IVPU_MMU_PUD_INDEX_MASK          GENMASK(38, 30)
#define IVPU_MMU_PMD_INDEX_MASK          GENMASK(29, 21)
#define IVPU_MMU_PTE_INDEX_MASK          GENMASK(20, 12)
#define IVPU_MMU_ENTRY_FLAGS_MASK        (BIT(52) | GENMASK(11, 0))
#define IVPU_MMU_ENTRY_FLAG_CONT         BIT(52)
#define IVPU_MMU_ENTRY_FLAG_NG           BIT(11)
#define IVPU_MMU_ENTRY_FLAG_AF           BIT(10)
#define IVPU_MMU_ENTRY_FLAG_RO           BIT(7)
#define IVPU_MMU_ENTRY_FLAG_USER         BIT(6)
#define IVPU_MMU_ENTRY_FLAG_LLC_COHERENT BIT(2)
#define IVPU_MMU_ENTRY_FLAG_TYPE_PAGE    BIT(1)
#define IVPU_MMU_ENTRY_FLAG_VALID        BIT(0)

#define IVPU_MMU_PAGE_SIZE       SZ_4K
#define IVPU_MMU_CONT_PAGES_SIZE (IVPU_MMU_PAGE_SIZE * 16)
#define IVPU_MMU_PTE_MAP_SIZE    (IVPU_MMU_PGTABLE_ENTRIES * IVPU_MMU_PAGE_SIZE)
#define IVPU_MMU_PMD_MAP_SIZE    (IVPU_MMU_PGTABLE_ENTRIES * IVPU_MMU_PTE_MAP_SIZE)
#define IVPU_MMU_PUD_MAP_SIZE    (IVPU_MMU_PGTABLE_ENTRIES * IVPU_MMU_PMD_MAP_SIZE)
#define IVPU_MMU_PGD_MAP_SIZE    (IVPU_MMU_PGTABLE_ENTRIES * IVPU_MMU_PUD_MAP_SIZE)
#define IVPU_MMU_PGTABLE_SIZE    (IVPU_MMU_PGTABLE_ENTRIES * sizeof(u64))

#define IVPU_MMU_DUMMY_ADDRESS 0xdeadb000
#define IVPU_MMU_ENTRY_VALID   (IVPU_MMU_ENTRY_FLAG_TYPE_PAGE | IVPU_MMU_ENTRY_FLAG_VALID)
#define IVPU_MMU_ENTRY_INVALID (IVPU_MMU_DUMMY_ADDRESS & ~IVPU_MMU_ENTRY_FLAGS_MASK)
#define IVPU_MMU_ENTRY_MAPPED  (IVPU_MMU_ENTRY_FLAG_AF | IVPU_MMU_ENTRY_FLAG_USER | \
				IVPU_MMU_ENTRY_FLAG_NG | IVPU_MMU_ENTRY_VALID)

static void *ivpu_pgtable_alloc_page(struct ivpu_device *vdev, dma_addr_t *dma)
{
	dma_addr_t dma_addr;
	struct page *page;
	void *cpu;

	page = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO);
	if (!page)
		return NULL;

	set_pages_array_wc(&page, 1);

	dma_addr = dma_map_page(vdev->drm.dev, page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(vdev->drm.dev, dma_addr))
		goto err_free_page;

	cpu = vmap(&page, 1, VM_MAP, pgprot_writecombine(PAGE_KERNEL));
	if (!cpu)
		goto err_dma_unmap_page;


	*dma = dma_addr;
	return cpu;

err_dma_unmap_page:
	dma_unmap_page(vdev->drm.dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

err_free_page:
	put_page(page);
	return NULL;
}

static void ivpu_pgtable_free_page(struct ivpu_device *vdev, u64 *cpu_addr, dma_addr_t dma_addr)
{
	struct page *page;

	if (cpu_addr) {
		page = vmalloc_to_page(cpu_addr);
		vunmap(cpu_addr);
		dma_unmap_page(vdev->drm.dev, dma_addr & ~IVPU_MMU_ENTRY_FLAGS_MASK, PAGE_SIZE,
			       DMA_BIDIRECTIONAL);
		set_pages_array_wb(&page, 1);
		put_page(page);
	}
}

static void ivpu_mmu_pgtables_free(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable)
{
	int pgd_idx, pud_idx, pmd_idx;
	dma_addr_t pud_dma, pmd_dma, pte_dma;
	u64 *pud_dma_ptr, *pmd_dma_ptr, *pte_dma_ptr;

	for (pgd_idx = 0; pgd_idx < IVPU_MMU_PGTABLE_ENTRIES; ++pgd_idx) {
		pud_dma_ptr = pgtable->pud_ptrs[pgd_idx];
		pud_dma = pgtable->pgd_dma_ptr[pgd_idx];

		if (!pud_dma_ptr)
			continue;

		for (pud_idx = 0; pud_idx < IVPU_MMU_PGTABLE_ENTRIES; ++pud_idx) {
			pmd_dma_ptr = pgtable->pmd_ptrs[pgd_idx][pud_idx];
			pmd_dma = pgtable->pud_ptrs[pgd_idx][pud_idx];

			if (!pmd_dma_ptr)
				continue;

			for (pmd_idx = 0; pmd_idx < IVPU_MMU_PGTABLE_ENTRIES; ++pmd_idx) {
				pte_dma_ptr = pgtable->pte_ptrs[pgd_idx][pud_idx][pmd_idx];
				pte_dma = pgtable->pmd_ptrs[pgd_idx][pud_idx][pmd_idx];

				ivpu_pgtable_free_page(vdev, pte_dma_ptr, pte_dma);
			}

			kfree(pgtable->pte_ptrs[pgd_idx][pud_idx]);
			ivpu_pgtable_free_page(vdev, pmd_dma_ptr, pmd_dma);
		}

		kfree(pgtable->pmd_ptrs[pgd_idx]);
		kfree(pgtable->pte_ptrs[pgd_idx]);
		ivpu_pgtable_free_page(vdev, pud_dma_ptr, pud_dma);
	}

	ivpu_pgtable_free_page(vdev, pgtable->pgd_dma_ptr, pgtable->pgd_dma);
	pgtable->pgd_dma_ptr = NULL;
	pgtable->pgd_dma = 0;
}

static u64*
ivpu_mmu_ensure_pgd(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable)
{
	u64 *pgd_dma_ptr = pgtable->pgd_dma_ptr;
	dma_addr_t pgd_dma;

	if (pgd_dma_ptr)
		return pgd_dma_ptr;

	pgd_dma_ptr = ivpu_pgtable_alloc_page(vdev, &pgd_dma);
	if (!pgd_dma_ptr)
		return NULL;

	pgtable->pgd_dma_ptr = pgd_dma_ptr;
	pgtable->pgd_dma = pgd_dma;

	return pgd_dma_ptr;
}

static u64*
ivpu_mmu_ensure_pud(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable, int pgd_idx)
{
	u64 *pud_dma_ptr = pgtable->pud_ptrs[pgd_idx];
	dma_addr_t pud_dma;

	if (pud_dma_ptr)
		return pud_dma_ptr;

	pud_dma_ptr = ivpu_pgtable_alloc_page(vdev, &pud_dma);
	if (!pud_dma_ptr)
		return NULL;

	drm_WARN_ON(&vdev->drm, pgtable->pmd_ptrs[pgd_idx]);
	pgtable->pmd_ptrs[pgd_idx] = kzalloc(IVPU_MMU_PGTABLE_SIZE, GFP_KERNEL);
	if (!pgtable->pmd_ptrs[pgd_idx])
		goto err_free_pud_dma_ptr;

	drm_WARN_ON(&vdev->drm, pgtable->pte_ptrs[pgd_idx]);
	pgtable->pte_ptrs[pgd_idx] = kzalloc(IVPU_MMU_PGTABLE_SIZE, GFP_KERNEL);
	if (!pgtable->pte_ptrs[pgd_idx])
		goto err_free_pmd_ptrs;

	pgtable->pud_ptrs[pgd_idx] = pud_dma_ptr;
	pgtable->pgd_dma_ptr[pgd_idx] = pud_dma | IVPU_MMU_ENTRY_VALID;

	return pud_dma_ptr;

err_free_pmd_ptrs:
	kfree(pgtable->pmd_ptrs[pgd_idx]);

err_free_pud_dma_ptr:
	ivpu_pgtable_free_page(vdev, pud_dma_ptr, pud_dma);
	return NULL;
}

static u64*
ivpu_mmu_ensure_pmd(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable, int pgd_idx,
		    int pud_idx)
{
	u64 *pmd_dma_ptr = pgtable->pmd_ptrs[pgd_idx][pud_idx];
	dma_addr_t pmd_dma;

	if (pmd_dma_ptr)
		return pmd_dma_ptr;

	pmd_dma_ptr = ivpu_pgtable_alloc_page(vdev, &pmd_dma);
	if (!pmd_dma_ptr)
		return NULL;

	drm_WARN_ON(&vdev->drm, pgtable->pte_ptrs[pgd_idx][pud_idx]);
	pgtable->pte_ptrs[pgd_idx][pud_idx] = kzalloc(IVPU_MMU_PGTABLE_SIZE, GFP_KERNEL);
	if (!pgtable->pte_ptrs[pgd_idx][pud_idx])
		goto err_free_pmd_dma_ptr;

	pgtable->pmd_ptrs[pgd_idx][pud_idx] = pmd_dma_ptr;
	pgtable->pud_ptrs[pgd_idx][pud_idx] = pmd_dma | IVPU_MMU_ENTRY_VALID;

	return pmd_dma_ptr;

err_free_pmd_dma_ptr:
	ivpu_pgtable_free_page(vdev, pmd_dma_ptr, pmd_dma);
	return NULL;
}

static u64*
ivpu_mmu_ensure_pte(struct ivpu_device *vdev, struct ivpu_mmu_pgtable *pgtable,
		    int pgd_idx, int pud_idx, int pmd_idx)
{
	u64 *pte_dma_ptr = pgtable->pte_ptrs[pgd_idx][pud_idx][pmd_idx];
	dma_addr_t pte_dma;

	if (pte_dma_ptr)
		return pte_dma_ptr;

	pte_dma_ptr = ivpu_pgtable_alloc_page(vdev, &pte_dma);
	if (!pte_dma_ptr)
		return NULL;

	pgtable->pte_ptrs[pgd_idx][pud_idx][pmd_idx] = pte_dma_ptr;
	pgtable->pmd_ptrs[pgd_idx][pud_idx][pmd_idx] = pte_dma | IVPU_MMU_ENTRY_VALID;

	return pte_dma_ptr;
}

static int
ivpu_mmu_context_map_page(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			  u64 vpu_addr, dma_addr_t dma_addr, u64 prot)
{
	u64 *pte;
	int pgd_idx = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
	int pud_idx = FIELD_GET(IVPU_MMU_PUD_INDEX_MASK, vpu_addr);
	int pmd_idx = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
	int pte_idx = FIELD_GET(IVPU_MMU_PTE_INDEX_MASK, vpu_addr);

	drm_WARN_ON(&vdev->drm, ctx->id == IVPU_RESERVED_CONTEXT_MMU_SSID);

	/* Allocate PGD - first level page table if needed */
	if (!ivpu_mmu_ensure_pgd(vdev, &ctx->pgtable))
		return -ENOMEM;

	/* Allocate PUD - second level page table if needed */
	if (!ivpu_mmu_ensure_pud(vdev, &ctx->pgtable, pgd_idx))
		return -ENOMEM;

	/* Allocate PMD - third level page table if needed */
	if (!ivpu_mmu_ensure_pmd(vdev, &ctx->pgtable, pgd_idx, pud_idx))
		return -ENOMEM;

	/* Allocate PTE - fourth level page table if needed */
	pte = ivpu_mmu_ensure_pte(vdev, &ctx->pgtable, pgd_idx, pud_idx, pmd_idx);
	if (!pte)
		return -ENOMEM;

	/* Update PTE */
	pte[pte_idx] = dma_addr | prot;

	return 0;
}

static int
ivpu_mmu_context_map_cont_64k(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx, u64 vpu_addr,
			      dma_addr_t dma_addr, u64 prot)
{
	size_t size = IVPU_MMU_CONT_PAGES_SIZE;

	drm_WARN_ON(&vdev->drm, !IS_ALIGNED(vpu_addr, size));
	drm_WARN_ON(&vdev->drm, !IS_ALIGNED(dma_addr, size));

	prot |= IVPU_MMU_ENTRY_FLAG_CONT;

	while (size) {
		int ret = ivpu_mmu_context_map_page(vdev, ctx, vpu_addr, dma_addr, prot);

		if (ret)
			return ret;

		size -= IVPU_MMU_PAGE_SIZE;
		vpu_addr += IVPU_MMU_PAGE_SIZE;
		dma_addr += IVPU_MMU_PAGE_SIZE;
	}

	return 0;
}

static void ivpu_mmu_context_unmap_page(struct ivpu_mmu_context *ctx, u64 vpu_addr)
{
	int pgd_idx = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
	int pud_idx = FIELD_GET(IVPU_MMU_PUD_INDEX_MASK, vpu_addr);
	int pmd_idx = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
	int pte_idx = FIELD_GET(IVPU_MMU_PTE_INDEX_MASK, vpu_addr);

	/* Update PTE with dummy physical address and clear flags */
	ctx->pgtable.pte_ptrs[pgd_idx][pud_idx][pmd_idx][pte_idx] = IVPU_MMU_ENTRY_INVALID;
}

static int
ivpu_mmu_context_map_pages(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			   u64 vpu_addr, dma_addr_t dma_addr, size_t size, u64 prot)
{
	int map_size;
	int ret;

	while (size) {
		if (!ivpu_disable_mmu_cont_pages && size >= IVPU_MMU_CONT_PAGES_SIZE &&
		    IS_ALIGNED(vpu_addr | dma_addr, IVPU_MMU_CONT_PAGES_SIZE)) {
			ret = ivpu_mmu_context_map_cont_64k(vdev, ctx, vpu_addr, dma_addr, prot);
			map_size = IVPU_MMU_CONT_PAGES_SIZE;
		} else {
			ret = ivpu_mmu_context_map_page(vdev, ctx, vpu_addr, dma_addr, prot);
			map_size = IVPU_MMU_PAGE_SIZE;
		}

		if (ret)
			return ret;

		vpu_addr += map_size;
		dma_addr += map_size;
		size -= map_size;
	}

	return 0;
}

static void ivpu_mmu_context_set_page_ro(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
					 u64 vpu_addr)
{
	int pgd_idx = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
	int pud_idx = FIELD_GET(IVPU_MMU_PUD_INDEX_MASK, vpu_addr);
	int pmd_idx = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
	int pte_idx = FIELD_GET(IVPU_MMU_PTE_INDEX_MASK, vpu_addr);

	ctx->pgtable.pte_ptrs[pgd_idx][pud_idx][pmd_idx][pte_idx] |= IVPU_MMU_ENTRY_FLAG_RO;
}

static void ivpu_mmu_context_split_page(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
					u64 vpu_addr)
{
	int pgd_idx = FIELD_GET(IVPU_MMU_PGD_INDEX_MASK, vpu_addr);
	int pud_idx = FIELD_GET(IVPU_MMU_PUD_INDEX_MASK, vpu_addr);
	int pmd_idx = FIELD_GET(IVPU_MMU_PMD_INDEX_MASK, vpu_addr);
	int pte_idx = FIELD_GET(IVPU_MMU_PTE_INDEX_MASK, vpu_addr);

	ctx->pgtable.pte_ptrs[pgd_idx][pud_idx][pmd_idx][pte_idx] &= ~IVPU_MMU_ENTRY_FLAG_CONT;
}

static void ivpu_mmu_context_split_64k_page(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
					    u64 vpu_addr)
{
	u64 start = ALIGN_DOWN(vpu_addr, IVPU_MMU_CONT_PAGES_SIZE);
	u64 end = ALIGN(vpu_addr, IVPU_MMU_CONT_PAGES_SIZE);
	u64 offset = 0;

	ivpu_dbg(vdev, MMU_MAP, "Split 64K page ctx: %u vpu_addr: 0x%llx\n", ctx->id, vpu_addr);

	while (start + offset < end) {
		ivpu_mmu_context_split_page(vdev, ctx, start + offset);
		offset += IVPU_MMU_PAGE_SIZE;
	}
}

int
ivpu_mmu_context_set_pages_ro(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx, u64 vpu_addr,
			      size_t size)
{
	u64 end = vpu_addr + size;
	size_t size_left = size;
	int ret;

	if (size == 0)
		return 0;

	if (drm_WARN_ON(&vdev->drm, !IS_ALIGNED(vpu_addr | size, IVPU_MMU_PAGE_SIZE)))
		return -EINVAL;

	mutex_lock(&ctx->lock);

	ivpu_dbg(vdev, MMU_MAP, "Set read-only pages ctx: %u vpu_addr: 0x%llx size: %lu\n",
		 ctx->id, vpu_addr, size);

	if (!ivpu_disable_mmu_cont_pages) {
		/* Split 64K contiguous page at the beginning if needed */
		if (!IS_ALIGNED(vpu_addr, IVPU_MMU_CONT_PAGES_SIZE))
			ivpu_mmu_context_split_64k_page(vdev, ctx, vpu_addr);

		/* Split 64K contiguous page at the end if needed */
		if (!IS_ALIGNED(vpu_addr + size, IVPU_MMU_CONT_PAGES_SIZE))
			ivpu_mmu_context_split_64k_page(vdev, ctx, vpu_addr + size);
	}

	while (size_left) {
		if (vpu_addr < end)
			ivpu_mmu_context_set_page_ro(vdev, ctx, vpu_addr);

		vpu_addr += IVPU_MMU_PAGE_SIZE;
		size_left -= IVPU_MMU_PAGE_SIZE;
	}

	/* Ensure page table modifications are flushed from wc buffers to memory */
	wmb();

	mutex_unlock(&ctx->lock);
	ret = ivpu_mmu_invalidate_tlb(vdev, ctx->id);
	if (ret)
		ivpu_err(vdev, "Failed to invalidate TLB for ctx %u: %d\n", ctx->id, ret);

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
	size_t start_vpu_addr = vpu_addr;
	struct scatterlist *sg;
	int ret;
	u64 prot;
	u64 i;

	if (drm_WARN_ON(&vdev->drm, !ctx))
		return -EINVAL;

	if (!IS_ALIGNED(vpu_addr, IVPU_MMU_PAGE_SIZE))
		return -EINVAL;

	if (vpu_addr & ~IVPU_MMU_VPU_ADDRESS_MASK)
		return -EINVAL;

	prot = IVPU_MMU_ENTRY_MAPPED;
	if (llc_coherent)
		prot |= IVPU_MMU_ENTRY_FLAG_LLC_COHERENT;

	mutex_lock(&ctx->lock);

	for_each_sgtable_dma_sg(sgt, sg, i) {
		dma_addr_t dma_addr = sg_dma_address(sg) - sg->offset;
		size_t size = sg_dma_len(sg) + sg->offset;

		ivpu_dbg(vdev, MMU_MAP, "Map ctx: %u dma_addr: 0x%llx vpu_addr: 0x%llx size: %lu\n",
			 ctx->id, dma_addr, vpu_addr, size);

		ret = ivpu_mmu_context_map_pages(vdev, ctx, vpu_addr, dma_addr, size, prot);
		if (ret) {
			ivpu_err(vdev, "Failed to map context pages\n");
			goto err_unmap_pages;
		}
		vpu_addr += size;
	}

	if (!ctx->is_cd_valid) {
		ret = ivpu_mmu_cd_set(vdev, ctx->id, &ctx->pgtable);
		if (ret) {
			ivpu_err(vdev, "Failed to set context descriptor for context %u: %d\n",
				 ctx->id, ret);
			goto err_unmap_pages;
		}
		ctx->is_cd_valid = true;
	}

	/* Ensure page table modifications are flushed from wc buffers to memory */
	wmb();

	ret = ivpu_mmu_invalidate_tlb(vdev, ctx->id);
	if (ret) {
		ivpu_err(vdev, "Failed to invalidate TLB for ctx %u: %d\n", ctx->id, ret);
		goto err_unmap_pages;
	}

	mutex_unlock(&ctx->lock);
	return 0;

err_unmap_pages:
	ivpu_mmu_context_unmap_pages(ctx, start_vpu_addr, vpu_addr - start_vpu_addr);
	mutex_unlock(&ctx->lock);
	return ret;
}

void
ivpu_mmu_context_unmap_sgt(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			   u64 vpu_addr, struct sg_table *sgt)
{
	struct scatterlist *sg;
	int ret;
	u64 i;

	if (drm_WARN_ON(&vdev->drm, !ctx))
		return;

	mutex_lock(&ctx->lock);

	for_each_sgtable_dma_sg(sgt, sg, i) {
		dma_addr_t dma_addr = sg_dma_address(sg) - sg->offset;
		size_t size = sg_dma_len(sg) + sg->offset;

		ivpu_dbg(vdev, MMU_MAP, "Unmap ctx: %u dma_addr: 0x%llx vpu_addr: 0x%llx size: %lu\n",
			 ctx->id, dma_addr, vpu_addr, size);

		ivpu_mmu_context_unmap_pages(ctx, vpu_addr, size);
		vpu_addr += size;
	}

	/* Ensure page table modifications are flushed from wc buffers to memory */
	wmb();

	mutex_unlock(&ctx->lock);

	ret = ivpu_mmu_invalidate_tlb(vdev, ctx->id);
	if (ret)
		ivpu_warn(vdev, "Failed to invalidate TLB for ctx %u: %d\n", ctx->id, ret);
}

int
ivpu_mmu_context_insert_node(struct ivpu_mmu_context *ctx, const struct ivpu_addr_range *range,
			     u64 size, struct drm_mm_node *node)
{
	int ret;

	WARN_ON(!range);

	mutex_lock(&ctx->lock);
	if (!ivpu_disable_mmu_cont_pages && size >= IVPU_MMU_CONT_PAGES_SIZE) {
		ret = drm_mm_insert_node_in_range(&ctx->mm, node, size, IVPU_MMU_CONT_PAGES_SIZE, 0,
						  range->start, range->end, DRM_MM_INSERT_BEST);
		if (!ret)
			goto unlock;
	}

	ret = drm_mm_insert_node_in_range(&ctx->mm, node, size, IVPU_MMU_PAGE_SIZE, 0,
					  range->start, range->end, DRM_MM_INSERT_BEST);
unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

void
ivpu_mmu_context_remove_node(struct ivpu_mmu_context *ctx, struct drm_mm_node *node)
{
	mutex_lock(&ctx->lock);
	drm_mm_remove_node(node);
	mutex_unlock(&ctx->lock);
}

void ivpu_mmu_context_init(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx, u32 context_id)
{
	u64 start, end;

	mutex_init(&ctx->lock);

	if (!context_id) {
		start = vdev->hw->ranges.global.start;
		end = vdev->hw->ranges.shave.end;
	} else {
		start = min_t(u64, vdev->hw->ranges.user.start, vdev->hw->ranges.shave.start);
		end = max_t(u64, vdev->hw->ranges.user.end, vdev->hw->ranges.dma.end);
	}

	drm_mm_init(&ctx->mm, start, end - start);
	ctx->id = context_id;
}

void ivpu_mmu_context_fini(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx)
{
	if (ctx->is_cd_valid) {
		ivpu_mmu_cd_clear(vdev, ctx->id);
		ctx->is_cd_valid = false;
	}

	mutex_destroy(&ctx->lock);
	ivpu_mmu_pgtables_free(vdev, &ctx->pgtable);
	drm_mm_takedown(&ctx->mm);
}

void ivpu_mmu_global_context_init(struct ivpu_device *vdev)
{
	ivpu_mmu_context_init(vdev, &vdev->gctx, IVPU_GLOBAL_CONTEXT_MMU_SSID);
}

void ivpu_mmu_global_context_fini(struct ivpu_device *vdev)
{
	ivpu_mmu_context_fini(vdev, &vdev->gctx);
}

int ivpu_mmu_reserved_context_init(struct ivpu_device *vdev)
{
	int ret;

	ivpu_mmu_context_init(vdev, &vdev->rctx, IVPU_RESERVED_CONTEXT_MMU_SSID);

	mutex_lock(&vdev->rctx.lock);

	if (!ivpu_mmu_ensure_pgd(vdev, &vdev->rctx.pgtable)) {
		ivpu_err(vdev, "Failed to allocate root page table for reserved context\n");
		ret = -ENOMEM;
		goto err_ctx_fini;
	}

	ret = ivpu_mmu_cd_set(vdev, vdev->rctx.id, &vdev->rctx.pgtable);
	if (ret) {
		ivpu_err(vdev, "Failed to set context descriptor for reserved context\n");
		goto err_ctx_fini;
	}

	mutex_unlock(&vdev->rctx.lock);
	return ret;

err_ctx_fini:
	mutex_unlock(&vdev->rctx.lock);
	ivpu_mmu_context_fini(vdev, &vdev->rctx);
	return ret;
}

void ivpu_mmu_reserved_context_fini(struct ivpu_device *vdev)
{
	ivpu_mmu_cd_clear(vdev, vdev->rctx.id);
	ivpu_mmu_context_fini(vdev, &vdev->rctx);
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
