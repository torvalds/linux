/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_NIC_IO_H_
#define _HINIC3_NIC_IO_H_

#include <linux/bitfield.h>

#include "hinic3_wq.h"

struct hinic3_nic_dev;

#define HINIC3_SQ_WQEBB_SHIFT      4
#define HINIC3_RQ_WQEBB_SHIFT      3
#define HINIC3_SQ_WQEBB_SIZE       BIT(HINIC3_SQ_WQEBB_SHIFT)

/* ******************** RQ_CTRL ******************** */
enum hinic3_rq_wqe_type {
	HINIC3_NORMAL_RQ_WQE = 1,
};

/* ******************** SQ_CTRL ******************** */
#define HINIC3_TX_MSS_DEFAULT  0x3E00
#define HINIC3_TX_MSS_MIN      0x50
#define HINIC3_MAX_SQ_SGE      18

struct hinic3_io_queue {
	struct hinic3_wq  wq;
	u8                owner;
	u16               q_id;
	u16               msix_entry_idx;
	u8 __iomem        *db_addr;
	u16               *cons_idx_addr;
} ____cacheline_aligned;

static inline u16 hinic3_get_sq_local_ci(const struct hinic3_io_queue *sq)
{
	const struct hinic3_wq *wq = &sq->wq;

	return wq->cons_idx & wq->idx_mask;
}

static inline u16 hinic3_get_sq_local_pi(const struct hinic3_io_queue *sq)
{
	const struct hinic3_wq *wq = &sq->wq;

	return wq->prod_idx & wq->idx_mask;
}

static inline u16 hinic3_get_sq_hw_ci(const struct hinic3_io_queue *sq)
{
	const struct hinic3_wq *wq = &sq->wq;

	return READ_ONCE(*sq->cons_idx_addr) & wq->idx_mask;
}

/* ******************** DB INFO ******************** */
#define DB_INFO_QID_MASK    GENMASK(12, 0)
#define DB_INFO_CFLAG_MASK  BIT(23)
#define DB_INFO_COS_MASK    GENMASK(26, 24)
#define DB_INFO_TYPE_MASK   GENMASK(31, 27)
#define DB_INFO_SET(val, member)  \
	FIELD_PREP(DB_INFO_##member##_MASK, val)

#define DB_PI_LOW_MASK   0xFFU
#define DB_PI_HIGH_MASK  0xFFU
#define DB_PI_HI_SHIFT   8
#define DB_PI_LOW(pi)    ((pi) & DB_PI_LOW_MASK)
#define DB_PI_HIGH(pi)   (((pi) >> DB_PI_HI_SHIFT) & DB_PI_HIGH_MASK)
#define DB_ADDR(q, pi)   ((u64 __iomem *)((q)->db_addr) + DB_PI_LOW(pi))
#define DB_SRC_TYPE      1

/* CFLAG_DATA_PATH */
#define DB_CFLAG_DP_SQ   0
#define DB_CFLAG_DP_RQ   1

struct hinic3_nic_db {
	__le32 db_info;
	__le32 pi_hi;
};

static inline void hinic3_write_db(struct hinic3_io_queue *queue, int cos,
				   u8 cflag, u16 pi)
{
	struct hinic3_nic_db db;

	db.db_info =
		cpu_to_le32(DB_INFO_SET(DB_SRC_TYPE, TYPE) |
			    DB_INFO_SET(cflag, CFLAG) |
			    DB_INFO_SET(cos, COS) |
			    DB_INFO_SET(queue->q_id, QID));
	db.pi_hi = cpu_to_le32(DB_PI_HIGH(pi));

	writeq(*((u64 *)&db), DB_ADDR(queue, pi));
}

struct hinic3_dyna_qp_params {
	u16                    num_qps;
	u32                    sq_depth;
	u32                    rq_depth;

	struct hinic3_io_queue *sqs;
	struct hinic3_io_queue *rqs;
};

struct hinic3_nic_io {
	struct hinic3_io_queue *sq;
	struct hinic3_io_queue *rq;

	u16                    num_qps;
	u16                    max_qps;

	/* Base address for consumer index of all tx queues. Each queue is
	 * given a full cache line to hold its consumer index. HW updates
	 * current consumer index as it consumes tx WQEs.
	 */
	void                   *ci_vaddr_base;
	dma_addr_t             ci_dma_base;

	u8 __iomem             *sqs_db_addr;
	u8 __iomem             *rqs_db_addr;

	u16                    rx_buf_len;
	u64                    feature_cap;
};

int hinic3_init_nic_io(struct hinic3_nic_dev *nic_dev);
void hinic3_free_nic_io(struct hinic3_nic_dev *nic_dev);

int hinic3_init_nicio_res(struct hinic3_nic_dev *nic_dev);
void hinic3_free_nicio_res(struct hinic3_nic_dev *nic_dev);

int hinic3_alloc_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params);
void hinic3_free_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params);
void hinic3_init_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params);
void hinic3_uninit_qps(struct hinic3_nic_dev *nic_dev,
		       struct hinic3_dyna_qp_params *qp_params);

int hinic3_init_qp_ctxts(struct hinic3_nic_dev *nic_dev);
void hinic3_free_qp_ctxts(struct hinic3_nic_dev *nic_dev);

#endif
