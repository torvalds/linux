// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <rdma/ib_umem.h>

#include <linux/bnge/hsi.h>
#include "bng_res.h"
#include "bng_roce_hsi.h"

/* Stats */
void bng_re_free_stats_ctx_mem(struct pci_dev *pdev,
			       struct bng_re_stats *stats)
{
	if (stats->dma) {
		dma_free_coherent(&pdev->dev, stats->size,
				  stats->dma, stats->dma_map);
	}
	memset(stats, 0, sizeof(*stats));
	stats->fw_id = -1;
}

int bng_re_alloc_stats_ctx_mem(struct pci_dev *pdev,
			       struct bng_re_chip_ctx *cctx,
			       struct bng_re_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->fw_id = -1;
	stats->size = cctx->hw_stats_size;
	stats->dma = dma_alloc_coherent(&pdev->dev, stats->size,
					&stats->dma_map, GFP_KERNEL);
	if (!stats->dma)
		return -ENOMEM;

	return 0;
}

static void bng_free_pbl(struct bng_re_res  *res, struct bng_re_pbl *pbl)
{
	struct pci_dev *pdev = res->pdev;
	int i;

	for (i = 0; i < pbl->pg_count; i++) {
		if (pbl->pg_arr[i])
			dma_free_coherent(&pdev->dev, pbl->pg_size,
					  (void *)((unsigned long)
					     pbl->pg_arr[i] &
						PAGE_MASK),
					  pbl->pg_map_arr[i]);
		else
			dev_warn(&pdev->dev,
					"PBL free pg_arr[%d] empty?!\n", i);
		pbl->pg_arr[i] = NULL;
	}

	vfree(pbl->pg_arr);
	pbl->pg_arr = NULL;
	vfree(pbl->pg_map_arr);
	pbl->pg_map_arr = NULL;
	pbl->pg_count = 0;
	pbl->pg_size = 0;
}

static int bng_alloc_pbl(struct bng_re_res  *res,
			 struct bng_re_pbl *pbl,
			 struct bng_re_sg_info *sginfo)
{
	struct pci_dev *pdev = res->pdev;
	u32 pages;
	int i;

	if (sginfo->nopte)
		return 0;
	pages = sginfo->npages;

	/* page ptr arrays */
	pbl->pg_arr = vmalloc_array(pages, sizeof(void *));
	if (!pbl->pg_arr)
		return -ENOMEM;

	pbl->pg_map_arr = vmalloc_array(pages, sizeof(dma_addr_t));
	if (!pbl->pg_map_arr) {
		vfree(pbl->pg_arr);
		pbl->pg_arr = NULL;
		return -ENOMEM;
	}
	pbl->pg_count = 0;
	pbl->pg_size = sginfo->pgsize;

	for (i = 0; i < pages; i++) {
		pbl->pg_arr[i] = dma_alloc_coherent(&pdev->dev,
				pbl->pg_size,
				&pbl->pg_map_arr[i],
				GFP_KERNEL);
		if (!pbl->pg_arr[i])
			goto fail;
		pbl->pg_count++;
	}

	return 0;
fail:
	bng_free_pbl(res, pbl);
	return -ENOMEM;
}

void bng_re_free_hwq(struct bng_re_res *res,
		     struct bng_re_hwq *hwq)
{
	int i;

	if (!hwq->max_elements)
		return;
	if (hwq->level >= BNG_PBL_LVL_MAX)
		return;

	for (i = 0; i < hwq->level + 1; i++)
		bng_free_pbl(res, &hwq->pbl[i]);

	hwq->level = BNG_PBL_LVL_MAX;
	hwq->max_elements = 0;
	hwq->element_size = 0;
	hwq->prod = 0;
	hwq->cons = 0;
}

/* All HWQs are power of 2 in size */
int bng_re_alloc_init_hwq(struct bng_re_hwq *hwq,
			  struct bng_re_hwq_attr *hwq_attr)
{
	u32 npages, pg_size;
	struct bng_re_sg_info sginfo = {};
	u32 depth, stride, npbl, npde;
	dma_addr_t *src_phys_ptr, **dst_virt_ptr;
	struct bng_re_res *res;
	struct pci_dev *pdev;
	int i, rc, lvl;

	res = hwq_attr->res;
	pdev = res->pdev;
	pg_size = hwq_attr->sginfo->pgsize;
	hwq->level = BNG_PBL_LVL_MAX;

	depth = roundup_pow_of_two(hwq_attr->depth);
	stride = roundup_pow_of_two(hwq_attr->stride);

	npages = (depth * stride) / pg_size;
	if ((depth * stride) % pg_size)
		npages++;
	if (!npages)
		return -EINVAL;
	hwq_attr->sginfo->npages = npages;

	if (npages == MAX_PBL_LVL_0_PGS && !hwq_attr->sginfo->nopte) {
		/* This request is Level 0, map PTE */
		rc = bng_alloc_pbl(res, &hwq->pbl[BNG_PBL_LVL_0], hwq_attr->sginfo);
		if (rc)
			goto fail;
		hwq->level = BNG_PBL_LVL_0;
		goto done;
	}

	if (npages >= MAX_PBL_LVL_0_PGS) {
		if (npages > MAX_PBL_LVL_1_PGS) {
			u32 flag = PTU_PTE_VALID;
			/* 2 levels of indirection */
			npbl = npages >> MAX_PBL_LVL_1_PGS_SHIFT;
			if (npages % BIT(MAX_PBL_LVL_1_PGS_SHIFT))
				npbl++;
			npde = npbl >> MAX_PDL_LVL_SHIFT;
			if (npbl % BIT(MAX_PDL_LVL_SHIFT))
				npde++;
			/* Alloc PDE pages */
			sginfo.pgsize = npde * pg_size;
			sginfo.npages = 1;
			rc = bng_alloc_pbl(res, &hwq->pbl[BNG_PBL_LVL_0], &sginfo);
			if (rc)
				goto fail;

			/* Alloc PBL pages */
			sginfo.npages = npbl;
			sginfo.pgsize = PAGE_SIZE;
			rc = bng_alloc_pbl(res, &hwq->pbl[BNG_PBL_LVL_1], &sginfo);
			if (rc)
				goto fail;
			/* Fill PDL with PBL page pointers */
			dst_virt_ptr =
				(dma_addr_t **)hwq->pbl[BNG_PBL_LVL_0].pg_arr;
			src_phys_ptr = hwq->pbl[BNG_PBL_LVL_1].pg_map_arr;
			for (i = 0; i < hwq->pbl[BNG_PBL_LVL_1].pg_count; i++)
				dst_virt_ptr[0][i] = src_phys_ptr[i] | flag;

			/* Alloc or init PTEs */
			rc = bng_alloc_pbl(res, &hwq->pbl[BNG_PBL_LVL_2],
					 hwq_attr->sginfo);
			if (rc)
				goto fail;
			hwq->level = BNG_PBL_LVL_2;
			if (hwq_attr->sginfo->nopte)
				goto done;
			/* Fill PBLs with PTE pointers */
			dst_virt_ptr =
				(dma_addr_t **)hwq->pbl[BNG_PBL_LVL_1].pg_arr;
			src_phys_ptr = hwq->pbl[BNG_PBL_LVL_2].pg_map_arr;
			for (i = 0; i < hwq->pbl[BNG_PBL_LVL_2].pg_count; i++) {
				dst_virt_ptr[PTR_PG(i)][PTR_IDX(i)] =
					src_phys_ptr[i] | PTU_PTE_VALID;
			}
			if (hwq_attr->type == BNG_HWQ_TYPE_QUEUE) {
				/* Find the last pg of the size */
				i = hwq->pbl[BNG_PBL_LVL_2].pg_count;
				dst_virt_ptr[PTR_PG(i - 1)][PTR_IDX(i - 1)] |=
								  PTU_PTE_LAST;
				if (i > 1)
					dst_virt_ptr[PTR_PG(i - 2)]
						    [PTR_IDX(i - 2)] |=
						    PTU_PTE_NEXT_TO_LAST;
			}
		} else { /* pages < 512 npbl = 1, npde = 0 */
			u32 flag = PTU_PTE_VALID;

			/* 1 level of indirection */
			npbl = npages >> MAX_PBL_LVL_1_PGS_SHIFT;
			if (npages % BIT(MAX_PBL_LVL_1_PGS_SHIFT))
				npbl++;
			sginfo.npages = npbl;
			sginfo.pgsize = PAGE_SIZE;
			/* Alloc PBL page */
			rc = bng_alloc_pbl(res, &hwq->pbl[BNG_PBL_LVL_0], &sginfo);
			if (rc)
				goto fail;
			/* Alloc or init  PTEs */
			rc = bng_alloc_pbl(res, &hwq->pbl[BNG_PBL_LVL_1],
					 hwq_attr->sginfo);
			if (rc)
				goto fail;
			hwq->level = BNG_PBL_LVL_1;
			if (hwq_attr->sginfo->nopte)
				goto done;
			/* Fill PBL with PTE pointers */
			dst_virt_ptr =
				(dma_addr_t **)hwq->pbl[BNG_PBL_LVL_0].pg_arr;
			src_phys_ptr = hwq->pbl[BNG_PBL_LVL_1].pg_map_arr;
			for (i = 0; i < hwq->pbl[BNG_PBL_LVL_1].pg_count; i++)
				dst_virt_ptr[PTR_PG(i)][PTR_IDX(i)] =
					src_phys_ptr[i] | flag;
			if (hwq_attr->type == BNG_HWQ_TYPE_QUEUE) {
				/* Find the last pg of the size */
				i = hwq->pbl[BNG_PBL_LVL_1].pg_count;
				dst_virt_ptr[PTR_PG(i - 1)][PTR_IDX(i - 1)] |=
								  PTU_PTE_LAST;
				if (i > 1)
					dst_virt_ptr[PTR_PG(i - 2)]
						    [PTR_IDX(i - 2)] |=
						    PTU_PTE_NEXT_TO_LAST;
			}
		}
	}
done:
	hwq->prod = 0;
	hwq->cons = 0;
	hwq->pdev = pdev;
	hwq->depth = hwq_attr->depth;
	hwq->max_elements = hwq->depth;
	hwq->element_size = stride;
	hwq->qe_ppg = pg_size / stride;
	/* For direct access to the elements */
	lvl = hwq->level;
	if (hwq_attr->sginfo->nopte && hwq->level)
		lvl = hwq->level - 1;
	hwq->pbl_ptr = hwq->pbl[lvl].pg_arr;
	hwq->pbl_dma_ptr = hwq->pbl[lvl].pg_map_arr;
	spin_lock_init(&hwq->lock);

	return 0;
fail:
	bng_re_free_hwq(res, hwq);
	return -ENOMEM;
}
