/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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

#include "mlx4.h"
#include "doorbell.h"
#include "wqe.h"
#include "mlx4-abi.h"

static void *get_wqe(struct mlx4_srq *srq, int n)
{
	return srq->buf.buf + (n << srq->wqe_shift);
}

void mlx4_free_srq_wqe(struct mlx4_srq *srq, int ind)
{
	struct mlx4_wqe_srq_next_seg *next;

	pthread_spin_lock(&srq->lock);

	next = get_wqe(srq, srq->tail);
	next->next_wqe_index = htobe16(ind);
	srq->tail = ind;

	pthread_spin_unlock(&srq->lock);
}

int mlx4_post_srq_recv(struct ibv_srq *ibsrq,
		       struct ibv_recv_wr *wr,
		       struct ibv_recv_wr **bad_wr)
{
	struct mlx4_srq *srq = to_msrq(ibsrq);
	struct mlx4_wqe_srq_next_seg *next;
	struct mlx4_wqe_data_seg *scat;
	int err = 0;
	int nreq;
	int i;

	pthread_spin_lock(&srq->lock);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (wr->num_sge > srq->max_gs) {
			err = -1;
			*bad_wr = wr;
			break;
		}

		if (srq->head == srq->tail) {
			/* SRQ is full*/
			err = -1;
			*bad_wr = wr;
			break;
		}

		srq->wrid[srq->head] = wr->wr_id;

		next      = get_wqe(srq, srq->head);
		srq->head = be16toh(next->next_wqe_index);
		scat      = (struct mlx4_wqe_data_seg *) (next + 1);

		for (i = 0; i < wr->num_sge; ++i) {
			scat[i].byte_count = htobe32(wr->sg_list[i].length);
			scat[i].lkey       = htobe32(wr->sg_list[i].lkey);
			scat[i].addr       = htobe64(wr->sg_list[i].addr);
		}

		if (i < srq->max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = htobe32(MLX4_INVALID_LKEY);
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

	pthread_spin_unlock(&srq->lock);

	return err;
}

int mlx4_alloc_srq_buf(struct ibv_pd *pd, struct ibv_srq_attr *attr,
		       struct mlx4_srq *srq)
{
	struct mlx4_wqe_srq_next_seg *next;
	struct mlx4_wqe_data_seg *scatter;
	int size;
	int buf_size;
	int i;

	srq->wrid = malloc(srq->max * sizeof (uint64_t));
	if (!srq->wrid)
		return -1;

	size = sizeof (struct mlx4_wqe_srq_next_seg) +
		srq->max_gs * sizeof (struct mlx4_wqe_data_seg);

	for (srq->wqe_shift = 5; 1 << srq->wqe_shift < size; ++srq->wqe_shift)
		; /* nothing */

	buf_size = srq->max << srq->wqe_shift;

	if (mlx4_alloc_buf(&srq->buf, buf_size,
			   to_mdev(pd->context->device)->page_size)) {
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

		for (scatter = (void *) (next + 1);
		     (void *) scatter < (void *) next + (1 << srq->wqe_shift);
		     ++scatter)
			scatter->lkey = htobe32(MLX4_INVALID_LKEY);
	}

	srq->head = 0;
	srq->tail = srq->max - 1;

	return 0;
}

void mlx4_init_xsrq_table(struct mlx4_xsrq_table *xsrq_table, int size)
{
	memset(xsrq_table, 0, sizeof *xsrq_table);
	xsrq_table->num_xsrq = size;
	xsrq_table->shift = ffs(size) - 1 - MLX4_XSRQ_TABLE_BITS;
	xsrq_table->mask = (1 << xsrq_table->shift) - 1;

	pthread_mutex_init(&xsrq_table->mutex, NULL);
}

struct mlx4_srq *mlx4_find_xsrq(struct mlx4_xsrq_table *xsrq_table, uint32_t srqn)
{
	int index;

	index = (srqn & (xsrq_table->num_xsrq - 1)) >> xsrq_table->shift;
	if (xsrq_table->xsrq_table[index].refcnt)
		return xsrq_table->xsrq_table[index].table[srqn & xsrq_table->mask];

	return NULL;
}

int mlx4_store_xsrq(struct mlx4_xsrq_table *xsrq_table, uint32_t srqn,
		    struct mlx4_srq *srq)
{
	int index, ret = 0;

	index = (srqn & (xsrq_table->num_xsrq - 1)) >> xsrq_table->shift;
	pthread_mutex_lock(&xsrq_table->mutex);
	if (!xsrq_table->xsrq_table[index].refcnt) {
		xsrq_table->xsrq_table[index].table = calloc(xsrq_table->mask + 1,
							     sizeof(struct mlx4_srq *));
		if (!xsrq_table->xsrq_table[index].table) {
			ret = -1;
			goto out;
		}
	}

	xsrq_table->xsrq_table[index].refcnt++;
	xsrq_table->xsrq_table[index].table[srqn & xsrq_table->mask] = srq;

out:
	pthread_mutex_unlock(&xsrq_table->mutex);
	return ret;
}

void mlx4_clear_xsrq(struct mlx4_xsrq_table *xsrq_table, uint32_t srqn)
{
	int index;

	index = (srqn & (xsrq_table->num_xsrq - 1)) >> xsrq_table->shift;
	pthread_mutex_lock(&xsrq_table->mutex);

	if (--xsrq_table->xsrq_table[index].refcnt)
		xsrq_table->xsrq_table[index].table[srqn & xsrq_table->mask] = NULL;
	else
		free(xsrq_table->xsrq_table[index].table);

	pthread_mutex_unlock(&xsrq_table->mutex);
}

struct ibv_srq *mlx4_create_xrc_srq(struct ibv_context *context,
				    struct ibv_srq_init_attr_ex *attr_ex)
{
	struct mlx4_create_xsrq cmd;
	struct mlx4_create_srq_resp resp;
	struct mlx4_srq *srq;
	int ret;

	/* Sanity check SRQ size before proceeding */
	if (attr_ex->attr.max_wr > 1 << 16 || attr_ex->attr.max_sge > 64)
		return NULL;

	srq = calloc(1, sizeof *srq);
	if (!srq)
		return NULL;

	if (pthread_spin_init(&srq->lock, PTHREAD_PROCESS_PRIVATE))
		goto err;

	srq->max     = align_queue_size(attr_ex->attr.max_wr + 1);
	srq->max_gs  = attr_ex->attr.max_sge;
	srq->counter = 0;
	srq->ext_srq = 1;

	if (mlx4_alloc_srq_buf(attr_ex->pd, &attr_ex->attr, srq))
		goto err;

	srq->db = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_RQ);
	if (!srq->db)
		goto err_free;

	*srq->db = 0;

	cmd.buf_addr = (uintptr_t) srq->buf.buf;
	cmd.db_addr  = (uintptr_t) srq->db;

	ret = ibv_cmd_create_srq_ex(context, &srq->verbs_srq,
				    sizeof(srq->verbs_srq),
				    attr_ex,
				    &cmd.ibv_cmd, sizeof cmd,
				    &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err_db;

	ret = mlx4_store_xsrq(&to_mctx(context)->xsrq_table,
			      srq->verbs_srq.srq_num, srq);
	if (ret)
		goto err_destroy;

	return &srq->verbs_srq.srq;

err_destroy:
	ibv_cmd_destroy_srq(&srq->verbs_srq.srq);
err_db:
	mlx4_free_db(to_mctx(context), MLX4_DB_TYPE_RQ, srq->db);
err_free:
	free(srq->wrid);
	mlx4_free_buf(&srq->buf);
err:
	free(srq);
	return NULL;
}

int mlx4_destroy_xrc_srq(struct ibv_srq *srq)
{
	struct mlx4_context *mctx = to_mctx(srq->context);
	struct mlx4_srq *msrq = to_msrq(srq);
	struct mlx4_cq *mcq;
	int ret;

	mcq = to_mcq(msrq->verbs_srq.cq);
	mlx4_cq_clean(mcq, 0, msrq);
	pthread_spin_lock(&mcq->lock);
	mlx4_clear_xsrq(&mctx->xsrq_table, msrq->verbs_srq.srq_num);
	pthread_spin_unlock(&mcq->lock);

	ret = ibv_cmd_destroy_srq(srq);
	if (ret) {
		pthread_spin_lock(&mcq->lock);
		mlx4_store_xsrq(&mctx->xsrq_table, msrq->verbs_srq.srq_num, msrq);
		pthread_spin_unlock(&mcq->lock);
		return ret;
	}

	mlx4_free_db(mctx, MLX4_DB_TYPE_RQ, msrq->db);
	mlx4_free_buf(&msrq->buf);
	free(msrq->wrid);
	free(msrq);

	return 0;
}
