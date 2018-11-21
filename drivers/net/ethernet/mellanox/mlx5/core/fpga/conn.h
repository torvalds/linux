/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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
 *
 */

#ifndef __MLX5_FPGA_CONN_H__
#define __MLX5_FPGA_CONN_H__

#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>

#include "fpga/core.h"
#include "fpga/sdk.h"
#include "wq.h"

struct mlx5_fpga_conn {
	struct mlx5_fpga_device *fdev;

	void (*recv_cb)(void *cb_arg, struct mlx5_fpga_dma_buf *buf);
	void *cb_arg;

	/* FPGA QP */
	u32 fpga_qpc[MLX5_ST_SZ_DW(fpga_qpc)];
	u32 fpga_qpn;

	/* CQ */
	struct {
		struct mlx5_cqwq wq;
		struct mlx5_wq_ctrl wq_ctrl;
		struct mlx5_core_cq mcq;
		struct tasklet_struct tasklet;
	} cq;

	/* QP */
	struct {
		bool active;
		int sgid_index;
		struct mlx5_wq_qp wq;
		struct mlx5_wq_ctrl wq_ctrl;
		struct mlx5_core_qp mqp;
		struct {
			spinlock_t lock; /* Protects all SQ state */
			unsigned int pc;
			unsigned int cc;
			unsigned int size;
			struct mlx5_fpga_dma_buf **bufs;
			struct list_head backlog;
		} sq;
		struct {
			unsigned int pc;
			unsigned int cc;
			unsigned int size;
			struct mlx5_fpga_dma_buf **bufs;
		} rq;
	} qp;
};

int mlx5_fpga_conn_device_init(struct mlx5_fpga_device *fdev);
void mlx5_fpga_conn_device_cleanup(struct mlx5_fpga_device *fdev);
struct mlx5_fpga_conn *
mlx5_fpga_conn_create(struct mlx5_fpga_device *fdev,
		      struct mlx5_fpga_conn_attr *attr,
		      enum mlx5_ifc_fpga_qp_type qp_type);
void mlx5_fpga_conn_destroy(struct mlx5_fpga_conn *conn);
int mlx5_fpga_conn_send(struct mlx5_fpga_conn *conn,
			struct mlx5_fpga_dma_buf *buf);

#endif /* __MLX5_FPGA_CONN_H__ */
