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

#ifndef HINIC_HW_QP_H
#define HINIC_HW_QP_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/pci.h>
#include <linux/skbuff.h>

#include "hinic_common.h"
#include "hinic_hw_if.h"
#include "hinic_hw_wqe.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_qp_ctxt.h"

#define HINIC_SQ_DB_INFO_PI_HI_SHIFT            0
#define HINIC_SQ_DB_INFO_QID_SHIFT              8
#define HINIC_SQ_DB_INFO_PATH_SHIFT             23
#define HINIC_SQ_DB_INFO_COS_SHIFT              24
#define HINIC_SQ_DB_INFO_TYPE_SHIFT             27

#define HINIC_SQ_DB_INFO_PI_HI_MASK             0xFF
#define HINIC_SQ_DB_INFO_QID_MASK               0x3FF
#define HINIC_SQ_DB_INFO_PATH_MASK              0x1
#define HINIC_SQ_DB_INFO_COS_MASK               0x7
#define HINIC_SQ_DB_INFO_TYPE_MASK              0x1F

#define HINIC_SQ_DB_INFO_SET(val, member)       \
		(((u32)(val) & HINIC_SQ_DB_INFO_##member##_MASK) \
		 << HINIC_SQ_DB_INFO_##member##_SHIFT)

#define HINIC_SQ_WQEBB_SIZE                     64
#define HINIC_RQ_WQEBB_SIZE                     32

#define HINIC_SQ_PAGE_SIZE                      SZ_4K
#define HINIC_RQ_PAGE_SIZE                      SZ_4K

#define HINIC_SQ_DEPTH                          SZ_4K
#define HINIC_RQ_DEPTH                          SZ_4K

/* In any change to HINIC_RX_BUF_SZ, HINIC_RX_BUF_SZ_IDX must be changed */
#define HINIC_RX_BUF_SZ                         2048
#define HINIC_RX_BUF_SZ_IDX			HINIC_RX_BUF_SZ_2048_IDX

#define HINIC_MIN_TX_WQE_SIZE(wq)               \
		ALIGN(HINIC_SQ_WQE_SIZE(1), (wq)->wqebb_size)

#define HINIC_MIN_TX_NUM_WQEBBS(sq)             \
		(HINIC_MIN_TX_WQE_SIZE((sq)->wq) / (sq)->wq->wqebb_size)

enum hinic_rx_buf_sz_idx {
	HINIC_RX_BUF_SZ_32_IDX,
	HINIC_RX_BUF_SZ_64_IDX,
	HINIC_RX_BUF_SZ_96_IDX,
	HINIC_RX_BUF_SZ_128_IDX,
	HINIC_RX_BUF_SZ_192_IDX,
	HINIC_RX_BUF_SZ_256_IDX,
	HINIC_RX_BUF_SZ_384_IDX,
	HINIC_RX_BUF_SZ_512_IDX,
	HINIC_RX_BUF_SZ_768_IDX,
	HINIC_RX_BUF_SZ_1024_IDX,
	HINIC_RX_BUF_SZ_1536_IDX,
	HINIC_RX_BUF_SZ_2048_IDX,
	HINIC_RX_BUF_SZ_3072_IDX,
	HINIC_RX_BUF_SZ_4096_IDX,
	HINIC_RX_BUF_SZ_8192_IDX,
	HINIC_RX_BUF_SZ_16384_IDX,
};

struct hinic_sq {
	struct hinic_hwif       *hwif;

	struct hinic_wq         *wq;

	u32                     irq;
	u16                     msix_entry;

	void                    *hw_ci_addr;
	dma_addr_t              hw_ci_dma_addr;

	void __iomem            *db_base;

	struct sk_buff          **saved_skb;
};

struct hinic_rq {
	struct hinic_hwif       *hwif;

	struct hinic_wq         *wq;

	u32                     irq;
	u16                     msix_entry;

	size_t                  buf_sz;

	struct sk_buff          **saved_skb;

	struct hinic_rq_cqe     **cqe;
	dma_addr_t              *cqe_dma;

	u16                     *pi_virt_addr;
	dma_addr_t              pi_dma_addr;
};

struct hinic_qp {
	struct hinic_sq         sq;
	struct hinic_rq         rq;

	u16     q_id;
};

void hinic_qp_prepare_header(struct hinic_qp_ctxt_header *qp_ctxt_hdr,
			     enum hinic_qp_ctxt_type ctxt_type,
			     u16 num_queues, u16 max_queues);

void hinic_sq_prepare_ctxt(struct hinic_sq_ctxt *sq_ctxt,
			   struct hinic_sq *sq, u16 global_qid);

void hinic_rq_prepare_ctxt(struct hinic_rq_ctxt *rq_ctxt,
			   struct hinic_rq *rq, u16 global_qid);

int hinic_init_sq(struct hinic_sq *sq, struct hinic_hwif *hwif,
		  struct hinic_wq *wq, struct msix_entry *entry, void *ci_addr,
		  dma_addr_t ci_dma_addr, void __iomem *db_base);

void hinic_clean_sq(struct hinic_sq *sq);

int hinic_init_rq(struct hinic_rq *rq, struct hinic_hwif *hwif,
		  struct hinic_wq *wq, struct msix_entry *entry);

void hinic_clean_rq(struct hinic_rq *rq);

int hinic_get_sq_free_wqebbs(struct hinic_sq *sq);

int hinic_get_rq_free_wqebbs(struct hinic_rq *rq);

void hinic_task_set_l2hdr(struct hinic_sq_task *task, u32 len);

void hinic_task_set_outter_l3(struct hinic_sq_task *task,
			      enum hinic_l3_offload_type l3_type,
			      u32 network_len);

void hinic_task_set_inner_l3(struct hinic_sq_task *task,
			     enum hinic_l3_offload_type l3_type,
			     u32 network_len);

void hinic_task_set_tunnel_l4(struct hinic_sq_task *task,
			      enum hinic_l4_tunnel_type l4_type,
			      u32 tunnel_len);

void hinic_set_cs_inner_l4(struct hinic_sq_task *task,
			   u32 *queue_info,
			   enum hinic_l4_offload_type l4_offload,
			   u32 l4_len, u32 offset);

void hinic_set_tso_inner_l4(struct hinic_sq_task *task,
			    u32 *queue_info,
			    enum hinic_l4_offload_type l4_offload,
			    u32 l4_len,
			    u32 offset, u32 ip_ident, u32 mss);

void hinic_sq_prepare_wqe(struct hinic_sq *sq, u16 prod_idx,
			  struct hinic_sq_wqe *wqe, struct hinic_sge *sges,
			  int nr_sges);

void hinic_sq_write_db(struct hinic_sq *sq, u16 prod_idx, unsigned int wqe_size,
		       unsigned int cos);

struct hinic_sq_wqe *hinic_sq_get_wqe(struct hinic_sq *sq,
				      unsigned int wqe_size, u16 *prod_idx);

void hinic_sq_return_wqe(struct hinic_sq *sq, unsigned int wqe_size);

void hinic_sq_write_wqe(struct hinic_sq *sq, u16 prod_idx,
			struct hinic_sq_wqe *wqe, struct sk_buff *skb,
			unsigned int wqe_size);

struct hinic_sq_wqe *hinic_sq_read_wqe(struct hinic_sq *sq,
				       struct sk_buff **skb,
				       unsigned int wqe_size, u16 *cons_idx);

struct hinic_sq_wqe *hinic_sq_read_wqebb(struct hinic_sq *sq,
					 struct sk_buff **skb,
					 unsigned int *wqe_size, u16 *cons_idx);

void hinic_sq_put_wqe(struct hinic_sq *sq, unsigned int wqe_size);

void hinic_sq_get_sges(struct hinic_sq_wqe *wqe, struct hinic_sge *sges,
		       int nr_sges);

struct hinic_rq_wqe *hinic_rq_get_wqe(struct hinic_rq *rq,
				      unsigned int wqe_size, u16 *prod_idx);

void hinic_rq_write_wqe(struct hinic_rq *rq, u16 prod_idx,
			struct hinic_rq_wqe *wqe, struct sk_buff *skb);

struct hinic_rq_wqe *hinic_rq_read_wqe(struct hinic_rq *rq,
				       unsigned int wqe_size,
				       struct sk_buff **skb, u16 *cons_idx);

struct hinic_rq_wqe *hinic_rq_read_next_wqe(struct hinic_rq *rq,
					    unsigned int wqe_size,
					    struct sk_buff **skb,
					    u16 *cons_idx);

void hinic_rq_put_wqe(struct hinic_rq *rq, u16 cons_idx,
		      unsigned int wqe_size);

void hinic_rq_get_sge(struct hinic_rq *rq, struct hinic_rq_wqe *wqe,
		      u16 cons_idx, struct hinic_sge *sge);

void hinic_rq_prepare_wqe(struct hinic_rq *rq, u16 prod_idx,
			  struct hinic_rq_wqe *wqe, struct hinic_sge *sge);

void hinic_rq_update(struct hinic_rq *rq, u16 prod_idx);

#endif
