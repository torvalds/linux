/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: QPLib resource manager
 */

#define dev_fmt(fmt) "QPLIB: " fmt

#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/inetdevice.h>
#include <linux/dma-mapping.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>

#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_rcfw.h"

static void bnxt_qplib_free_stats_ctx(struct pci_dev *pdev,
				      struct bnxt_qplib_stats *stats);
static int bnxt_qplib_alloc_stats_ctx(struct pci_dev *pdev,
				      struct bnxt_qplib_chip_ctx *cctx,
				      struct bnxt_qplib_stats *stats);

/* PBL */
static void __free_pbl(struct bnxt_qplib_res *res, struct bnxt_qplib_pbl *pbl,
		       bool is_umem)
{
	struct pci_dev *pdev = res->pdev;
	int i;

	if (!is_umem) {
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
	}
	vfree(pbl->pg_arr);
	pbl->pg_arr = NULL;
	vfree(pbl->pg_map_arr);
	pbl->pg_map_arr = NULL;
	pbl->pg_count = 0;
	pbl->pg_size = 0;
}

static void bnxt_qplib_fill_user_dma_pages(struct bnxt_qplib_pbl *pbl,
					   struct bnxt_qplib_sg_info *sginfo)
{
	struct ib_block_iter biter;
	int i = 0;

	rdma_umem_for_each_dma_block(sginfo->umem, &biter, sginfo->pgsize) {
		pbl->pg_map_arr[i] = rdma_block_iter_dma_address(&biter);
		pbl->pg_arr[i] = NULL;
		pbl->pg_count++;
		i++;
	}
}

static int __alloc_pbl(struct bnxt_qplib_res *res,
		       struct bnxt_qplib_pbl *pbl,
		       struct bnxt_qplib_sg_info *sginfo)
{
	struct pci_dev *pdev = res->pdev;
	bool is_umem = false;
	u32 pages;
	int i;

	if (sginfo->nopte)
		return 0;
	if (sginfo->umem)
		pages = ib_umem_num_dma_blocks(sginfo->umem, sginfo->pgsize);
	else
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

	if (!sginfo->umem) {
		for (i = 0; i < pages; i++) {
			pbl->pg_arr[i] = dma_alloc_coherent(&pdev->dev,
							    pbl->pg_size,
							    &pbl->pg_map_arr[i],
							    GFP_KERNEL);
			if (!pbl->pg_arr[i])
				goto fail;
			pbl->pg_count++;
		}
	} else {
		is_umem = true;
		bnxt_qplib_fill_user_dma_pages(pbl, sginfo);
	}

	return 0;
fail:
	__free_pbl(res, pbl, is_umem);
	return -ENOMEM;
}

/* HWQ */
void bnxt_qplib_free_hwq(struct bnxt_qplib_res *res,
			 struct bnxt_qplib_hwq *hwq)
{
	int i;

	if (!hwq->max_elements)
		return;
	if (hwq->level >= PBL_LVL_MAX)
		return;

	for (i = 0; i < hwq->level + 1; i++) {
		if (i == hwq->level)
			__free_pbl(res, &hwq->pbl[i], hwq->is_user);
		else
			__free_pbl(res, &hwq->pbl[i], false);
	}

	hwq->level = PBL_LVL_MAX;
	hwq->max_elements = 0;
	hwq->element_size = 0;
	hwq->prod = 0;
	hwq->cons = 0;
	hwq->cp_bit = 0;
}

/* All HWQs are power of 2 in size */

int bnxt_qplib_alloc_init_hwq(struct bnxt_qplib_hwq *hwq,
			      struct bnxt_qplib_hwq_attr *hwq_attr)
{
	u32 npages, aux_slots, pg_size, aux_pages = 0, aux_size = 0;
	struct bnxt_qplib_sg_info sginfo = {};
	u32 depth, stride, npbl, npde;
	dma_addr_t *src_phys_ptr, **dst_virt_ptr;
	struct bnxt_qplib_res *res;
	struct pci_dev *pdev;
	int i, rc, lvl;

	res = hwq_attr->res;
	pdev = res->pdev;
	pg_size = hwq_attr->sginfo->pgsize;
	hwq->level = PBL_LVL_MAX;

	depth = roundup_pow_of_two(hwq_attr->depth);
	stride = roundup_pow_of_two(hwq_attr->stride);
	if (hwq_attr->aux_depth) {
		aux_slots = hwq_attr->aux_depth;
		aux_size = roundup_pow_of_two(hwq_attr->aux_stride);
		aux_pages = (aux_slots * aux_size) / pg_size;
		if ((aux_slots * aux_size) % pg_size)
			aux_pages++;
	}

	if (!hwq_attr->sginfo->umem) {
		hwq->is_user = false;
		npages = (depth * stride) / pg_size + aux_pages;
		if ((depth * stride) % pg_size)
			npages++;
		if (!npages)
			return -EINVAL;
		hwq_attr->sginfo->npages = npages;
	} else {
		npages = ib_umem_num_dma_blocks(hwq_attr->sginfo->umem,
						hwq_attr->sginfo->pgsize);
		hwq->is_user = true;
	}

	if (npages == MAX_PBL_LVL_0_PGS && !hwq_attr->sginfo->nopte) {
		/* This request is Level 0, map PTE */
		rc = __alloc_pbl(res, &hwq->pbl[PBL_LVL_0], hwq_attr->sginfo);
		if (rc)
			goto fail;
		hwq->level = PBL_LVL_0;
		goto done;
	}

	if (npages >= MAX_PBL_LVL_0_PGS) {
		if (npages > MAX_PBL_LVL_1_PGS) {
			u32 flag = (hwq_attr->type == HWQ_TYPE_L2_CMPL) ?
				    0 : PTU_PTE_VALID;
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
			rc = __alloc_pbl(res, &hwq->pbl[PBL_LVL_0], &sginfo);

			/* Alloc PBL pages */
			sginfo.npages = npbl;
			sginfo.pgsize = PAGE_SIZE;
			rc = __alloc_pbl(res, &hwq->pbl[PBL_LVL_1], &sginfo);
			if (rc)
				goto fail;
			/* Fill PDL with PBL page pointers */
			dst_virt_ptr =
				(dma_addr_t **)hwq->pbl[PBL_LVL_0].pg_arr;
			src_phys_ptr = hwq->pbl[PBL_LVL_1].pg_map_arr;
			if (hwq_attr->type == HWQ_TYPE_MR) {
			/* For MR it is expected that we supply only 1 contigous
			 * page i.e only 1 entry in the PDL that will contain
			 * all the PBLs for the user supplied memory region
			 */
				for (i = 0; i < hwq->pbl[PBL_LVL_1].pg_count;
				     i++)
					dst_virt_ptr[0][i] = src_phys_ptr[i] |
						flag;
			} else {
				for (i = 0; i < hwq->pbl[PBL_LVL_1].pg_count;
				     i++)
					dst_virt_ptr[PTR_PG(i)][PTR_IDX(i)] =
						src_phys_ptr[i] |
						PTU_PDE_VALID;
			}
			/* Alloc or init PTEs */
			rc = __alloc_pbl(res, &hwq->pbl[PBL_LVL_2],
					 hwq_attr->sginfo);
			if (rc)
				goto fail;
			hwq->level = PBL_LVL_2;
			if (hwq_attr->sginfo->nopte)
				goto done;
			/* Fill PBLs with PTE pointers */
			dst_virt_ptr =
				(dma_addr_t **)hwq->pbl[PBL_LVL_1].pg_arr;
			src_phys_ptr = hwq->pbl[PBL_LVL_2].pg_map_arr;
			for (i = 0; i < hwq->pbl[PBL_LVL_2].pg_count; i++) {
				dst_virt_ptr[PTR_PG(i)][PTR_IDX(i)] =
					src_phys_ptr[i] | PTU_PTE_VALID;
			}
			if (hwq_attr->type == HWQ_TYPE_QUEUE) {
				/* Find the last pg of the size */
				i = hwq->pbl[PBL_LVL_2].pg_count;
				dst_virt_ptr[PTR_PG(i - 1)][PTR_IDX(i - 1)] |=
								  PTU_PTE_LAST;
				if (i > 1)
					dst_virt_ptr[PTR_PG(i - 2)]
						    [PTR_IDX(i - 2)] |=
						    PTU_PTE_NEXT_TO_LAST;
			}
		} else { /* pages < 512 npbl = 1, npde = 0 */
			u32 flag = (hwq_attr->type == HWQ_TYPE_L2_CMPL) ?
				    0 : PTU_PTE_VALID;

			/* 1 level of indirection */
			npbl = npages >> MAX_PBL_LVL_1_PGS_SHIFT;
			if (npages % BIT(MAX_PBL_LVL_1_PGS_SHIFT))
				npbl++;
			sginfo.npages = npbl;
			sginfo.pgsize = PAGE_SIZE;
			/* Alloc PBL page */
			rc = __alloc_pbl(res, &hwq->pbl[PBL_LVL_0], &sginfo);
			if (rc)
				goto fail;
			/* Alloc or init  PTEs */
			rc = __alloc_pbl(res, &hwq->pbl[PBL_LVL_1],
					 hwq_attr->sginfo);
			if (rc)
				goto fail;
			hwq->level = PBL_LVL_1;
			if (hwq_attr->sginfo->nopte)
				goto done;
			/* Fill PBL with PTE pointers */
			dst_virt_ptr =
				(dma_addr_t **)hwq->pbl[PBL_LVL_0].pg_arr;
			src_phys_ptr = hwq->pbl[PBL_LVL_1].pg_map_arr;
			for (i = 0; i < hwq->pbl[PBL_LVL_1].pg_count; i++)
				dst_virt_ptr[PTR_PG(i)][PTR_IDX(i)] =
					src_phys_ptr[i] | flag;
			if (hwq_attr->type == HWQ_TYPE_QUEUE) {
				/* Find the last pg of the size */
				i = hwq->pbl[PBL_LVL_1].pg_count;
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
	bnxt_qplib_free_hwq(res, hwq);
	return -ENOMEM;
}

/* Context Tables */
void bnxt_qplib_free_ctx(struct bnxt_qplib_res *res,
			 struct bnxt_qplib_ctx *ctx)
{
	int i;

	bnxt_qplib_free_hwq(res, &ctx->qpc_tbl);
	bnxt_qplib_free_hwq(res, &ctx->mrw_tbl);
	bnxt_qplib_free_hwq(res, &ctx->srqc_tbl);
	bnxt_qplib_free_hwq(res, &ctx->cq_tbl);
	bnxt_qplib_free_hwq(res, &ctx->tim_tbl);
	for (i = 0; i < MAX_TQM_ALLOC_REQ; i++)
		bnxt_qplib_free_hwq(res, &ctx->tqm_ctx.qtbl[i]);
	/* restore original pde level before destroy */
	ctx->tqm_ctx.pde.level = ctx->tqm_ctx.pde_level;
	bnxt_qplib_free_hwq(res, &ctx->tqm_ctx.pde);
	bnxt_qplib_free_stats_ctx(res->pdev, &ctx->stats);
}

static int bnxt_qplib_alloc_tqm_rings(struct bnxt_qplib_res *res,
				      struct bnxt_qplib_ctx *ctx)
{
	struct bnxt_qplib_hwq_attr hwq_attr = {};
	struct bnxt_qplib_sg_info sginfo = {};
	struct bnxt_qplib_tqm_ctx *tqmctx;
	int rc;
	int i;

	tqmctx = &ctx->tqm_ctx;

	sginfo.pgsize = PAGE_SIZE;
	sginfo.pgshft = PAGE_SHIFT;
	hwq_attr.sginfo = &sginfo;
	hwq_attr.res = res;
	hwq_attr.type = HWQ_TYPE_CTX;
	hwq_attr.depth = 512;
	hwq_attr.stride = sizeof(u64);
	/* Alloc pdl buffer */
	rc = bnxt_qplib_alloc_init_hwq(&tqmctx->pde, &hwq_attr);
	if (rc)
		goto out;
	/* Save original pdl level */
	tqmctx->pde_level = tqmctx->pde.level;

	hwq_attr.stride = 1;
	for (i = 0; i < MAX_TQM_ALLOC_REQ; i++) {
		if (!tqmctx->qcount[i])
			continue;
		hwq_attr.depth = ctx->qpc_count * tqmctx->qcount[i];
		rc = bnxt_qplib_alloc_init_hwq(&tqmctx->qtbl[i], &hwq_attr);
		if (rc)
			goto out;
	}
out:
	return rc;
}

static void bnxt_qplib_map_tqm_pgtbl(struct bnxt_qplib_tqm_ctx *ctx)
{
	struct bnxt_qplib_hwq *tbl;
	dma_addr_t *dma_ptr;
	__le64 **pbl_ptr, *ptr;
	int i, j, k;
	int fnz_idx = -1;
	int pg_count;

	pbl_ptr = (__le64 **)ctx->pde.pbl_ptr;

	for (i = 0, j = 0; i < MAX_TQM_ALLOC_REQ;
	     i++, j += MAX_TQM_ALLOC_BLK_SIZE) {
		tbl = &ctx->qtbl[i];
		if (!tbl->max_elements)
			continue;
		if (fnz_idx == -1)
			fnz_idx = i; /* first non-zero index */
		switch (tbl->level) {
		case PBL_LVL_2:
			pg_count = tbl->pbl[PBL_LVL_1].pg_count;
			for (k = 0; k < pg_count; k++) {
				ptr = &pbl_ptr[PTR_PG(j + k)][PTR_IDX(j + k)];
				dma_ptr = &tbl->pbl[PBL_LVL_1].pg_map_arr[k];
				*ptr = cpu_to_le64(*dma_ptr | PTU_PTE_VALID);
			}
			break;
		case PBL_LVL_1:
		case PBL_LVL_0:
		default:
			ptr = &pbl_ptr[PTR_PG(j)][PTR_IDX(j)];
			*ptr = cpu_to_le64(tbl->pbl[PBL_LVL_0].pg_map_arr[0] |
					   PTU_PTE_VALID);
			break;
		}
	}
	if (fnz_idx == -1)
		fnz_idx = 0;
	/* update pde level as per page table programming */
	ctx->pde.level = (ctx->qtbl[fnz_idx].level == PBL_LVL_2) ? PBL_LVL_2 :
			  ctx->qtbl[fnz_idx].level + 1;
}

static int bnxt_qplib_setup_tqm_rings(struct bnxt_qplib_res *res,
				      struct bnxt_qplib_ctx *ctx)
{
	int rc;

	rc = bnxt_qplib_alloc_tqm_rings(res, ctx);
	if (rc)
		goto fail;

	bnxt_qplib_map_tqm_pgtbl(&ctx->tqm_ctx);
fail:
	return rc;
}

/*
 * Routine: bnxt_qplib_alloc_ctx
 * Description:
 *     Context tables are memories which are used by the chip fw.
 *     The 6 tables defined are:
 *             QPC ctx - holds QP states
 *             MRW ctx - holds memory region and window
 *             SRQ ctx - holds shared RQ states
 *             CQ ctx - holds completion queue states
 *             TQM ctx - holds Tx Queue Manager context
 *             TIM ctx - holds timer context
 *     Depending on the size of the tbl requested, either a 1 Page Buffer List
 *     or a 1-to-2-stage indirection Page Directory List + 1 PBL is used
 *     instead.
 *     Table might be employed as follows:
 *             For 0      < ctx size <= 1 PAGE, 0 level of ind is used
 *             For 1 PAGE < ctx size <= 512 entries size, 1 level of ind is used
 *             For 512    < ctx size <= MAX, 2 levels of ind is used
 * Returns:
 *     0 if success, else -ERRORS
 */
int bnxt_qplib_alloc_ctx(struct bnxt_qplib_res *res,
			 struct bnxt_qplib_ctx *ctx,
			 bool virt_fn, bool is_p5)
{
	struct bnxt_qplib_hwq_attr hwq_attr = {};
	struct bnxt_qplib_sg_info sginfo = {};
	int rc;

	if (virt_fn || is_p5)
		goto stats_alloc;

	/* QPC Tables */
	sginfo.pgsize = PAGE_SIZE;
	sginfo.pgshft = PAGE_SHIFT;
	hwq_attr.sginfo = &sginfo;

	hwq_attr.res = res;
	hwq_attr.depth = ctx->qpc_count;
	hwq_attr.stride = BNXT_QPLIB_MAX_QP_CTX_ENTRY_SIZE;
	hwq_attr.type = HWQ_TYPE_CTX;
	rc = bnxt_qplib_alloc_init_hwq(&ctx->qpc_tbl, &hwq_attr);
	if (rc)
		goto fail;

	/* MRW Tables */
	hwq_attr.depth = ctx->mrw_count;
	hwq_attr.stride = BNXT_QPLIB_MAX_MRW_CTX_ENTRY_SIZE;
	rc = bnxt_qplib_alloc_init_hwq(&ctx->mrw_tbl, &hwq_attr);
	if (rc)
		goto fail;

	/* SRQ Tables */
	hwq_attr.depth = ctx->srqc_count;
	hwq_attr.stride = BNXT_QPLIB_MAX_SRQ_CTX_ENTRY_SIZE;
	rc = bnxt_qplib_alloc_init_hwq(&ctx->srqc_tbl, &hwq_attr);
	if (rc)
		goto fail;

	/* CQ Tables */
	hwq_attr.depth = ctx->cq_count;
	hwq_attr.stride = BNXT_QPLIB_MAX_CQ_CTX_ENTRY_SIZE;
	rc = bnxt_qplib_alloc_init_hwq(&ctx->cq_tbl, &hwq_attr);
	if (rc)
		goto fail;

	/* TQM Buffer */
	rc = bnxt_qplib_setup_tqm_rings(res, ctx);
	if (rc)
		goto fail;
	/* TIM Buffer */
	ctx->tim_tbl.max_elements = ctx->qpc_count * 16;
	hwq_attr.depth = ctx->qpc_count * 16;
	hwq_attr.stride = 1;
	rc = bnxt_qplib_alloc_init_hwq(&ctx->tim_tbl, &hwq_attr);
	if (rc)
		goto fail;
stats_alloc:
	/* Stats */
	rc = bnxt_qplib_alloc_stats_ctx(res->pdev, res->cctx, &ctx->stats);
	if (rc)
		goto fail;

	return 0;

fail:
	bnxt_qplib_free_ctx(res, ctx);
	return rc;
}

static void bnxt_qplib_free_sgid_tbl(struct bnxt_qplib_res *res,
				     struct bnxt_qplib_sgid_tbl *sgid_tbl)
{
	kfree(sgid_tbl->tbl);
	kfree(sgid_tbl->hw_id);
	kfree(sgid_tbl->ctx);
	kfree(sgid_tbl->vlan);
	sgid_tbl->tbl = NULL;
	sgid_tbl->hw_id = NULL;
	sgid_tbl->ctx = NULL;
	sgid_tbl->vlan = NULL;
	sgid_tbl->max = 0;
	sgid_tbl->active = 0;
}

static int bnxt_qplib_alloc_sgid_tbl(struct bnxt_qplib_res *res,
				     struct bnxt_qplib_sgid_tbl *sgid_tbl,
				     u16 max)
{
	sgid_tbl->tbl = kcalloc(max, sizeof(*sgid_tbl->tbl), GFP_KERNEL);
	if (!sgid_tbl->tbl)
		return -ENOMEM;

	sgid_tbl->hw_id = kcalloc(max, sizeof(u16), GFP_KERNEL);
	if (!sgid_tbl->hw_id)
		goto out_free1;

	sgid_tbl->ctx = kcalloc(max, sizeof(void *), GFP_KERNEL);
	if (!sgid_tbl->ctx)
		goto out_free2;

	sgid_tbl->vlan = kcalloc(max, sizeof(u8), GFP_KERNEL);
	if (!sgid_tbl->vlan)
		goto out_free3;

	sgid_tbl->max = max;
	return 0;
out_free3:
	kfree(sgid_tbl->ctx);
	sgid_tbl->ctx = NULL;
out_free2:
	kfree(sgid_tbl->hw_id);
	sgid_tbl->hw_id = NULL;
out_free1:
	kfree(sgid_tbl->tbl);
	sgid_tbl->tbl = NULL;
	return -ENOMEM;
};

static void bnxt_qplib_cleanup_sgid_tbl(struct bnxt_qplib_res *res,
					struct bnxt_qplib_sgid_tbl *sgid_tbl)
{
	int i;

	for (i = 0; i < sgid_tbl->max; i++) {
		if (memcmp(&sgid_tbl->tbl[i], &bnxt_qplib_gid_zero,
			   sizeof(bnxt_qplib_gid_zero)))
			bnxt_qplib_del_sgid(sgid_tbl, &sgid_tbl->tbl[i].gid,
					    sgid_tbl->tbl[i].vlan_id, true);
	}
	memset(sgid_tbl->tbl, 0, sizeof(*sgid_tbl->tbl) * sgid_tbl->max);
	memset(sgid_tbl->hw_id, -1, sizeof(u16) * sgid_tbl->max);
	memset(sgid_tbl->vlan, 0, sizeof(u8) * sgid_tbl->max);
	sgid_tbl->active = 0;
}

static void bnxt_qplib_init_sgid_tbl(struct bnxt_qplib_sgid_tbl *sgid_tbl,
				     struct net_device *netdev)
{
	u32 i;

	for (i = 0; i < sgid_tbl->max; i++)
		sgid_tbl->tbl[i].vlan_id = 0xffff;

	memset(sgid_tbl->hw_id, -1, sizeof(u16) * sgid_tbl->max);
}

/* PDs */
int bnxt_qplib_alloc_pd(struct bnxt_qplib_res  *res, struct bnxt_qplib_pd *pd)
{
	struct bnxt_qplib_pd_tbl *pdt = &res->pd_tbl;
	u32 bit_num;
	int rc = 0;

	mutex_lock(&res->pd_tbl_lock);
	bit_num = find_first_bit(pdt->tbl, pdt->max);
	if (bit_num == pdt->max) {
		rc = -ENOMEM;
		goto exit;
	}

	/* Found unused PD */
	clear_bit(bit_num, pdt->tbl);
	pd->id = bit_num;
exit:
	mutex_unlock(&res->pd_tbl_lock);
	return rc;
}

int bnxt_qplib_dealloc_pd(struct bnxt_qplib_res *res,
			  struct bnxt_qplib_pd_tbl *pdt,
			  struct bnxt_qplib_pd *pd)
{
	int rc = 0;

	mutex_lock(&res->pd_tbl_lock);
	if (test_and_set_bit(pd->id, pdt->tbl)) {
		dev_warn(&res->pdev->dev, "Freeing an unused PD? pdn = %d\n",
			 pd->id);
		rc = -EINVAL;
		goto exit;
	}
	pd->id = 0;
exit:
	mutex_unlock(&res->pd_tbl_lock);
	return rc;
}

static void bnxt_qplib_free_pd_tbl(struct bnxt_qplib_pd_tbl *pdt)
{
	kfree(pdt->tbl);
	pdt->tbl = NULL;
	pdt->max = 0;
}

static int bnxt_qplib_alloc_pd_tbl(struct bnxt_qplib_res *res,
				   struct bnxt_qplib_pd_tbl *pdt,
				   u32 max)
{
	u32 bytes;

	bytes = max >> 3;
	if (!bytes)
		bytes = 1;
	pdt->tbl = kmalloc(bytes, GFP_KERNEL);
	if (!pdt->tbl)
		return -ENOMEM;

	pdt->max = max;
	memset((u8 *)pdt->tbl, 0xFF, bytes);
	mutex_init(&res->pd_tbl_lock);

	return 0;
}

/* DPIs */
int bnxt_qplib_alloc_dpi(struct bnxt_qplib_res *res,
			 struct bnxt_qplib_dpi *dpi,
			 void *app, u8 type)
{
	struct bnxt_qplib_dpi_tbl *dpit = &res->dpi_tbl;
	struct bnxt_qplib_reg_desc *reg;
	u32 bit_num;
	u64 umaddr;

	reg = &dpit->wcreg;
	mutex_lock(&res->dpi_tbl_lock);

	bit_num = find_first_bit(dpit->tbl, dpit->max);
	if (bit_num == dpit->max) {
		mutex_unlock(&res->dpi_tbl_lock);
		return -ENOMEM;
	}

	/* Found unused DPI */
	clear_bit(bit_num, dpit->tbl);
	dpit->app_tbl[bit_num] = app;

	dpi->bit = bit_num;
	dpi->dpi = bit_num + (reg->offset - dpit->ucreg.offset) / PAGE_SIZE;

	umaddr = reg->bar_base + reg->offset + bit_num * PAGE_SIZE;
	dpi->umdbr = umaddr;

	switch (type) {
	case BNXT_QPLIB_DPI_TYPE_KERNEL:
		/* privileged dbr was already mapped just initialize it. */
		dpi->umdbr = dpit->ucreg.bar_base +
			     dpit->ucreg.offset + bit_num * PAGE_SIZE;
		dpi->dbr = dpit->priv_db;
		dpi->dpi = dpi->bit;
		break;
	case BNXT_QPLIB_DPI_TYPE_WC:
		dpi->dbr = ioremap_wc(umaddr, PAGE_SIZE);
		break;
	default:
		dpi->dbr = ioremap(umaddr, PAGE_SIZE);
		break;
	}

	dpi->type = type;
	mutex_unlock(&res->dpi_tbl_lock);
	return 0;

}

int bnxt_qplib_dealloc_dpi(struct bnxt_qplib_res *res,
			   struct bnxt_qplib_dpi *dpi)
{
	struct bnxt_qplib_dpi_tbl *dpit = &res->dpi_tbl;

	mutex_lock(&res->dpi_tbl_lock);
	if (dpi->dpi && dpi->type != BNXT_QPLIB_DPI_TYPE_KERNEL)
		pci_iounmap(res->pdev, dpi->dbr);

	if (test_and_set_bit(dpi->bit, dpit->tbl)) {
		dev_warn(&res->pdev->dev,
			 "Freeing an unused DPI? dpi = %d, bit = %d\n",
				dpi->dpi, dpi->bit);
		mutex_unlock(&res->dpi_tbl_lock);
		return -EINVAL;
	}
	if (dpit->app_tbl)
		dpit->app_tbl[dpi->bit] = NULL;
	memset(dpi, 0, sizeof(*dpi));
	mutex_unlock(&res->dpi_tbl_lock);
	return 0;
}

static void bnxt_qplib_free_dpi_tbl(struct bnxt_qplib_res     *res,
				    struct bnxt_qplib_dpi_tbl *dpit)
{
	kfree(dpit->tbl);
	kfree(dpit->app_tbl);
	dpit->tbl = NULL;
	dpit->app_tbl = NULL;
	dpit->max = 0;
}

static int bnxt_qplib_alloc_dpi_tbl(struct bnxt_qplib_res *res,
				    struct bnxt_qplib_dev_attr *dev_attr)
{
	struct bnxt_qplib_dpi_tbl *dpit;
	struct bnxt_qplib_reg_desc *reg;
	unsigned long bar_len;
	u32 dbr_offset;
	u32 bytes;

	dpit = &res->dpi_tbl;
	reg = &dpit->wcreg;

	if (!bnxt_qplib_is_chip_gen_p5(res->cctx)) {
		/* Offest should come from L2 driver */
		dbr_offset = dev_attr->l2_db_size;
		dpit->ucreg.offset = dbr_offset;
		dpit->wcreg.offset = dbr_offset;
	}

	bar_len = pci_resource_len(res->pdev, reg->bar_id);
	dpit->max = (bar_len - reg->offset) / PAGE_SIZE;
	if (dev_attr->max_dpi)
		dpit->max = min_t(u32, dpit->max, dev_attr->max_dpi);

	dpit->app_tbl = kcalloc(dpit->max,  sizeof(void *), GFP_KERNEL);
	if (!dpit->app_tbl)
		return -ENOMEM;

	bytes = dpit->max >> 3;
	if (!bytes)
		bytes = 1;

	dpit->tbl = kmalloc(bytes, GFP_KERNEL);
	if (!dpit->tbl) {
		kfree(dpit->app_tbl);
		dpit->app_tbl = NULL;
		return -ENOMEM;
	}

	memset((u8 *)dpit->tbl, 0xFF, bytes);
	mutex_init(&res->dpi_tbl_lock);
	dpit->priv_db = dpit->ucreg.bar_reg + dpit->ucreg.offset;

	return 0;

}

/* Stats */
static void bnxt_qplib_free_stats_ctx(struct pci_dev *pdev,
				      struct bnxt_qplib_stats *stats)
{
	if (stats->dma) {
		dma_free_coherent(&pdev->dev, stats->size,
				  stats->dma, stats->dma_map);
	}
	memset(stats, 0, sizeof(*stats));
	stats->fw_id = -1;
}

static int bnxt_qplib_alloc_stats_ctx(struct pci_dev *pdev,
				      struct bnxt_qplib_chip_ctx *cctx,
				      struct bnxt_qplib_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->fw_id = -1;
	stats->size = cctx->hw_stats_size;
	stats->dma = dma_alloc_coherent(&pdev->dev, stats->size,
					&stats->dma_map, GFP_KERNEL);
	if (!stats->dma) {
		dev_err(&pdev->dev, "Stats DMA allocation failed\n");
		return -ENOMEM;
	}
	return 0;
}

void bnxt_qplib_cleanup_res(struct bnxt_qplib_res *res)
{
	bnxt_qplib_cleanup_sgid_tbl(res, &res->sgid_tbl);
}

int bnxt_qplib_init_res(struct bnxt_qplib_res *res)
{
	bnxt_qplib_init_sgid_tbl(&res->sgid_tbl, res->netdev);

	return 0;
}

void bnxt_qplib_free_res(struct bnxt_qplib_res *res)
{
	bnxt_qplib_free_sgid_tbl(res, &res->sgid_tbl);
	bnxt_qplib_free_pd_tbl(&res->pd_tbl);
	bnxt_qplib_free_dpi_tbl(res, &res->dpi_tbl);
}

int bnxt_qplib_alloc_res(struct bnxt_qplib_res *res, struct pci_dev *pdev,
			 struct net_device *netdev,
			 struct bnxt_qplib_dev_attr *dev_attr)
{
	int rc;

	res->pdev = pdev;
	res->netdev = netdev;

	rc = bnxt_qplib_alloc_sgid_tbl(res, &res->sgid_tbl, dev_attr->max_sgid);
	if (rc)
		goto fail;

	rc = bnxt_qplib_alloc_pd_tbl(res, &res->pd_tbl, dev_attr->max_pd);
	if (rc)
		goto fail;

	rc = bnxt_qplib_alloc_dpi_tbl(res, dev_attr);
	if (rc)
		goto fail;

	return 0;
fail:
	bnxt_qplib_free_res(res);
	return rc;
}

void bnxt_qplib_unmap_db_bar(struct bnxt_qplib_res *res)
{
	struct bnxt_qplib_reg_desc *reg;

	reg = &res->dpi_tbl.ucreg;
	if (reg->bar_reg)
		pci_iounmap(res->pdev, reg->bar_reg);
	reg->bar_reg = NULL;
	reg->bar_base = 0;
	reg->len = 0;
	reg->bar_id = 0;
}

int bnxt_qplib_map_db_bar(struct bnxt_qplib_res *res)
{
	struct bnxt_qplib_reg_desc *ucreg;
	struct bnxt_qplib_reg_desc *wcreg;

	wcreg = &res->dpi_tbl.wcreg;
	wcreg->bar_id = RCFW_DBR_PCI_BAR_REGION;
	wcreg->bar_base = pci_resource_start(res->pdev, wcreg->bar_id);

	ucreg = &res->dpi_tbl.ucreg;
	ucreg->bar_id = RCFW_DBR_PCI_BAR_REGION;
	ucreg->bar_base = pci_resource_start(res->pdev, ucreg->bar_id);
	ucreg->len = ucreg->offset + PAGE_SIZE;
	if (!ucreg->len || ((ucreg->len & (PAGE_SIZE - 1)) != 0)) {
		dev_err(&res->pdev->dev, "QPLIB: invalid dbr length %d",
			(int)ucreg->len);
		return -EINVAL;
	}
	ucreg->bar_reg = ioremap(ucreg->bar_base, ucreg->len);
	if (!ucreg->bar_reg) {
		dev_err(&res->pdev->dev, "privileged dpi map failed!");
		return -ENOMEM;
	}

	return 0;
}

int bnxt_qplib_determine_atomics(struct pci_dev *dev)
{
	int comp;
	u16 ctl2;

	comp = pci_enable_atomic_ops_to_root(dev,
					     PCI_EXP_DEVCAP2_ATOMIC_COMP32);
	if (comp)
		return -EOPNOTSUPP;
	comp = pci_enable_atomic_ops_to_root(dev,
					     PCI_EXP_DEVCAP2_ATOMIC_COMP64);
	if (comp)
		return -EOPNOTSUPP;
	pcie_capability_read_word(dev, PCI_EXP_DEVCTL2, &ctl2);
	return !(ctl2 & PCI_EXP_DEVCTL2_ATOMIC_REQ);
}
