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
#include <linux/skbuff.h>
#include <linux/io.h>
#include <asm/barrier.h>
#include <asm/byteorder.h>

#include "hinic_common.h"
#include "hinic_hw_if.h"
#include "hinic_hw_wqe.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_qp_ctxt.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_io.h"

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

#define SIZE_16BYTES(size)              (ALIGN(size, 16) >> 4)
#define SIZE_8BYTES(size)               (ALIGN(size, 8) >> 3)
#define SECT_SIZE_FROM_8BYTES(size)     ((size) << 3)

#define SQ_DB_PI_HI_SHIFT       8
#define SQ_DB_PI_HI(prod_idx)   ((prod_idx) >> SQ_DB_PI_HI_SHIFT)

#define SQ_DB_PI_LOW_MASK       0xFF
#define SQ_DB_PI_LOW(prod_idx)  ((prod_idx) & SQ_DB_PI_LOW_MASK)

#define SQ_DB_ADDR(sq, pi)      ((u64 *)((sq)->db_base) + SQ_DB_PI_LOW(pi))

#define SQ_MASKED_IDX(sq, idx)  ((idx) & (sq)->wq->mask)
#define RQ_MASKED_IDX(rq, idx)  ((idx) & (rq)->wq->mask)

enum sq_wqe_type {
	SQ_NORMAL_WQE = 0,
};

enum rq_completion_fmt {
	RQ_COMPLETE_SGE = 1
};

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
		rq->cqe[i] = dma_alloc_coherent(&pdev->dev,
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
	rq->pi_virt_addr = dma_alloc_coherent(&pdev->dev, pi_size,
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

/**
 * hinic_get_sq_free_wqebbs - return number of free wqebbs for use
 * @sq: send queue
 *
 * Return number of free wqebbs
 **/
int hinic_get_sq_free_wqebbs(struct hinic_sq *sq)
{
	struct hinic_wq *wq = sq->wq;

	return atomic_read(&wq->delta) - 1;
}

/**
 * hinic_get_rq_free_wqebbs - return number of free wqebbs for use
 * @rq: recv queue
 *
 * Return number of free wqebbs
 **/
int hinic_get_rq_free_wqebbs(struct hinic_rq *rq)
{
	struct hinic_wq *wq = rq->wq;

	return atomic_read(&wq->delta) - 1;
}

static void sq_prepare_ctrl(struct hinic_sq_ctrl *ctrl, u16 prod_idx,
			    int nr_descs)
{
	u32 ctrl_size, task_size, bufdesc_size;

	ctrl_size = SIZE_8BYTES(sizeof(struct hinic_sq_ctrl));
	task_size = SIZE_8BYTES(sizeof(struct hinic_sq_task));
	bufdesc_size = nr_descs * sizeof(struct hinic_sq_bufdesc);
	bufdesc_size = SIZE_8BYTES(bufdesc_size);

	ctrl->ctrl_info = HINIC_SQ_CTRL_SET(bufdesc_size, BUFDESC_SECT_LEN) |
			  HINIC_SQ_CTRL_SET(task_size, TASKSECT_LEN)        |
			  HINIC_SQ_CTRL_SET(SQ_NORMAL_WQE, DATA_FORMAT)     |
			  HINIC_SQ_CTRL_SET(ctrl_size, LEN);

	ctrl->queue_info = HINIC_SQ_CTRL_SET(HINIC_MSS_DEFAULT,
					     QUEUE_INFO_MSS) |
			   HINIC_SQ_CTRL_SET(1, QUEUE_INFO_UC);
}

static void sq_prepare_task(struct hinic_sq_task *task)
{
	task->pkt_info0 = 0;
	task->pkt_info1 = 0;
	task->pkt_info2 = 0;

	task->ufo_v6_identify = 0;

	task->pkt_info4 = HINIC_SQ_TASK_INFO4_SET(HINIC_L2TYPE_ETH, L2TYPE);

	task->zero_pad = 0;
}

void hinic_task_set_l2hdr(struct hinic_sq_task *task, u32 len)
{
	task->pkt_info0 |= HINIC_SQ_TASK_INFO0_SET(len, L2HDR_LEN);
}

void hinic_task_set_outter_l3(struct hinic_sq_task *task,
			      enum hinic_l3_offload_type l3_type,
			      u32 network_len)
{
	task->pkt_info2 |= HINIC_SQ_TASK_INFO2_SET(l3_type, OUTER_L3TYPE) |
			   HINIC_SQ_TASK_INFO2_SET(network_len, OUTER_L3LEN);
}

void hinic_task_set_inner_l3(struct hinic_sq_task *task,
			     enum hinic_l3_offload_type l3_type,
			     u32 network_len)
{
	task->pkt_info0 |= HINIC_SQ_TASK_INFO0_SET(l3_type, INNER_L3TYPE);
	task->pkt_info1 |= HINIC_SQ_TASK_INFO1_SET(network_len, INNER_L3LEN);
}

void hinic_task_set_tunnel_l4(struct hinic_sq_task *task,
			      enum hinic_l4_tunnel_type l4_type,
			      u32 tunnel_len)
{
	task->pkt_info2 |= HINIC_SQ_TASK_INFO2_SET(l4_type, TUNNEL_L4TYPE) |
			   HINIC_SQ_TASK_INFO2_SET(tunnel_len, TUNNEL_L4LEN);
}

void hinic_set_cs_inner_l4(struct hinic_sq_task *task, u32 *queue_info,
			   enum hinic_l4_offload_type l4_offload,
			   u32 l4_len, u32 offset)
{
	u32 tcp_udp_cs = 0, sctp = 0;
	u32 mss = HINIC_MSS_DEFAULT;

	if (l4_offload == TCP_OFFLOAD_ENABLE ||
	    l4_offload == UDP_OFFLOAD_ENABLE)
		tcp_udp_cs = 1;
	else if (l4_offload == SCTP_OFFLOAD_ENABLE)
		sctp = 1;

	task->pkt_info0 |= HINIC_SQ_TASK_INFO0_SET(l4_offload, L4_OFFLOAD);
	task->pkt_info1 |= HINIC_SQ_TASK_INFO1_SET(l4_len, INNER_L4LEN);

	*queue_info |= HINIC_SQ_CTRL_SET(offset, QUEUE_INFO_PLDOFF) |
		       HINIC_SQ_CTRL_SET(tcp_udp_cs, QUEUE_INFO_TCPUDP_CS) |
		       HINIC_SQ_CTRL_SET(sctp, QUEUE_INFO_SCTP);

	*queue_info = HINIC_SQ_CTRL_CLEAR(*queue_info, QUEUE_INFO_MSS);
	*queue_info |= HINIC_SQ_CTRL_SET(mss, QUEUE_INFO_MSS);
}

void hinic_set_tso_inner_l4(struct hinic_sq_task *task, u32 *queue_info,
			    enum hinic_l4_offload_type l4_offload,
			    u32 l4_len, u32 offset, u32 ip_ident, u32 mss)
{
	u32 tso = 0, ufo = 0;

	if (l4_offload == TCP_OFFLOAD_ENABLE)
		tso = 1;
	else if (l4_offload == UDP_OFFLOAD_ENABLE)
		ufo = 1;

	task->ufo_v6_identify = ip_ident;

	task->pkt_info0 |= HINIC_SQ_TASK_INFO0_SET(l4_offload, L4_OFFLOAD);
	task->pkt_info0 |= HINIC_SQ_TASK_INFO0_SET(tso || ufo, TSO_FLAG);
	task->pkt_info1 |= HINIC_SQ_TASK_INFO1_SET(l4_len, INNER_L4LEN);

	*queue_info |= HINIC_SQ_CTRL_SET(offset, QUEUE_INFO_PLDOFF) |
		       HINIC_SQ_CTRL_SET(tso, QUEUE_INFO_TSO) |
		       HINIC_SQ_CTRL_SET(ufo, QUEUE_INFO_UFO) |
		       HINIC_SQ_CTRL_SET(!!l4_offload, QUEUE_INFO_TCPUDP_CS);

	/* set MSS value */
	*queue_info = HINIC_SQ_CTRL_CLEAR(*queue_info, QUEUE_INFO_MSS);
	*queue_info |= HINIC_SQ_CTRL_SET(mss, QUEUE_INFO_MSS);
}

/**
 * hinic_sq_prepare_wqe - prepare wqe before insert to the queue
 * @sq: send queue
 * @prod_idx: pi value
 * @sq_wqe: wqe to prepare
 * @sges: sges for use by the wqe for send for buf addresses
 * @nr_sges: number of sges
 **/
void hinic_sq_prepare_wqe(struct hinic_sq *sq, u16 prod_idx,
			  struct hinic_sq_wqe *sq_wqe, struct hinic_sge *sges,
			  int nr_sges)
{
	int i;

	sq_prepare_ctrl(&sq_wqe->ctrl, prod_idx, nr_sges);

	sq_prepare_task(&sq_wqe->task);

	for (i = 0; i < nr_sges; i++)
		sq_wqe->buf_descs[i].sge = sges[i];
}

/**
 * sq_prepare_db - prepare doorbell to write
 * @sq: send queue
 * @prod_idx: pi value for the doorbell
 * @cos: cos of the doorbell
 *
 * Return db value
 **/
static u32 sq_prepare_db(struct hinic_sq *sq, u16 prod_idx, unsigned int cos)
{
	struct hinic_qp *qp = container_of(sq, struct hinic_qp, sq);
	u8 hi_prod_idx = SQ_DB_PI_HI(SQ_MASKED_IDX(sq, prod_idx));

	/* Data should be written to HW in Big Endian Format */
	return cpu_to_be32(HINIC_SQ_DB_INFO_SET(hi_prod_idx, PI_HI)     |
			   HINIC_SQ_DB_INFO_SET(HINIC_DB_SQ_TYPE, TYPE) |
			   HINIC_SQ_DB_INFO_SET(HINIC_DATA_PATH, PATH)  |
			   HINIC_SQ_DB_INFO_SET(cos, COS)               |
			   HINIC_SQ_DB_INFO_SET(qp->q_id, QID));
}

/**
 * hinic_sq_write_db- write doorbell
 * @sq: send queue
 * @prod_idx: pi value for the doorbell
 * @wqe_size: wqe size
 * @cos: cos of the wqe
 **/
void hinic_sq_write_db(struct hinic_sq *sq, u16 prod_idx, unsigned int wqe_size,
		       unsigned int cos)
{
	struct hinic_wq *wq = sq->wq;

	/* increment prod_idx to the next */
	prod_idx += ALIGN(wqe_size, wq->wqebb_size) / wq->wqebb_size;

	wmb();  /* Write all before the doorbell */

	writel(sq_prepare_db(sq, prod_idx, cos), SQ_DB_ADDR(sq, prod_idx));
}

/**
 * hinic_sq_get_wqe - get wqe ptr in the current pi and update the pi
 * @sq: sq to get wqe from
 * @wqe_size: wqe size
 * @prod_idx: returned pi
 *
 * Return wqe pointer
 **/
struct hinic_sq_wqe *hinic_sq_get_wqe(struct hinic_sq *sq,
				      unsigned int wqe_size, u16 *prod_idx)
{
	struct hinic_hw_wqe *hw_wqe = hinic_get_wqe(sq->wq, wqe_size,
						    prod_idx);

	if (IS_ERR(hw_wqe))
		return NULL;

	return &hw_wqe->sq_wqe;
}

/**
 * hinic_sq_return_wqe - return the wqe to the sq
 * @sq: send queue
 * @wqe_size: the size of the wqe
 **/
void hinic_sq_return_wqe(struct hinic_sq *sq, unsigned int wqe_size)
{
	hinic_return_wqe(sq->wq, wqe_size);
}

/**
 * hinic_sq_write_wqe - write the wqe to the sq
 * @sq: send queue
 * @prod_idx: pi of the wqe
 * @sq_wqe: the wqe to write
 * @skb: skb to save
 * @wqe_size: the size of the wqe
 **/
void hinic_sq_write_wqe(struct hinic_sq *sq, u16 prod_idx,
			struct hinic_sq_wqe *sq_wqe,
			struct sk_buff *skb, unsigned int wqe_size)
{
	struct hinic_hw_wqe *hw_wqe = (struct hinic_hw_wqe *)sq_wqe;

	sq->saved_skb[prod_idx] = skb;

	/* The data in the HW should be in Big Endian Format */
	hinic_cpu_to_be32(sq_wqe, wqe_size);

	hinic_write_wqe(sq->wq, hw_wqe, wqe_size);
}

/**
 * hinic_sq_read_wqebb - read wqe ptr in the current ci and update the ci, the
 * wqe only have one wqebb
 * @sq: send queue
 * @skb: return skb that was saved
 * @wqe_size: the wqe size ptr
 * @cons_idx: consumer index of the wqe
 *
 * Return wqe in ci position
 **/
struct hinic_sq_wqe *hinic_sq_read_wqebb(struct hinic_sq *sq,
					 struct sk_buff **skb,
					 unsigned int *wqe_size, u16 *cons_idx)
{
	struct hinic_hw_wqe *hw_wqe;
	struct hinic_sq_wqe *sq_wqe;
	struct hinic_sq_ctrl *ctrl;
	unsigned int buf_sect_len;
	u32 ctrl_info;

	/* read the ctrl section for getting wqe size */
	hw_wqe = hinic_read_wqe(sq->wq, sizeof(*ctrl), cons_idx);
	if (IS_ERR(hw_wqe))
		return NULL;

	*skb = sq->saved_skb[*cons_idx];

	sq_wqe = &hw_wqe->sq_wqe;
	ctrl = &sq_wqe->ctrl;
	ctrl_info = be32_to_cpu(ctrl->ctrl_info);
	buf_sect_len = HINIC_SQ_CTRL_GET(ctrl_info, BUFDESC_SECT_LEN);

	*wqe_size = sizeof(*ctrl) + sizeof(sq_wqe->task);
	*wqe_size += SECT_SIZE_FROM_8BYTES(buf_sect_len);
	*wqe_size = ALIGN(*wqe_size, sq->wq->wqebb_size);

	return &hw_wqe->sq_wqe;
}

/**
 * hinic_sq_read_wqe - read wqe ptr in the current ci and update the ci
 * @sq: send queue
 * @skb: return skb that was saved
 * @wqe_size: the size of the wqe
 * @cons_idx: consumer index of the wqe
 *
 * Return wqe in ci position
 **/
struct hinic_sq_wqe *hinic_sq_read_wqe(struct hinic_sq *sq,
				       struct sk_buff **skb,
				       unsigned int wqe_size, u16 *cons_idx)
{
	struct hinic_hw_wqe *hw_wqe;

	hw_wqe = hinic_read_wqe(sq->wq, wqe_size, cons_idx);
	*skb = sq->saved_skb[*cons_idx];

	return &hw_wqe->sq_wqe;
}

/**
 * hinic_sq_put_wqe - release the ci for new wqes
 * @sq: send queue
 * @wqe_size: the size of the wqe
 **/
void hinic_sq_put_wqe(struct hinic_sq *sq, unsigned int wqe_size)
{
	hinic_put_wqe(sq->wq, wqe_size);
}

/**
 * hinic_sq_get_sges - get sges from the wqe
 * @sq_wqe: wqe to get the sges from its buffer addresses
 * @sges: returned sges
 * @nr_sges: number sges to return
 **/
void hinic_sq_get_sges(struct hinic_sq_wqe *sq_wqe, struct hinic_sge *sges,
		       int nr_sges)
{
	int i;

	for (i = 0; i < nr_sges && i < HINIC_MAX_SQ_BUFDESCS; i++) {
		sges[i] = sq_wqe->buf_descs[i].sge;
		hinic_be32_to_cpu(&sges[i], sizeof(sges[i]));
	}
}

/**
 * hinic_rq_get_wqe - get wqe ptr in the current pi and update the pi
 * @rq: rq to get wqe from
 * @wqe_size: wqe size
 * @prod_idx: returned pi
 *
 * Return wqe pointer
 **/
struct hinic_rq_wqe *hinic_rq_get_wqe(struct hinic_rq *rq,
				      unsigned int wqe_size, u16 *prod_idx)
{
	struct hinic_hw_wqe *hw_wqe = hinic_get_wqe(rq->wq, wqe_size,
						    prod_idx);

	if (IS_ERR(hw_wqe))
		return NULL;

	return &hw_wqe->rq_wqe;
}

/**
 * hinic_rq_write_wqe - write the wqe to the rq
 * @rq: recv queue
 * @prod_idx: pi of the wqe
 * @rq_wqe: the wqe to write
 * @skb: skb to save
 **/
void hinic_rq_write_wqe(struct hinic_rq *rq, u16 prod_idx,
			struct hinic_rq_wqe *rq_wqe, struct sk_buff *skb)
{
	struct hinic_hw_wqe *hw_wqe = (struct hinic_hw_wqe *)rq_wqe;

	rq->saved_skb[prod_idx] = skb;

	/* The data in the HW should be in Big Endian Format */
	hinic_cpu_to_be32(rq_wqe, sizeof(*rq_wqe));

	hinic_write_wqe(rq->wq, hw_wqe, sizeof(*rq_wqe));
}

/**
 * hinic_rq_read_wqe - read wqe ptr in the current ci and update the ci
 * @rq: recv queue
 * @wqe_size: the size of the wqe
 * @skb: return saved skb
 * @cons_idx: consumer index of the wqe
 *
 * Return wqe in ci position
 **/
struct hinic_rq_wqe *hinic_rq_read_wqe(struct hinic_rq *rq,
				       unsigned int wqe_size,
				       struct sk_buff **skb, u16 *cons_idx)
{
	struct hinic_hw_wqe *hw_wqe;
	struct hinic_rq_cqe *cqe;
	int rx_done;
	u32 status;

	hw_wqe = hinic_read_wqe(rq->wq, wqe_size, cons_idx);
	if (IS_ERR(hw_wqe))
		return NULL;

	cqe = rq->cqe[*cons_idx];

	status = be32_to_cpu(cqe->status);

	rx_done = HINIC_RQ_CQE_STATUS_GET(status, RXDONE);
	if (!rx_done)
		return NULL;

	*skb = rq->saved_skb[*cons_idx];

	return &hw_wqe->rq_wqe;
}

/**
 * hinic_rq_read_next_wqe - increment ci and read the wqe in ci position
 * @rq: recv queue
 * @wqe_size: the size of the wqe
 * @skb: return saved skb
 * @cons_idx: consumer index in the wq
 *
 * Return wqe in incremented ci position
 **/
struct hinic_rq_wqe *hinic_rq_read_next_wqe(struct hinic_rq *rq,
					    unsigned int wqe_size,
					    struct sk_buff **skb,
					    u16 *cons_idx)
{
	struct hinic_wq *wq = rq->wq;
	struct hinic_hw_wqe *hw_wqe;
	unsigned int num_wqebbs;

	wqe_size = ALIGN(wqe_size, wq->wqebb_size);
	num_wqebbs = wqe_size / wq->wqebb_size;

	*cons_idx = RQ_MASKED_IDX(rq, *cons_idx + num_wqebbs);

	*skb = rq->saved_skb[*cons_idx];

	hw_wqe = hinic_read_wqe_direct(wq, *cons_idx);

	return &hw_wqe->rq_wqe;
}

/**
 * hinic_put_wqe - release the ci for new wqes
 * @rq: recv queue
 * @cons_idx: consumer index of the wqe
 * @wqe_size: the size of the wqe
 **/
void hinic_rq_put_wqe(struct hinic_rq *rq, u16 cons_idx,
		      unsigned int wqe_size)
{
	struct hinic_rq_cqe *cqe = rq->cqe[cons_idx];
	u32 status = be32_to_cpu(cqe->status);

	status = HINIC_RQ_CQE_STATUS_CLEAR(status, RXDONE);

	/* Rx WQE size is 1 WQEBB, no wq shadow*/
	cqe->status = cpu_to_be32(status);

	wmb();          /* clear done flag */

	hinic_put_wqe(rq->wq, wqe_size);
}

/**
 * hinic_rq_get_sge - get sge from the wqe
 * @rq: recv queue
 * @rq_wqe: wqe to get the sge from its buf address
 * @cons_idx: consumer index
 * @sge: returned sge
 **/
void hinic_rq_get_sge(struct hinic_rq *rq, struct hinic_rq_wqe *rq_wqe,
		      u16 cons_idx, struct hinic_sge *sge)
{
	struct hinic_rq_cqe *cqe = rq->cqe[cons_idx];
	u32 len = be32_to_cpu(cqe->len);

	sge->hi_addr = be32_to_cpu(rq_wqe->buf_desc.hi_addr);
	sge->lo_addr = be32_to_cpu(rq_wqe->buf_desc.lo_addr);
	sge->len = HINIC_RQ_CQE_SGE_GET(len, LEN);
}

/**
 * hinic_rq_prepare_wqe - prepare wqe before insert to the queue
 * @rq: recv queue
 * @prod_idx: pi value
 * @rq_wqe: the wqe
 * @sge: sge for use by the wqe for recv buf address
 **/
void hinic_rq_prepare_wqe(struct hinic_rq *rq, u16 prod_idx,
			  struct hinic_rq_wqe *rq_wqe, struct hinic_sge *sge)
{
	struct hinic_rq_cqe_sect *cqe_sect = &rq_wqe->cqe_sect;
	struct hinic_rq_bufdesc *buf_desc = &rq_wqe->buf_desc;
	struct hinic_rq_cqe *cqe = rq->cqe[prod_idx];
	struct hinic_rq_ctrl *ctrl = &rq_wqe->ctrl;
	dma_addr_t cqe_dma = rq->cqe_dma[prod_idx];

	ctrl->ctrl_info =
		HINIC_RQ_CTRL_SET(SIZE_8BYTES(sizeof(*ctrl)), LEN) |
		HINIC_RQ_CTRL_SET(SIZE_8BYTES(sizeof(*cqe_sect)),
				  COMPLETE_LEN)                    |
		HINIC_RQ_CTRL_SET(SIZE_8BYTES(sizeof(*buf_desc)),
				  BUFDESC_SECT_LEN)                |
		HINIC_RQ_CTRL_SET(RQ_COMPLETE_SGE, COMPLETE_FORMAT);

	hinic_set_sge(&cqe_sect->sge, cqe_dma, sizeof(*cqe));

	buf_desc->hi_addr = sge->hi_addr;
	buf_desc->lo_addr = sge->lo_addr;
}

/**
 * hinic_rq_update - update pi of the rq
 * @rq: recv queue
 * @prod_idx: pi value
 **/
void hinic_rq_update(struct hinic_rq *rq, u16 prod_idx)
{
	*rq->pi_virt_addr = cpu_to_be16(RQ_MASKED_IDX(rq, prod_idx + 1));
}
