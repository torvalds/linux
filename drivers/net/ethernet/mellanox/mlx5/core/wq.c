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

#include <linux/mlx5/driver.h>
#include "wq.h"
#include "mlx5_core.h"

u32 mlx5_wq_cyc_get_size(struct mlx5_wq_cyc *wq)
{
	return (u32)wq->sz_m1 + 1;
}

u32 mlx5_cqwq_get_size(struct mlx5_cqwq *wq)
{
	return wq->sz_m1 + 1;
}

u32 mlx5_wq_ll_get_size(struct mlx5_wq_ll *wq)
{
	return (u32)wq->sz_m1 + 1;
}

static u32 mlx5_wq_cyc_get_byte_size(struct mlx5_wq_cyc *wq)
{
	return mlx5_wq_cyc_get_size(wq) << wq->log_stride;
}

static u32 mlx5_cqwq_get_byte_size(struct mlx5_cqwq *wq)
{
	return mlx5_cqwq_get_size(wq) << wq->log_stride;
}

static u32 mlx5_wq_ll_get_byte_size(struct mlx5_wq_ll *wq)
{
	return mlx5_wq_ll_get_size(wq) << wq->log_stride;
}

int mlx5_wq_cyc_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		       void *wqc, struct mlx5_wq_cyc *wq,
		       struct mlx5_wq_ctrl *wq_ctrl)
{
	int err;

	wq->log_stride = MLX5_GET(wq, wqc, log_wq_stride);
	wq->sz_m1 = (1 << MLX5_GET(wq, wqc, log_wq_sz)) - 1;

	err = mlx5_db_alloc_node(mdev, &wq_ctrl->db, param->db_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_db_alloc_node() failed, %d\n", err);
		return err;
	}

	err = mlx5_buf_alloc_node(mdev, mlx5_wq_cyc_get_byte_size(wq),
				  &wq_ctrl->buf, param->buf_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_buf_alloc_node() failed, %d\n", err);
		goto err_db_free;
	}

	wq->buf = wq_ctrl->buf.direct.buf;
	wq->db  = wq_ctrl->db.db;

	wq_ctrl->mdev = mdev;

	return 0;

err_db_free:
	mlx5_db_free(mdev, &wq_ctrl->db);

	return err;
}

int mlx5_cqwq_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		     void *cqc, struct mlx5_cqwq *wq,
		     struct mlx5_frag_wq_ctrl *wq_ctrl)
{
	int err;

	wq->log_stride	= 6 + MLX5_GET(cqc, cqc, cqe_sz);
	wq->log_sz	= MLX5_GET(cqc, cqc, log_cq_size);
	wq->sz_m1	= (1 << wq->log_sz) - 1;
	wq->log_frag_strides = PAGE_SHIFT - wq->log_stride;
	wq->frag_sz_m1	= (1 << wq->log_frag_strides) - 1;

	err = mlx5_db_alloc_node(mdev, &wq_ctrl->db, param->db_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_db_alloc_node() failed, %d\n", err);
		return err;
	}

	err = mlx5_frag_buf_alloc_node(mdev, mlx5_cqwq_get_byte_size(wq),
				       &wq_ctrl->frag_buf,
				       param->buf_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_frag_buf_alloc_node() failed, %d\n",
			       err);
		goto err_db_free;
	}

	wq->frag_buf = wq_ctrl->frag_buf;
	wq->db  = wq_ctrl->db.db;

	wq_ctrl->mdev = mdev;

	return 0;

err_db_free:
	mlx5_db_free(mdev, &wq_ctrl->db);

	return err;
}

int mlx5_wq_ll_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		      void *wqc, struct mlx5_wq_ll *wq,
		      struct mlx5_wq_ctrl *wq_ctrl)
{
	struct mlx5_wqe_srq_next_seg *next_seg;
	int err;
	int i;

	wq->log_stride = MLX5_GET(wq, wqc, log_wq_stride);
	wq->sz_m1 = (1 << MLX5_GET(wq, wqc, log_wq_sz)) - 1;

	err = mlx5_db_alloc_node(mdev, &wq_ctrl->db, param->db_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_db_alloc_node() failed, %d\n", err);
		return err;
	}

	err = mlx5_buf_alloc_node(mdev, mlx5_wq_ll_get_byte_size(wq),
				  &wq_ctrl->buf, param->buf_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_buf_alloc_node() failed, %d\n", err);
		goto err_db_free;
	}

	wq->buf = wq_ctrl->buf.direct.buf;
	wq->db  = wq_ctrl->db.db;

	for (i = 0; i < wq->sz_m1; i++) {
		next_seg = mlx5_wq_ll_get_wqe(wq, i);
		next_seg->next_wqe_index = cpu_to_be16(i + 1);
	}
	next_seg = mlx5_wq_ll_get_wqe(wq, i);
	wq->tail_next = &next_seg->next_wqe_index;

	wq_ctrl->mdev = mdev;

	return 0;

err_db_free:
	mlx5_db_free(mdev, &wq_ctrl->db);

	return err;
}

void mlx5_wq_destroy(struct mlx5_wq_ctrl *wq_ctrl)
{
	mlx5_buf_free(wq_ctrl->mdev, &wq_ctrl->buf);
	mlx5_db_free(wq_ctrl->mdev, &wq_ctrl->db);
}

void mlx5_cqwq_destroy(struct mlx5_frag_wq_ctrl *wq_ctrl)
{
	mlx5_frag_buf_free(wq_ctrl->mdev, &wq_ctrl->frag_buf);
	mlx5_db_free(wq_ctrl->mdev, &wq_ctrl->db);
}
