/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <linux/atomic.h>
#include <asm/byteorder.h>

#include "hinic_common.h"
#include "hinic_hw_if.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_qp_ctxt.h"
#include "hinic_hw_qp.h"

#define SQ_DB_OFF               SZ_2K

/* The number of cache line to prefetch Until threshold state */
#define WQ_PREFETCH_MAX         2
/* The number of cache line to prefetch After threshold state */
#define WQ_PREFETCH_MIN         1
/* Threshold state */
#define WQ_PREFETCH_THRESHOLD   256

/* sizes of the SQ/RQ ctxt */
#define Q_CTXT_SIZE             48
#define CTXT_RSVD               240

#define SQ_CTXT_OFFSET(max_sqs, max_rqs, q_id)  \
		(((max_rqs) + (max_sqs)) * CTXT_RSVD + (q_id) * Q_CTXT_SIZE)

#define RQ_CTXT_OFFSET(max_sqs, max_rqs, q_id)  \
		(((max_rqs) + (max_sqs)) * CTXT_RSVD + \
		 (max_sqs + (q_id)) * Q_CTXT_SIZE)

#define SIZE_16BYTES(size)      (ALIGN(size, 16) >> 4)

void hinic_qp_prepare_header(struct hinic_qp_ctxt_header *qp_ctxt_hdr,
			     enum hinic_qp_ctxt_type ctxt_type,
			     u16 num_queues, u16 max_queues)
{
	u16 max_sqs = max_queues;
	u16 max_rqs = max_queues;

	qp_ctxt_hdr->num_queues = num_queues;
	qp_ctxt_hdr->queue_type = ctxt_type;

	if (ctxt_type == HINIC_QP_CTXT_TYPE_SQ)
		qp_ctxt_hdr->addr_offset = SQ_CTXT_OFFSET(max_sqs, max_rqs, 0);
	else
		qp_ctxt_hdr->addr_offset = RQ_CTXT_OFFSET(max_sqs, max_rqs, 0);

	qp_ctxt_hdr->addr_offset = SIZE_16BYTES(qp_ctxt_hdr->addr_offset);

	hinic_cpu_to_be32(qp_ctxt_hdr, sizeof(*qp_ctxt_hdr));
}

void hinic_sq_prepare_ctxt(struct hinic_sq_ctxt *sq_ctxt,
			   struct hinic_sq *sq, u16 global_qid)
{
	u32 wq_page_pfn_hi, wq_page_pfn_lo, wq_block_pfn_hi, wq_block_pfn_lo;
	u64 wq_page_addr, wq_page_pfn, wq_block_pfn;
	u16 pi_start, ci_start;
	struct hinic_wq *wq;

	wq = sq->wq;
	ci_start = atomic_read(&wq->cons_idx);
	pi_start = atomic_read(&wq->prod_idx);

	/* Read the first page paddr from the WQ page paddr ptrs */
	wq_page_addr = be64_to_cpu(*wq->block_vaddr);

	wq_page_pfn = HINIC_WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	wq_block_pfn = HINIC_WQ_BLOCK_PFN(wq->block_paddr);
	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	sq_ctxt->ceq_attr = HINIC_SQ_CTXT_CEQ_ATTR_SET(global_qid,
						       GLOBAL_SQ_ID) |
			    HINIC_SQ_CTXT_CEQ_ATTR_SET(0, EN);

	sq_ctxt->ci_wrapped = HINIC_SQ_CTXT_CI_SET(ci_start, IDX) |
			      HINIC_SQ_CTXT_CI_SET(1, WRAPPED);

	sq_ctxt->wq_hi_pfn_pi =
			HINIC_SQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
			HINIC_SQ_CTXT_WQ_PAGE_SET(pi_start, PI);

	sq_ctxt->wq_lo_pfn = wq_page_pfn_lo;

	sq_ctxt->pref_cache =
		HINIC_SQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
		HINIC_SQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
		HINIC_SQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);

	sq_ctxt->pref_wrapped = 1;

	sq_ctxt->pref_wq_hi_pfn_ci =
		HINIC_SQ_CTXT_PREF_SET(ci_start, CI) |
		HINIC_SQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_HI_PFN);

	sq_ctxt->pref_wq_lo_pfn = wq_page_pfn_lo;

	sq_ctxt->wq_block_hi_pfn =
		HINIC_SQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, HI_PFN);

	sq_ctxt->wq_block_lo_pfn = wq_block_pfn_lo;

	hinic_cpu_to_be32(sq_ctxt, sizeof(*sq_ctxt));
}

void hinic_rq_prepare_ctxt(struct hinic_rq_ctxt *rq_ctxt,
			   struct hinic_rq *rq, u16 global_qid)
{
	u32 wq_page_pfn_hi, wq_page_pfn_lo, wq_block_pfn_hi, wq_block_pfn_lo;
	u64 wq_page_addr, wq_page_pfn, wq_block_pfn;
	u16 pi_start, ci_start;
	struct hinic_wq *wq;

	wq = rq->wq;
	ci_start = atomic_read(&wq->cons_idx);
	pi_start = atomic_read(&wq->prod_idx);

	/* Read the first page paddr from the WQ page paddr ptrs */
	wq_page_addr = be64_to_cpu(*wq->block_vaddr);

	wq_page_pfn = HINIC_WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	wq_block_pfn = HINIC_WQ_BLOCK_PFN(wq->block_paddr);
	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	rq_ctxt->ceq_attr = HINIC_RQ_CTXT_CEQ_ATTR_SET(0, EN) |
			    HINIC_RQ_CTXT_CEQ_ATTR_SET(1, WRAPPED);

	rq_ctxt->pi_intr_attr = HINIC_RQ_CTXT_PI_SET(pi_start, IDX) |
				HINIC_RQ_CTXT_PI_SET(rq->msix_entry, INTR);

	rq_ctxt->wq_hi_pfn_ci = HINIC_RQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi,
							  HI_PFN) |
				HINIC_RQ_CTXT_WQ_PAGE_SET(ci_start, CI);

	rq_ctxt->wq_lo_pfn = wq_page_pfn_lo;

	rq_ctxt->pref_cache =
		HINIC_RQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
		HINIC_RQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
		HINIC_RQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);

	rq_ctxt->pref_wrapped = 1;

	rq_ctxt->pref_wq_hi_pfn_ci =
		HINIC_RQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_HI_PFN) |
		HINIC_RQ_CTXT_PREF_SET(ci_start, CI);

	rq_ctxt->pref_wq_lo_pfn = wq_page_pfn_lo;

	rq_ctxt->pi_paddr_hi = upper_32_bits(rq->pi_dma_addr);
	rq_ctxt->pi_paddr_lo = lower_32_bits(rq->pi_dma_addr);

	rq_ctxt->wq_block_hi_pfn =
		HINIC_RQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, HI_PFN);

	rq_ctxt->wq_block_lo_pfn = wq_block_pfn_lo;

	hinic_cpu_to_be32(rq_ctxt, sizeof(*rq_ctxt));
}

/**
 * alloc_sq_skb_arr - allocate sq array for saved skb
 * @sq: HW Send Queue
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_sq_skb_arr(struct hinic_sq *sq)
{
	struct hinic_wq *wq = sq->wq;
	size_t skb_arr_size;

	skb_arr_size = wq->q_depth * sizeof(*sq->saved_skb);
	sq->saved_skb = vzalloc(skb_arr_size);
	if (!sq->saved_skb)
		return -ENOMEM;

	return 0;
}

/**
 * free_sq_skb_arr - free sq array for saved skb
 * @sq: HW Send Queue
 **/
static void free_sq_skb_arr(struct hinic_sq *sq)
{
	vfree(sq->saved_skb);
}

/**
 * alloc_rq_skb_arr - allocate rq array for saved skb
 * @rq: HW Receive Queue
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_rq_skb_arr(struct hinic_rq *rq)
{
	struct hinic_wq *wq = rq->wq;
	size_t skb_arr_size;

	skb_arr_size = wq->q_depth * sizeof(*rq->saved_skb);
	rq->saved_skb = vzalloc(skb_arr_size);
	if (!rq->saved_skb)
		return -ENOMEM;

	return 0;
}

/**
 * free_rq_skb_arr - free rq array for saved skb
 * @rq: HW Receive Queue
 **/
static void free_rq_skb_arr(struct hinic_rq *rq)
{
	vfree(rq->saved_skb);
}

/**
 * hinic_init_sq - Initialize HW Send Queue
 * @sq: HW Send Queue
 * @hwif: HW Interface for accessing HW
 * @wq: Work Queue for the data of the SQ
 * @entry: msix entry for sq
 * @ci_addr: address for reading the current HW consumer index
 * @ci_dma_addr: dma address for reading the current HW consumer index
 * @db_base: doorbell base address
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_sq(struct hinic_sq *sq, struct hinic_hwif *hwif,
		  struct hinic_wq *wq, struct msix_entry *entry,
		  void *ci_addr, dma_addr_t ci_dma_addr,
		  void __iomem *db_base)
{
	sq->hwif = hwif;

	sq->wq = wq;

	sq->irq = entry->vector;
	sq->msix_entry = entry->entry;

	sq->hw_ci_addr = ci_addr;
	sq->hw_ci_dma_addr = ci_dma_addr;

	sq->db_base = db_base + SQ_DB_OFF;

	return alloc_sq_skb_arr(sq);
}

/**
 * hinic_clean_sq - Clean HW Send Queue's Resources
 * @sq: Send Queue
 **/
void hinic_clean_sq(struct hinic_sq *sq)
{
	free_sq_skb_arr(sq);
}

/**
 * alloc_rq_cqe - allocate rq completion queue elements
 * @rq: HW Receive Queue
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_rq_cqe(struct hinic_rq *rq)
{
	struct hinic_hwif *hwif = rq->hwif;
	struct pci_dev *pdev = hwif->pdev;
	size_t cqe_dma_size, cqe_size;
	struct hinic_wq *wq = rq->wq;
	int j, i;

	cqe_size = wq->q_depth * sizeof(*rq->cqe);
	rq->cqe = vzalloc(cqe_size);
	if (!rq->cqe)
		return -ENOMEM;

	cqe_dma_size = wq->q_depth * sizeof(*rq->cqe_dma);
	rq->cqe_dma = vzalloc(cqe_dma_size);
	if (!rq->cqe_dma)
		goto err_cqe_dma_arr_alloc;

	for (i = 0; i < wq->q_depth; i++) {
		rq->cqe[i] = dma_zalloc_coherent(&pdev->dev,
						 sizeof(*rq->cqe[i]),
						 &rq->cqe_dma[i], GFP_KERNEL);
		if (!rq->cqe[i])
			goto err_cqe_alloc;
	}

	return 0;

err_cqe_alloc:
	for (j = 0; j < i; j++)
		dma_free_coherent(&pdev->dev, sizeof(*rq->cqe[j]), rq->cqe[j],
				  rq->cqe_dma[j]);

	vfree(rq->cqe_dma);

err_cqe_dma_arr_alloc:
	vfree(rq->cqe);
	return -ENOMEM;
}

/**
 * free_rq_cqe - free rq completion queue elements
 * @rq: HW Receive Queue
 **/
static void free_rq_cqe(struct hinic_rq *rq)
{
	struct hinic_hwif *hwif = rq->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_wq *wq = rq->wq;
	int i;

	for (i = 0; i < wq->q_depth; i++)
		dma_free_coherent(&pdev->dev, sizeof(*rq->cqe[i]), rq->cqe[i],
				  rq->cqe_dma[i]);

	vfree(rq->cqe_dma);
	vfree(rq->cqe);
}

/**
 * hinic_init_rq - Initialize HW Receive Queue
 * @rq: HW Receive Queue
 * @hwif: HW Interface for accessing HW
 * @wq: Work Queue for the data of the RQ
 * @entry: msix entry for rq
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_rq(struct hinic_rq *rq, struct hinic_hwif *hwif,
		  struct hinic_wq *wq, struct msix_entry *entry)
{
	struct pci_dev *pdev = hwif->pdev;
	size_t pi_size;
	int err;

	rq->hwif = hwif;

	rq->wq = wq;

	rq->irq = entry->vector;
	rq->msix_entry = entry->entry;

	rq->buf_sz = HINIC_RX_BUF_SZ;

	err = alloc_rq_skb_arr(rq);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate rq priv data\n");
		return err;
	}

	err = alloc_rq_cqe(rq);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate rq cqe\n");
		goto err_alloc_rq_cqe;
	}

	/* HW requirements: Must be at least 32 bit */
	pi_size = ALIGN(sizeof(*rq->pi_virt_addr), sizeof(u32));
	rq->pi_virt_addr = dma_zalloc_coherent(&pdev->dev, pi_size,
					       &rq->pi_dma_addr, GFP_KERNEL);
	if (!rq->pi_virt_addr) {
		dev_err(&pdev->dev, "Failed to allocate PI address\n");
		err = -ENOMEM;
		goto err_pi_virt;
	}

	return 0;

err_pi_virt:
	free_rq_cqe(rq);

err_alloc_rq_cqe:
	free_rq_skb_arr(rq);
	return err;
}

/**
 * hinic_clean_rq - Clean HW Receive Queue's Resources
 * @rq: HW Receive Queue
 **/
void hinic_clean_rq(struct hinic_rq *rq)
{
	struct hinic_hwif *hwif = rq->hwif;
	struct pci_dev *pdev = hwif->pdev;
	size_t pi_size;

	pi_size = ALIGN(sizeof(*rq->pi_virt_addr), sizeof(u32));
	dma_free_coherent(&pdev->dev, pi_size, rq->pi_virt_addr,
			  rq->pi_dma_addr);

	free_rq_cqe(rq);
	free_rq_skb_arr(rq);
}
