/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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

#include <config.h>

#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "mlx5.h"
#include "doorbell.h"
#include "wqe.h"

static void *get_wqe(struct mlx5_srq *srq, int n)
{
	return srq->buf.buf + (n << srq->wqe_shift);
}

int mlx5_copy_to_recv_srq(struct mlx5_srq *srq, int idx, void *buf, int size)
{
	struct mlx5_wqe_srq_next_seg *next;
	struct mlx5_wqe_data_seg *scat;
	int copy;
	int i;
	int max = 1 << (srq->wqe_shift - 4);

	next = get_wqe(srq, idx);
	scat = (struct mlx5_wqe_data_seg *) (next + 1);

	for (i = 0; i < max; ++i) {
		copy = min_t(long, size, be32toh(scat->byte_count));
		memcpy((void *)(unsigned long)be64toh(scat->addr), buf, copy);
		size -= copy;
		if (size <= 0)
			return IBV_WC_SUCCESS;

		buf += copy;
		++scat;
	}
	return IBV_WC_LOC_LEN_ERR;
}

void mlx5_free_srq_wqe(struct mlx5_srq *srq, int ind)
{
	struct mlx5_wqe_srq_next_seg *next;

	mlx5_spin_lock(&srq->lock);

	next = get_wqe(srq, srq->tail);
	next->next_wqe_index = htobe16(ind);
	srq->tail = ind;

	mlx5_spin_unlock(&srq->lock);
}

int mlx5_post_srq_recv(struct ibv_srq *ibsrq,
		       struct ibv_recv_wr *wr,
		       struct ibv_recv_wr **bad_wr)
{
	struct mlx5_srq *srq = to_msrq(ibsrq);
	struct mlx5_wqe_srq_next_seg *next;
	struct mlx5_wqe_data_seg *scat;
	int err = 0;
	int nreq;
	int i;

	mlx5_spin_lock(&srq->lock);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (wr->num_sge > srq->max_gs) {
			err = EINVAL;
			*bad_wr = wr;
			break;
		}

		if (srq->head == srq->tail) {
			/* SRQ is full*/
			err = ENOMEM;
			*bad_wr = wr;
			break;
		}

		srq->wrid[srq->head] = wr->wr_id;

		next      = get_wqe(srq, srq->head);
		srq->head = be16toh(next->next_wqe_index);
		scat      = (struct mlx5_wqe_data_seg *) (next + 1);

		for (i = 0; i < wr->num_sge; ++i) {
			scat[i].byte_count = htobe32(wr->sg_list[i].length);
			scat[i].lkey       = htobe32(wr->sg_list[i].lkey);
			scat[i].addr       = htobe64(wr->sg_list[i].addr);
		}

		if (i < srq->max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = htobe32(MLX5_INVALID_LKEY);
			scat[i].addr       = 0;
		}
	}

	if (nreq) {
		srq->counter += nreq;

		/*
		 * Make sure that descriptors are written before
		 * we write doorbell record.
		 */
		udma_to_device_barrier();

		*srq->db = htobe32(srq->counter);
	}

	mlx5_spin_unlock(&srq->lock);

	return err;
}

int mlx5_alloc_srq_buf(struct ibv_context *context, struct mlx5_srq *srq)
{
	struct mlx5_wqe_srq_next_seg *next;
	int size;
	int buf_size;
	int i;
	struct mlx5_context	   *ctx;

	ctx = to_mctx(context);

	if (srq->max_gs < 0) {
		errno = EINVAL;
		return -1;
	}

	srq->wrid = malloc(srq->max * sizeof *srq->wrid);
	if (!srq->wrid)
		return -1;

	size = sizeof(struct mlx5_wqe_srq_next_seg) +
		srq->max_gs * sizeof(struct mlx5_wqe_data_seg);
	size = max(32, size);

	size = mlx5_round_up_power_of_two(size);

	if (size > ctx->max_recv_wr) {
		errno = EINVAL;
		return -1;
	}
	srq->max_gs = (size - sizeof(struct mlx5_wqe_srq_next_seg)) /
		sizeof(struct mlx5_wqe_data_seg);

	srq->wqe_shift = mlx5_ilog2(size);

	buf_size = srq->max * size;

	if (mlx5_alloc_buf(&srq->buf, buf_size,
			   to_mdev(context->device)->page_size)) {
		free(srq->wrid);
		return -1;
	}

	memset(srq->buf.buf, 0, buf_size);

	/*
	 * Now initialize the SRQ buffer so that all of the WQEs are
	 * linked into the list of free WQEs.
	 */

	for (i = 0; i < srq->max; ++i) {
		next = get_wqe(srq, i);
		next->next_wqe_index = htobe16((i + 1) & (srq->max - 1));
	}

	srq->head = 0;
	srq->tail = srq->max - 1;

	return 0;
}

struct mlx5_srq *mlx5_find_srq(struct mlx5_context *ctx, uint32_t srqn)
{
	int tind = srqn >> MLX5_SRQ_TABLE_SHIFT;

	if (ctx->srq_table[tind].refcnt)
		return ctx->srq_table[tind].table[srqn & MLX5_SRQ_TABLE_MASK];
	else
		return NULL;
}

int mlx5_store_srq(struct mlx5_context *ctx, uint32_t srqn,
		   struct mlx5_srq *srq)
{
	int tind = srqn >> MLX5_SRQ_TABLE_SHIFT;

	if (!ctx->srq_table[tind].refcnt) {
		ctx->srq_table[tind].table = calloc(MLX5_QP_TABLE_MASK + 1,
						   sizeof(struct mlx5_qp *));
		if (!ctx->srq_table[tind].table)
			return -1;
	}

	++ctx->srq_table[tind].refcnt;
	ctx->srq_table[tind].table[srqn & MLX5_QP_TABLE_MASK] = srq;
	return 0;
}

void mlx5_clear_srq(struct mlx5_context *ctx, uint32_t srqn)
{
	int tind = srqn >> MLX5_QP_TABLE_SHIFT;

	if (!--ctx->srq_table[tind].refcnt)
		free(ctx->srq_table[tind].table);
	else
		ctx->srq_table[tind].table[srqn & MLX5_SRQ_TABLE_MASK] = NULL;
}
