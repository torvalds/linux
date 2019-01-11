/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __MLX5_WQ_H__
#define __MLX5_WQ_H__

#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>

struct mlx5_wq_param {
	int		buf_numa_node;
	int		db_numa_node;
};

struct mlx5_wq_ctrl {
	struct mlx5_core_dev	*mdev;
	struct mlx5_frag_buf	buf;
	struct mlx5_db		db;
};

struct mlx5_wq_cyc {
	struct mlx5_frag_buf_ctrl fbc;
	__be32			*db;
	u16			sz;
	u16			wqe_ctr;
	u16			cur_sz;
};

struct mlx5_wq_qp {
	struct mlx5_wq_cyc	rq;
	struct mlx5_wq_cyc	sq;
};

struct mlx5_cqwq {
	struct mlx5_frag_buf_ctrl fbc;
	__be32			  *db;
	u32			  cc; /* consumer counter */
};

struct mlx5_wq_ll {
	struct mlx5_frag_buf_ctrl fbc;
	__be32			*db;
	__be16			*tail_next;
	u16			head;
	u16			wqe_ctr;
	u16			cur_sz;
};

int mlx5_wq_cyc_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		       void *wqc, struct mlx5_wq_cyc *wq,
		       struct mlx5_wq_ctrl *wq_ctrl);
u32 mlx5_wq_cyc_get_size(struct mlx5_wq_cyc *wq);
u32 mlx5_wq_cyc_get_frag_size(struct mlx5_wq_cyc *wq);

int mlx5_wq_qp_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		      void *qpc, struct mlx5_wq_qp *wq,
		      struct mlx5_wq_ctrl *wq_ctrl);

int mlx5_cqwq_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		     void *cqc, struct mlx5_cqwq *wq,
		     struct mlx5_wq_ctrl *wq_ctrl);
u32 mlx5_cqwq_get_size(struct mlx5_cqwq *wq);

int mlx5_wq_ll_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		      void *wqc, struct mlx5_wq_ll *wq,
		      struct mlx5_wq_ctrl *wq_ctrl);
u32 mlx5_wq_ll_get_size(struct mlx5_wq_ll *wq);

void mlx5_wq_destroy(struct mlx5_wq_ctrl *wq_ctrl);

static inline int mlx5_wq_cyc_is_full(struct mlx5_wq_cyc *wq)
{
	return wq->cur_sz == wq->sz;
}

static inline int mlx5_wq_cyc_missing(struct mlx5_wq_cyc *wq)
{
	return wq->sz - wq->cur_sz;
}

static inline int mlx5_wq_cyc_is_empty(struct mlx5_wq_cyc *wq)
{
	return !wq->cur_sz;
}

static inline void mlx5_wq_cyc_push(struct mlx5_wq_cyc *wq)
{
	wq->wqe_ctr++;
	wq->cur_sz++;
}

static inline void mlx5_wq_cyc_push_n(struct mlx5_wq_cyc *wq, u8 n)
{
	wq->wqe_ctr += n;
	wq->cur_sz += n;
}

static inline void mlx5_wq_cyc_pop(struct mlx5_wq_cyc *wq)
{
	wq->cur_sz--;
}

static inline void mlx5_wq_cyc_update_db_record(struct mlx5_wq_cyc *wq)
{
	*wq->db = cpu_to_be32(wq->wqe_ctr);
}

static inline u16 mlx5_wq_cyc_ctr2ix(struct mlx5_wq_cyc *wq, u16 ctr)
{
	return ctr & wq->fbc.sz_m1;
}

static inline u16 mlx5_wq_cyc_ctr2fragix(struct mlx5_wq_cyc *wq, u16 ctr)
{
	return ctr & wq->fbc.frag_sz_m1;
}

static inline u16 mlx5_wq_cyc_get_head(struct mlx5_wq_cyc *wq)
{
	return mlx5_wq_cyc_ctr2ix(wq, wq->wqe_ctr);
}

static inline u16 mlx5_wq_cyc_get_tail(struct mlx5_wq_cyc *wq)
{
	return mlx5_wq_cyc_ctr2ix(wq, wq->wqe_ctr - wq->cur_sz);
}

static inline void *mlx5_wq_cyc_get_wqe(struct mlx5_wq_cyc *wq, u16 ix)
{
	return mlx5_frag_buf_get_wqe(&wq->fbc, ix);
}

static inline int mlx5_wq_cyc_cc_bigger(u16 cc1, u16 cc2)
{
	int equal   = (cc1 == cc2);
	int smaller = 0x8000 & (cc1 - cc2);

	return !equal && !smaller;
}

static inline u32 mlx5_cqwq_ctr2ix(struct mlx5_cqwq *wq, u32 ctr)
{
	return ctr & wq->fbc.sz_m1;
}

static inline u32 mlx5_cqwq_get_ci(struct mlx5_cqwq *wq)
{
	return mlx5_cqwq_ctr2ix(wq, wq->cc);
}

static inline void *mlx5_cqwq_get_wqe(struct mlx5_cqwq *wq, u32 ix)
{
	return mlx5_frag_buf_get_wqe(&wq->fbc, ix);
}

static inline u32 mlx5_cqwq_get_ctr_wrap_cnt(struct mlx5_cqwq *wq, u32 ctr)
{
	return ctr >> wq->fbc.log_sz;
}

static inline u32 mlx5_cqwq_get_wrap_cnt(struct mlx5_cqwq *wq)
{
	return mlx5_cqwq_get_ctr_wrap_cnt(wq, wq->cc);
}

static inline void mlx5_cqwq_pop(struct mlx5_cqwq *wq)
{
	wq->cc++;
}

static inline void mlx5_cqwq_update_db_record(struct mlx5_cqwq *wq)
{
	*wq->db = cpu_to_be32(wq->cc & 0xffffff);
}

static inline struct mlx5_cqe64 *mlx5_cqwq_get_cqe(struct mlx5_cqwq *wq)
{
	u32 ci = mlx5_cqwq_get_ci(wq);
	struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(wq, ci);
	u8 cqe_ownership_bit = cqe->op_own & MLX5_CQE_OWNER_MASK;
	u8 sw_ownership_val = mlx5_cqwq_get_wrap_cnt(wq) & 1;

	if (cqe_ownership_bit != sw_ownership_val)
		return NULL;

	/* ensure cqe content is read after cqe ownership bit */
	dma_rmb();

	return cqe;
}

static inline int mlx5_wq_ll_is_full(struct mlx5_wq_ll *wq)
{
	return wq->cur_sz == wq->fbc.sz_m1;
}

static inline int mlx5_wq_ll_is_empty(struct mlx5_wq_ll *wq)
{
	return !wq->cur_sz;
}

static inline void *mlx5_wq_ll_get_wqe(struct mlx5_wq_ll *wq, u16 ix)
{
	return mlx5_frag_buf_get_wqe(&wq->fbc, ix);
}

static inline void mlx5_wq_ll_push(struct mlx5_wq_ll *wq, u16 head_next)
{
	wq->head = head_next;
	wq->wqe_ctr++;
	wq->cur_sz++;
}

static inline void mlx5_wq_ll_pop(struct mlx5_wq_ll *wq, __be16 ix,
				  __be16 *next_tail_next)
{
	*wq->tail_next = ix;
	wq->tail_next = next_tail_next;
	wq->cur_sz--;
}

static inline void mlx5_wq_ll_update_db_record(struct mlx5_wq_ll *wq)
{
	*wq->db = cpu_to_be32(wq->wqe_ctr);
}

#endif /* __MLX5_WQ_H__ */
