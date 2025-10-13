// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/crash_dump.h>
#include <linux/bnxt/hsi.h>

#include "bnge.h"
#include "bnge_hwrm_lib.h"
#include "bnge_rmem.h"

static void bnge_init_ctx_mem(struct bnge_ctx_mem_type *ctxm,
			      void *p, int len)
{
	u8 init_val = ctxm->init_value;
	u16 offset = ctxm->init_offset;
	u8 *p2 = p;
	int i;

	if (!init_val)
		return;
	if (offset == BNGE_CTX_INIT_INVALID_OFFSET) {
		memset(p, init_val, len);
		return;
	}
	for (i = 0; i < len; i += ctxm->entry_size)
		*(p2 + i + offset) = init_val;
}

void bnge_free_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bd->pdev;
	int i;

	if (!rmem->pg_arr)
		goto skip_pages;

	for (i = 0; i < rmem->nr_pages; i++) {
		if (!rmem->pg_arr[i])
			continue;

		dma_free_coherent(&pdev->dev, rmem->page_size,
				  rmem->pg_arr[i], rmem->dma_arr[i]);

		rmem->pg_arr[i] = NULL;
	}
skip_pages:
	if (rmem->pg_tbl) {
		size_t pg_tbl_size = rmem->nr_pages * 8;

		if (rmem->flags & BNGE_RMEM_USE_FULL_PAGE_FLAG)
			pg_tbl_size = rmem->page_size;
		dma_free_coherent(&pdev->dev, pg_tbl_size,
				  rmem->pg_tbl, rmem->dma_pg_tbl);
		rmem->pg_tbl = NULL;
	}
	if (rmem->vmem_size && *rmem->vmem) {
		vfree(*rmem->vmem);
		*rmem->vmem = NULL;
	}
}

int bnge_alloc_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bd->pdev;
	u64 valid_bit = 0;
	int i;

	if (rmem->flags & (BNGE_RMEM_VALID_PTE_FLAG | BNGE_RMEM_RING_PTE_FLAG))
		valid_bit = PTU_PTE_VALID;

	if ((rmem->nr_pages > 1 || rmem->depth > 0) && !rmem->pg_tbl) {
		size_t pg_tbl_size = rmem->nr_pages * 8;

		if (rmem->flags & BNGE_RMEM_USE_FULL_PAGE_FLAG)
			pg_tbl_size = rmem->page_size;
		rmem->pg_tbl = dma_alloc_coherent(&pdev->dev, pg_tbl_size,
						  &rmem->dma_pg_tbl,
						  GFP_KERNEL);
		if (!rmem->pg_tbl)
			return -ENOMEM;
	}

	for (i = 0; i < rmem->nr_pages; i++) {
		u64 extra_bits = valid_bit;

		rmem->pg_arr[i] = dma_alloc_coherent(&pdev->dev,
						     rmem->page_size,
						     &rmem->dma_arr[i],
						     GFP_KERNEL);
		if (!rmem->pg_arr[i])
			goto err_free_ring;

		if (rmem->ctx_mem)
			bnge_init_ctx_mem(rmem->ctx_mem, rmem->pg_arr[i],
					  rmem->page_size);

		if (rmem->nr_pages > 1 || rmem->depth > 0) {
			if (i == rmem->nr_pages - 2 &&
			    (rmem->flags & BNGE_RMEM_RING_PTE_FLAG))
				extra_bits |= PTU_PTE_NEXT_TO_LAST;
			else if (i == rmem->nr_pages - 1 &&
				 (rmem->flags & BNGE_RMEM_RING_PTE_FLAG))
				extra_bits |= PTU_PTE_LAST;
			rmem->pg_tbl[i] =
				cpu_to_le64(rmem->dma_arr[i] | extra_bits);
		}
	}

	if (rmem->vmem_size) {
		*rmem->vmem = vzalloc(rmem->vmem_size);
		if (!(*rmem->vmem))
			goto err_free_ring;
	}
	return 0;

err_free_ring:
	bnge_free_ring(bd, rmem);
	return -ENOMEM;
}

static int bnge_alloc_ctx_one_lvl(struct bnge_dev *bd,
				  struct bnge_ctx_pg_info *ctx_pg)
{
	struct bnge_ring_mem_info *rmem = &ctx_pg->ring_mem;

	rmem->page_size = BNGE_PAGE_SIZE;
	rmem->pg_arr = ctx_pg->ctx_pg_arr;
	rmem->dma_arr = ctx_pg->ctx_dma_arr;
	rmem->flags = BNGE_RMEM_VALID_PTE_FLAG;
	if (rmem->depth >= 1)
		rmem->flags |= BNGE_RMEM_USE_FULL_PAGE_FLAG;
	return bnge_alloc_ring(bd, rmem);
}

static int bnge_alloc_ctx_pg_tbls(struct bnge_dev *bd,
				  struct bnge_ctx_pg_info *ctx_pg, u32 mem_size,
				  u8 depth, struct bnge_ctx_mem_type *ctxm)
{
	struct bnge_ring_mem_info *rmem = &ctx_pg->ring_mem;
	int rc;

	if (!mem_size)
		return -EINVAL;

	ctx_pg->nr_pages = DIV_ROUND_UP(mem_size, BNGE_PAGE_SIZE);
	if (ctx_pg->nr_pages > MAX_CTX_TOTAL_PAGES) {
		ctx_pg->nr_pages = 0;
		return -EINVAL;
	}
	if (ctx_pg->nr_pages > MAX_CTX_PAGES || depth > 1) {
		int nr_tbls, i;

		rmem->depth = 2;
		ctx_pg->ctx_pg_tbl = kcalloc(MAX_CTX_PAGES, sizeof(ctx_pg),
					     GFP_KERNEL);
		if (!ctx_pg->ctx_pg_tbl)
			return -ENOMEM;
		nr_tbls = DIV_ROUND_UP(ctx_pg->nr_pages, MAX_CTX_PAGES);
		rmem->nr_pages = nr_tbls;
		rc = bnge_alloc_ctx_one_lvl(bd, ctx_pg);
		if (rc)
			return rc;
		for (i = 0; i < nr_tbls; i++) {
			struct bnge_ctx_pg_info *pg_tbl;

			pg_tbl = kzalloc(sizeof(*pg_tbl), GFP_KERNEL);
			if (!pg_tbl)
				return -ENOMEM;
			ctx_pg->ctx_pg_tbl[i] = pg_tbl;
			rmem = &pg_tbl->ring_mem;
			rmem->pg_tbl = ctx_pg->ctx_pg_arr[i];
			rmem->dma_pg_tbl = ctx_pg->ctx_dma_arr[i];
			rmem->depth = 1;
			rmem->nr_pages = MAX_CTX_PAGES;
			rmem->ctx_mem = ctxm;
			if (i == (nr_tbls - 1)) {
				int rem = ctx_pg->nr_pages % MAX_CTX_PAGES;

				if (rem)
					rmem->nr_pages = rem;
			}
			rc = bnge_alloc_ctx_one_lvl(bd, pg_tbl);
			if (rc)
				break;
		}
	} else {
		rmem->nr_pages = DIV_ROUND_UP(mem_size, BNGE_PAGE_SIZE);
		if (rmem->nr_pages > 1 || depth)
			rmem->depth = 1;
		rmem->ctx_mem = ctxm;
		rc = bnge_alloc_ctx_one_lvl(bd, ctx_pg);
	}

	return rc;
}

static void bnge_free_ctx_pg_tbls(struct bnge_dev *bd,
				  struct bnge_ctx_pg_info *ctx_pg)
{
	struct bnge_ring_mem_info *rmem = &ctx_pg->ring_mem;

	if (rmem->depth > 1 || ctx_pg->nr_pages > MAX_CTX_PAGES ||
	    ctx_pg->ctx_pg_tbl) {
		int i, nr_tbls = rmem->nr_pages;

		for (i = 0; i < nr_tbls; i++) {
			struct bnge_ctx_pg_info *pg_tbl;
			struct bnge_ring_mem_info *rmem2;

			pg_tbl = ctx_pg->ctx_pg_tbl[i];
			if (!pg_tbl)
				continue;
			rmem2 = &pg_tbl->ring_mem;
			bnge_free_ring(bd, rmem2);
			ctx_pg->ctx_pg_arr[i] = NULL;
			kfree(pg_tbl);
			ctx_pg->ctx_pg_tbl[i] = NULL;
		}
		kfree(ctx_pg->ctx_pg_tbl);
		ctx_pg->ctx_pg_tbl = NULL;
	}
	bnge_free_ring(bd, rmem);
	ctx_pg->nr_pages = 0;
}

static int bnge_setup_ctxm_pg_tbls(struct bnge_dev *bd,
				   struct bnge_ctx_mem_type *ctxm, u32 entries,
				   u8 pg_lvl)
{
	struct bnge_ctx_pg_info *ctx_pg = ctxm->pg_info;
	int i, rc = 0, n = 1;
	u32 mem_size;

	if (!ctxm->entry_size || !ctx_pg)
		return -EINVAL;
	if (ctxm->instance_bmap)
		n = hweight32(ctxm->instance_bmap);
	if (ctxm->entry_multiple)
		entries = roundup(entries, ctxm->entry_multiple);
	entries = clamp_t(u32, entries, ctxm->min_entries, ctxm->max_entries);
	mem_size = entries * ctxm->entry_size;
	for (i = 0; i < n && !rc; i++) {
		ctx_pg[i].entries = entries;
		rc = bnge_alloc_ctx_pg_tbls(bd, &ctx_pg[i], mem_size, pg_lvl,
					    ctxm->init_value ? ctxm : NULL);
	}

	return rc;
}

static int bnge_backing_store_cfg(struct bnge_dev *bd, u32 ena)
{
	struct bnge_ctx_mem_info *ctx = bd->ctx;
	struct bnge_ctx_mem_type *ctxm;
	u16 last_type;
	int rc = 0;
	u16 type;

	if (!ena)
		return 0;
	else if (ena & FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM)
		last_type = BNGE_CTX_MAX - 1;
	else
		last_type = BNGE_CTX_L2_MAX - 1;
	ctx->ctx_arr[last_type].last = 1;

	for (type = 0 ; type < BNGE_CTX_V2_MAX; type++) {
		ctxm = &ctx->ctx_arr[type];

		rc = bnge_hwrm_func_backing_store(bd, ctxm, ctxm->last);
		if (rc)
			return rc;
	}

	return 0;
}

void bnge_free_ctx_mem(struct bnge_dev *bd)
{
	struct bnge_ctx_mem_info *ctx = bd->ctx;
	u16 type;

	if (!ctx)
		return;

	for (type = 0; type < BNGE_CTX_V2_MAX; type++) {
		struct bnge_ctx_mem_type *ctxm = &ctx->ctx_arr[type];
		struct bnge_ctx_pg_info *ctx_pg = ctxm->pg_info;
		int i, n = 1;

		if (!ctx_pg)
			continue;
		if (ctxm->instance_bmap)
			n = hweight32(ctxm->instance_bmap);
		for (i = 0; i < n; i++)
			bnge_free_ctx_pg_tbls(bd, &ctx_pg[i]);

		kfree(ctx_pg);
		ctxm->pg_info = NULL;
	}

	ctx->flags &= ~BNGE_CTX_FLAG_INITED;
	kfree(ctx);
	bd->ctx = NULL;
}

#define FUNC_BACKING_STORE_CFG_REQ_DFLT_ENABLES			\
	(FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_SRQ |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_CQ |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_VNIC |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_STAT)

int bnge_alloc_ctx_mem(struct bnge_dev *bd)
{
	struct bnge_ctx_mem_type *ctxm;
	struct bnge_ctx_mem_info *ctx;
	u32 l2_qps, qp1_qps, max_qps;
	u32 ena, entries_sp, entries;
	u32 srqs, max_srqs, min;
	u32 num_mr, num_ah;
	u32 extra_srqs = 0;
	u32 extra_qps = 0;
	u32 fast_qpmd_qps;
	u8 pg_lvl = 1;
	int i, rc;

	rc = bnge_hwrm_func_backing_store_qcaps(bd);
	if (rc) {
		dev_err(bd->dev, "Failed querying ctx mem caps, rc: %d\n", rc);
		return rc;
	}

	ctx = bd->ctx;
	if (!ctx || (ctx->flags & BNGE_CTX_FLAG_INITED))
		return 0;

	ctxm = &ctx->ctx_arr[BNGE_CTX_QP];
	l2_qps = ctxm->qp_l2_entries;
	qp1_qps = ctxm->qp_qp1_entries;
	fast_qpmd_qps = ctxm->qp_fast_qpmd_entries;
	max_qps = ctxm->max_entries;
	ctxm = &ctx->ctx_arr[BNGE_CTX_SRQ];
	srqs = ctxm->srq_l2_entries;
	max_srqs = ctxm->max_entries;
	ena = 0;
	if (bnge_is_roce_en(bd) && !is_kdump_kernel()) {
		pg_lvl = 2;
		extra_qps = min_t(u32, 65536, max_qps - l2_qps - qp1_qps);
		/* allocate extra qps if fast qp destroy feature enabled */
		extra_qps += fast_qpmd_qps;
		extra_srqs = min_t(u32, 8192, max_srqs - srqs);
		if (fast_qpmd_qps)
			ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP_FAST_QPMD;
	}

	ctxm = &ctx->ctx_arr[BNGE_CTX_QP];
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, l2_qps + qp1_qps + extra_qps,
				     pg_lvl);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNGE_CTX_SRQ];
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, srqs + extra_srqs, pg_lvl);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNGE_CTX_CQ];
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, ctxm->cq_l2_entries +
				     extra_qps * 2, pg_lvl);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNGE_CTX_VNIC];
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, ctxm->max_entries, 1);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNGE_CTX_STAT];
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, ctxm->max_entries, 1);
	if (rc)
		return rc;

	if (!bnge_is_roce_en(bd))
		goto skip_rdma;

	ctxm = &ctx->ctx_arr[BNGE_CTX_MRAV];
	/* 128K extra is needed to accommodate static AH context
	 * allocation by f/w.
	 */
	num_mr = min_t(u32, ctxm->max_entries / 2, 1024 * 256);
	num_ah = min_t(u32, num_mr, 1024 * 128);
	ctxm->split_entry_cnt = BNGE_CTX_MRAV_AV_SPLIT_ENTRY + 1;
	if (!ctxm->mrav_av_entries || ctxm->mrav_av_entries > num_ah)
		ctxm->mrav_av_entries = num_ah;

	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, num_mr + num_ah, 2);
	if (rc)
		return rc;
	ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_MRAV;

	ctxm = &ctx->ctx_arr[BNGE_CTX_TIM];
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, l2_qps + qp1_qps + extra_qps, 1);
	if (rc)
		return rc;
	ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM;

skip_rdma:
	ctxm = &ctx->ctx_arr[BNGE_CTX_STQM];
	min = ctxm->min_entries;
	entries_sp = ctx->ctx_arr[BNGE_CTX_VNIC].vnic_entries + l2_qps +
		     2 * (extra_qps + qp1_qps) + min;
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, entries_sp, 2);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNGE_CTX_FTQM];
	entries = l2_qps + 2 * (extra_qps + qp1_qps);
	rc = bnge_setup_ctxm_pg_tbls(bd, ctxm, entries, 2);
	if (rc)
		return rc;
	for (i = 0; i < ctx->tqm_fp_rings_count + 1; i++)
		ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_TQM_SP << i;
	ena |= FUNC_BACKING_STORE_CFG_REQ_DFLT_ENABLES;

	rc = bnge_backing_store_cfg(bd, ena);
	if (rc) {
		dev_err(bd->dev, "Failed configuring ctx mem, rc: %d\n", rc);
		return rc;
	}
	ctx->flags |= BNGE_CTX_FLAG_INITED;

	return 0;
}

void bnge_init_ring_struct(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, j;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_ring_mem_info *rmem;
		struct bnge_nq_ring_info *nqr;
		struct bnge_rx_ring_info *rxr;
		struct bnge_tx_ring_info *txr;
		struct bnge_ring_struct *ring;

		nqr = &bnapi->nq_ring;
		ring = &nqr->ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bn->cp_nr_pages;
		rmem->page_size = HW_CMPD_RING_SIZE;
		rmem->pg_arr = (void **)nqr->desc_ring;
		rmem->dma_arr = nqr->desc_mapping;
		rmem->vmem_size = 0;

		rxr = bnapi->rx_ring;
		if (!rxr)
			goto skip_rx;

		ring = &rxr->rx_ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bn->rx_nr_pages;
		rmem->page_size = HW_RXBD_RING_SIZE;
		rmem->pg_arr = (void **)rxr->rx_desc_ring;
		rmem->dma_arr = rxr->rx_desc_mapping;
		rmem->vmem_size = SW_RXBD_RING_SIZE * bn->rx_nr_pages;
		rmem->vmem = (void **)&rxr->rx_buf_ring;

		ring = &rxr->rx_agg_ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bn->rx_agg_nr_pages;
		rmem->page_size = HW_RXBD_RING_SIZE;
		rmem->pg_arr = (void **)rxr->rx_agg_desc_ring;
		rmem->dma_arr = rxr->rx_agg_desc_mapping;
		rmem->vmem_size = SW_RXBD_AGG_RING_SIZE * bn->rx_agg_nr_pages;
		rmem->vmem = (void **)&rxr->rx_agg_buf_ring;

skip_rx:
		bnge_for_each_napi_tx(j, bnapi, txr) {
			ring = &txr->tx_ring_struct;
			rmem = &ring->ring_mem;
			rmem->nr_pages = bn->tx_nr_pages;
			rmem->page_size = HW_TXBD_RING_SIZE;
			rmem->pg_arr = (void **)txr->tx_desc_ring;
			rmem->dma_arr = txr->tx_desc_mapping;
			rmem->vmem_size = SW_TXBD_RING_SIZE * bn->tx_nr_pages;
			rmem->vmem = (void **)&txr->tx_buf_ring;
		}
	}
}
