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

#include <net/addrconf.h>
#include <linux/etherdevice.h>
#include <linux/mlx5/vport.h>

#include "mlx5_core.h"
#include "lib/mlx5.h"
#include "fpga/conn.h"

#define MLX5_FPGA_PKEY 0xFFFF
#define MLX5_FPGA_PKEY_INDEX 0 /* RoCE PKEY 0xFFFF is always at index 0 */
#define MLX5_FPGA_RECV_SIZE 2048
#define MLX5_FPGA_PORT_NUM 1
#define MLX5_FPGA_CQ_BUDGET 64

static int mlx5_fpga_conn_map_buf(struct mlx5_fpga_conn *conn,
				  struct mlx5_fpga_dma_buf *buf)
{
	struct device *dma_device;
	int err = 0;

	if (unlikely(!buf->sg[0].data))
		goto out;

	dma_device = &conn->fdev->mdev->pdev->dev;
	buf->sg[0].dma_addr = dma_map_single(dma_device, buf->sg[0].data,
					     buf->sg[0].size, buf->dma_dir);
	err = dma_mapping_error(dma_device, buf->sg[0].dma_addr);
	if (unlikely(err)) {
		mlx5_fpga_warn(conn->fdev, "DMA error on sg 0: %d\n", err);
		err = -ENOMEM;
		goto out;
	}

	if (!buf->sg[1].data)
		goto out;

	buf->sg[1].dma_addr = dma_map_single(dma_device, buf->sg[1].data,
					     buf->sg[1].size, buf->dma_dir);
	err = dma_mapping_error(dma_device, buf->sg[1].dma_addr);
	if (unlikely(err)) {
		mlx5_fpga_warn(conn->fdev, "DMA error on sg 1: %d\n", err);
		dma_unmap_single(dma_device, buf->sg[0].dma_addr,
				 buf->sg[0].size, buf->dma_dir);
		err = -ENOMEM;
	}

out:
	return err;
}

static void mlx5_fpga_conn_unmap_buf(struct mlx5_fpga_conn *conn,
				     struct mlx5_fpga_dma_buf *buf)
{
	struct device *dma_device;

	dma_device = &conn->fdev->mdev->pdev->dev;
	if (buf->sg[1].data)
		dma_unmap_single(dma_device, buf->sg[1].dma_addr,
				 buf->sg[1].size, buf->dma_dir);

	if (likely(buf->sg[0].data))
		dma_unmap_single(dma_device, buf->sg[0].dma_addr,
				 buf->sg[0].size, buf->dma_dir);
}

static int mlx5_fpga_conn_post_recv(struct mlx5_fpga_conn *conn,
				    struct mlx5_fpga_dma_buf *buf)
{
	struct mlx5_wqe_data_seg *data;
	unsigned int ix;
	int err = 0;

	err = mlx5_fpga_conn_map_buf(conn, buf);
	if (unlikely(err))
		goto out;

	if (unlikely(conn->qp.rq.pc - conn->qp.rq.cc >= conn->qp.rq.size)) {
		mlx5_fpga_conn_unmap_buf(conn, buf);
		return -EBUSY;
	}

	ix = conn->qp.rq.pc & (conn->qp.rq.size - 1);
	data = mlx5_wq_cyc_get_wqe(&conn->qp.wq.rq, ix);
	data->byte_count = cpu_to_be32(buf->sg[0].size);
	data->lkey = cpu_to_be32(conn->fdev->conn_res.mkey.key);
	data->addr = cpu_to_be64(buf->sg[0].dma_addr);

	conn->qp.rq.pc++;
	conn->qp.rq.bufs[ix] = buf;

	/* Make sure that descriptors are written before doorbell record. */
	dma_wmb();
	*conn->qp.wq.rq.db = cpu_to_be32(conn->qp.rq.pc & 0xffff);
out:
	return err;
}

static void mlx5_fpga_conn_notify_hw(struct mlx5_fpga_conn *conn, void *wqe)
{
	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();
	*conn->qp.wq.sq.db = cpu_to_be32(conn->qp.sq.pc);
	/* Make sure that doorbell record is visible before ringing */
	wmb();
	mlx5_write64(wqe, conn->fdev->conn_res.uar->map + MLX5_BF_OFFSET, NULL);
}

static void mlx5_fpga_conn_post_send(struct mlx5_fpga_conn *conn,
				     struct mlx5_fpga_dma_buf *buf)
{
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_wqe_data_seg *data;
	unsigned int ix, sgi;
	int size = 1;

	ix = conn->qp.sq.pc & (conn->qp.sq.size - 1);

	ctrl = mlx5_wq_cyc_get_wqe(&conn->qp.wq.sq, ix);
	data = (void *)(ctrl + 1);

	for (sgi = 0; sgi < ARRAY_SIZE(buf->sg); sgi++) {
		if (!buf->sg[sgi].data)
			break;
		data->byte_count = cpu_to_be32(buf->sg[sgi].size);
		data->lkey = cpu_to_be32(conn->fdev->conn_res.mkey.key);
		data->addr = cpu_to_be64(buf->sg[sgi].dma_addr);
		data++;
		size++;
	}

	ctrl->imm = 0;
	ctrl->fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
	ctrl->opmod_idx_opcode = cpu_to_be32(((conn->qp.sq.pc & 0xffff) << 8) |
					     MLX5_OPCODE_SEND);
	ctrl->qpn_ds = cpu_to_be32(size | (conn->qp.mqp.qpn << 8));

	conn->qp.sq.pc++;
	conn->qp.sq.bufs[ix] = buf;
	mlx5_fpga_conn_notify_hw(conn, ctrl);
}

int mlx5_fpga_conn_send(struct mlx5_fpga_conn *conn,
			struct mlx5_fpga_dma_buf *buf)
{
	unsigned long flags;
	int err;

	if (!conn->qp.active)
		return -ENOTCONN;

	buf->dma_dir = DMA_TO_DEVICE;
	err = mlx5_fpga_conn_map_buf(conn, buf);
	if (err)
		return err;

	spin_lock_irqsave(&conn->qp.sq.lock, flags);

	if (conn->qp.sq.pc - conn->qp.sq.cc >= conn->qp.sq.size) {
		list_add_tail(&buf->list, &conn->qp.sq.backlog);
		goto out_unlock;
	}

	mlx5_fpga_conn_post_send(conn, buf);

out_unlock:
	spin_unlock_irqrestore(&conn->qp.sq.lock, flags);
	return err;
}

static int mlx5_fpga_conn_post_recv_buf(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_dma_buf *buf;
	int err;

	buf = kzalloc(sizeof(*buf) + MLX5_FPGA_RECV_SIZE, 0);
	if (!buf)
		return -ENOMEM;

	buf->sg[0].data = (void *)(buf + 1);
	buf->sg[0].size = MLX5_FPGA_RECV_SIZE;
	buf->dma_dir = DMA_FROM_DEVICE;

	err = mlx5_fpga_conn_post_recv(conn, buf);
	if (err)
		kfree(buf);

	return err;
}

static int mlx5_fpga_conn_create_mkey(struct mlx5_core_dev *mdev, u32 pdn,
				      struct mlx5_core_mkey *mkey)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	void *mkc;
	u32 *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);

	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	err = mlx5_core_create_mkey(mdev, mkey, in, inlen);

	kvfree(in);
	return err;
}

static void mlx5_fpga_conn_rq_cqe(struct mlx5_fpga_conn *conn,
				  struct mlx5_cqe64 *cqe, u8 status)
{
	struct mlx5_fpga_dma_buf *buf;
	int ix, err;

	ix = be16_to_cpu(cqe->wqe_counter) & (conn->qp.rq.size - 1);
	buf = conn->qp.rq.bufs[ix];
	conn->qp.rq.bufs[ix] = NULL;
	conn->qp.rq.cc++;

	if (unlikely(status && (status != MLX5_CQE_SYNDROME_WR_FLUSH_ERR)))
		mlx5_fpga_warn(conn->fdev, "RQ buf %p on FPGA QP %u completion status %d\n",
			       buf, conn->fpga_qpn, status);
	else
		mlx5_fpga_dbg(conn->fdev, "RQ buf %p on FPGA QP %u completion status %d\n",
			      buf, conn->fpga_qpn, status);

	mlx5_fpga_conn_unmap_buf(conn, buf);

	if (unlikely(status || !conn->qp.active)) {
		conn->qp.active = false;
		kfree(buf);
		return;
	}

	buf->sg[0].size = be32_to_cpu(cqe->byte_cnt);
	mlx5_fpga_dbg(conn->fdev, "Message with %u bytes received successfully\n",
		      buf->sg[0].size);
	conn->recv_cb(conn->cb_arg, buf);

	buf->sg[0].size = MLX5_FPGA_RECV_SIZE;
	err = mlx5_fpga_conn_post_recv(conn, buf);
	if (unlikely(err)) {
		mlx5_fpga_warn(conn->fdev,
			       "Failed to re-post recv buf: %d\n", err);
		kfree(buf);
	}
}

static void mlx5_fpga_conn_sq_cqe(struct mlx5_fpga_conn *conn,
				  struct mlx5_cqe64 *cqe, u8 status)
{
	struct mlx5_fpga_dma_buf *buf, *nextbuf;
	unsigned long flags;
	int ix;

	spin_lock_irqsave(&conn->qp.sq.lock, flags);

	ix = be16_to_cpu(cqe->wqe_counter) & (conn->qp.sq.size - 1);
	buf = conn->qp.sq.bufs[ix];
	conn->qp.sq.bufs[ix] = NULL;
	conn->qp.sq.cc++;

	/* Handle backlog still under the spinlock to ensure message post order */
	if (unlikely(!list_empty(&conn->qp.sq.backlog))) {
		if (likely(conn->qp.active)) {
			nextbuf = list_first_entry(&conn->qp.sq.backlog,
						   struct mlx5_fpga_dma_buf, list);
			list_del(&nextbuf->list);
			mlx5_fpga_conn_post_send(conn, nextbuf);
		}
	}

	spin_unlock_irqrestore(&conn->qp.sq.lock, flags);

	if (unlikely(status && (status != MLX5_CQE_SYNDROME_WR_FLUSH_ERR)))
		mlx5_fpga_warn(conn->fdev, "SQ buf %p on FPGA QP %u completion status %d\n",
			       buf, conn->fpga_qpn, status);
	else
		mlx5_fpga_dbg(conn->fdev, "SQ buf %p on FPGA QP %u completion status %d\n",
			      buf, conn->fpga_qpn, status);

	mlx5_fpga_conn_unmap_buf(conn, buf);

	if (likely(buf->complete))
		buf->complete(conn, conn->fdev, buf, status);

	if (unlikely(status))
		conn->qp.active = false;
}

static void mlx5_fpga_conn_handle_cqe(struct mlx5_fpga_conn *conn,
				      struct mlx5_cqe64 *cqe)
{
	u8 opcode, status = 0;

	opcode = get_cqe_opcode(cqe);

	switch (opcode) {
	case MLX5_CQE_REQ_ERR:
		status = ((struct mlx5_err_cqe *)cqe)->syndrome;
		/* Fall through */
	case MLX5_CQE_REQ:
		mlx5_fpga_conn_sq_cqe(conn, cqe, status);
		break;

	case MLX5_CQE_RESP_ERR:
		status = ((struct mlx5_err_cqe *)cqe)->syndrome;
		/* Fall through */
	case MLX5_CQE_RESP_SEND:
		mlx5_fpga_conn_rq_cqe(conn, cqe, status);
		break;
	default:
		mlx5_fpga_warn(conn->fdev, "Unexpected cqe opcode %u\n",
			       opcode);
	}
}

static void mlx5_fpga_conn_arm_cq(struct mlx5_fpga_conn *conn)
{
	mlx5_cq_arm(&conn->cq.mcq, MLX5_CQ_DB_REQ_NOT,
		    conn->fdev->conn_res.uar->map, conn->cq.wq.cc);
}

static void mlx5_fpga_conn_cq_event(struct mlx5_core_cq *mcq,
				    enum mlx5_event event)
{
	struct mlx5_fpga_conn *conn;

	conn = container_of(mcq, struct mlx5_fpga_conn, cq.mcq);
	mlx5_fpga_warn(conn->fdev, "CQ event %u on CQ #%u\n", event, mcq->cqn);
}

static void mlx5_fpga_conn_event(struct mlx5_core_qp *mqp, int event)
{
	struct mlx5_fpga_conn *conn;

	conn = container_of(mqp, struct mlx5_fpga_conn, qp.mqp);
	mlx5_fpga_warn(conn->fdev, "QP event %u on QP #%u\n", event, mqp->qpn);
}

static inline void mlx5_fpga_conn_cqes(struct mlx5_fpga_conn *conn,
				       unsigned int budget)
{
	struct mlx5_cqe64 *cqe;

	while (budget) {
		cqe = mlx5_cqwq_get_cqe(&conn->cq.wq);
		if (!cqe)
			break;

		budget--;
		mlx5_cqwq_pop(&conn->cq.wq);
		mlx5_fpga_conn_handle_cqe(conn, cqe);
		mlx5_cqwq_update_db_record(&conn->cq.wq);
	}
	if (!budget) {
		tasklet_schedule(&conn->cq.tasklet);
		return;
	}

	mlx5_fpga_dbg(conn->fdev, "Re-arming CQ with cc# %u\n", conn->cq.wq.cc);
	/* ensure cq space is freed before enabling more cqes */
	wmb();
	mlx5_fpga_conn_arm_cq(conn);
}

static void mlx5_fpga_conn_cq_tasklet(unsigned long data)
{
	struct mlx5_fpga_conn *conn = (void *)data;

	if (unlikely(!conn->qp.active))
		return;
	mlx5_fpga_conn_cqes(conn, MLX5_FPGA_CQ_BUDGET);
}

static void mlx5_fpga_conn_cq_complete(struct mlx5_core_cq *mcq)
{
	struct mlx5_fpga_conn *conn;

	conn = container_of(mcq, struct mlx5_fpga_conn, cq.mcq);
	if (unlikely(!conn->qp.active))
		return;
	mlx5_fpga_conn_cqes(conn, MLX5_FPGA_CQ_BUDGET);
}

static int mlx5_fpga_conn_create_cq(struct mlx5_fpga_conn *conn, int cq_size)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	u32 temp_cqc[MLX5_ST_SZ_DW(cqc)] = {0};
	struct mlx5_wq_param wqp;
	struct mlx5_cqe64 *cqe;
	int inlen, err, eqn;
	unsigned int irqn;
	void *cqc, *in;
	__be64 *pas;
	u32 i;

	cq_size = roundup_pow_of_two(cq_size);
	MLX5_SET(cqc, temp_cqc, log_cq_size, ilog2(cq_size));

	wqp.buf_numa_node = mdev->priv.numa_node;
	wqp.db_numa_node  = mdev->priv.numa_node;

	err = mlx5_cqwq_create(mdev, &wqp, temp_cqc, &conn->cq.wq,
			       &conn->cq.wq_ctrl);
	if (err)
		return err;

	for (i = 0; i < mlx5_cqwq_get_size(&conn->cq.wq); i++) {
		cqe = mlx5_cqwq_get_wqe(&conn->cq.wq, i);
		cqe->op_own = MLX5_CQE_INVALID << 4 | MLX5_CQE_OWNER_MASK;
	}

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * conn->cq.wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_cqwq;
	}

	err = mlx5_vector2eqn(mdev, smp_processor_id(), &eqn, &irqn);
	if (err)
		goto err_cqwq;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, log_cq_size, ilog2(cq_size));
	MLX5_SET(cqc, cqc, c_eqn, eqn);
	MLX5_SET(cqc, cqc, uar_page, fdev->conn_res.uar->index);
	MLX5_SET(cqc, cqc, log_page_size, conn->cq.wq_ctrl.buf.page_shift -
			   MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr, conn->cq.wq_ctrl.db.dma);

	pas = (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas);
	mlx5_fill_page_frag_array(&conn->cq.wq_ctrl.buf, pas);

	err = mlx5_core_create_cq(mdev, &conn->cq.mcq, in, inlen);
	kvfree(in);

	if (err)
		goto err_cqwq;

	conn->cq.mcq.cqe_sz     = 64;
	conn->cq.mcq.set_ci_db  = conn->cq.wq_ctrl.db.db;
	conn->cq.mcq.arm_db     = conn->cq.wq_ctrl.db.db + 1;
	*conn->cq.mcq.set_ci_db = 0;
	*conn->cq.mcq.arm_db    = 0;
	conn->cq.mcq.vector     = 0;
	conn->cq.mcq.comp       = mlx5_fpga_conn_cq_complete;
	conn->cq.mcq.event      = mlx5_fpga_conn_cq_event;
	conn->cq.mcq.irqn       = irqn;
	conn->cq.mcq.uar        = fdev->conn_res.uar;
	tasklet_init(&conn->cq.tasklet, mlx5_fpga_conn_cq_tasklet,
		     (unsigned long)conn);

	mlx5_fpga_dbg(fdev, "Created CQ #0x%x\n", conn->cq.mcq.cqn);

	goto out;

err_cqwq:
	mlx5_wq_destroy(&conn->cq.wq_ctrl);
out:
	return err;
}

static void mlx5_fpga_conn_destroy_cq(struct mlx5_fpga_conn *conn)
{
	tasklet_disable(&conn->cq.tasklet);
	tasklet_kill(&conn->cq.tasklet);
	mlx5_core_destroy_cq(conn->fdev->mdev, &conn->cq.mcq);
	mlx5_wq_destroy(&conn->cq.wq_ctrl);
}

static int mlx5_fpga_conn_create_wq(struct mlx5_fpga_conn *conn, void *qpc)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	struct mlx5_wq_param wqp;

	wqp.buf_numa_node = mdev->priv.numa_node;
	wqp.db_numa_node  = mdev->priv.numa_node;

	return mlx5_wq_qp_create(mdev, &wqp, qpc, &conn->qp.wq,
				 &conn->qp.wq_ctrl);
}

static int mlx5_fpga_conn_create_qp(struct mlx5_fpga_conn *conn,
				    unsigned int tx_size, unsigned int rx_size)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	u32 temp_qpc[MLX5_ST_SZ_DW(qpc)] = {0};
	void *in = NULL, *qpc;
	int err, inlen;

	conn->qp.rq.pc = 0;
	conn->qp.rq.cc = 0;
	conn->qp.rq.size = roundup_pow_of_two(rx_size);
	conn->qp.sq.pc = 0;
	conn->qp.sq.cc = 0;
	conn->qp.sq.size = roundup_pow_of_two(tx_size);

	MLX5_SET(qpc, temp_qpc, log_rq_stride, ilog2(MLX5_SEND_WQE_DS) - 4);
	MLX5_SET(qpc, temp_qpc, log_rq_size, ilog2(conn->qp.rq.size));
	MLX5_SET(qpc, temp_qpc, log_sq_size, ilog2(conn->qp.sq.size));
	err = mlx5_fpga_conn_create_wq(conn, temp_qpc);
	if (err)
		goto out;

	conn->qp.rq.bufs = kvcalloc(conn->qp.rq.size,
				    sizeof(conn->qp.rq.bufs[0]),
				    GFP_KERNEL);
	if (!conn->qp.rq.bufs) {
		err = -ENOMEM;
		goto err_wq;
	}

	conn->qp.sq.bufs = kvcalloc(conn->qp.sq.size,
				    sizeof(conn->qp.sq.bufs[0]),
				    GFP_KERNEL);
	if (!conn->qp.sq.bufs) {
		err = -ENOMEM;
		goto err_rq_bufs;
	}

	inlen = MLX5_ST_SZ_BYTES(create_qp_in) +
		MLX5_FLD_SZ_BYTES(create_qp_in, pas[0]) *
		conn->qp.wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_sq_bufs;
	}

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, uar_page, fdev->conn_res.uar->index);
	MLX5_SET(qpc, qpc, log_page_size,
		 conn->qp.wq_ctrl.buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(qpc, qpc, fre, 1);
	MLX5_SET(qpc, qpc, rlky, 1);
	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, fdev->conn_res.pdn);
	MLX5_SET(qpc, qpc, log_rq_stride, ilog2(MLX5_SEND_WQE_DS) - 4);
	MLX5_SET(qpc, qpc, log_rq_size, ilog2(conn->qp.rq.size));
	MLX5_SET(qpc, qpc, rq_type, MLX5_NON_ZERO_RQ);
	MLX5_SET(qpc, qpc, log_sq_size, ilog2(conn->qp.sq.size));
	MLX5_SET(qpc, qpc, cqn_snd, conn->cq.mcq.cqn);
	MLX5_SET(qpc, qpc, cqn_rcv, conn->cq.mcq.cqn);
	MLX5_SET64(qpc, qpc, dbr_addr, conn->qp.wq_ctrl.db.dma);
	if (MLX5_CAP_GEN(mdev, cqe_version) == 1)
		MLX5_SET(qpc, qpc, user_index, 0xFFFFFF);

	mlx5_fill_page_frag_array(&conn->qp.wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(create_qp_in, in, pas));

	err = mlx5_core_create_qp(mdev, &conn->qp.mqp, in, inlen);
	if (err)
		goto err_sq_bufs;

	conn->qp.mqp.event = mlx5_fpga_conn_event;
	mlx5_fpga_dbg(fdev, "Created QP #0x%x\n", conn->qp.mqp.qpn);

	goto out;

err_sq_bufs:
	kvfree(conn->qp.sq.bufs);
err_rq_bufs:
	kvfree(conn->qp.rq.bufs);
err_wq:
	mlx5_wq_destroy(&conn->qp.wq_ctrl);
out:
	kvfree(in);
	return err;
}

static void mlx5_fpga_conn_free_recv_bufs(struct mlx5_fpga_conn *conn)
{
	int ix;

	for (ix = 0; ix < conn->qp.rq.size; ix++) {
		if (!conn->qp.rq.bufs[ix])
			continue;
		mlx5_fpga_conn_unmap_buf(conn, conn->qp.rq.bufs[ix]);
		kfree(conn->qp.rq.bufs[ix]);
		conn->qp.rq.bufs[ix] = NULL;
	}
}

static void mlx5_fpga_conn_flush_send_bufs(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_dma_buf *buf, *temp;
	int ix;

	for (ix = 0; ix < conn->qp.sq.size; ix++) {
		buf = conn->qp.sq.bufs[ix];
		if (!buf)
			continue;
		conn->qp.sq.bufs[ix] = NULL;
		mlx5_fpga_conn_unmap_buf(conn, buf);
		if (!buf->complete)
			continue;
		buf->complete(conn, conn->fdev, buf, MLX5_CQE_SYNDROME_WR_FLUSH_ERR);
	}
	list_for_each_entry_safe(buf, temp, &conn->qp.sq.backlog, list) {
		mlx5_fpga_conn_unmap_buf(conn, buf);
		if (!buf->complete)
			continue;
		buf->complete(conn, conn->fdev, buf, MLX5_CQE_SYNDROME_WR_FLUSH_ERR);
	}
}

static void mlx5_fpga_conn_destroy_qp(struct mlx5_fpga_conn *conn)
{
	mlx5_core_destroy_qp(conn->fdev->mdev, &conn->qp.mqp);
	mlx5_fpga_conn_free_recv_bufs(conn);
	mlx5_fpga_conn_flush_send_bufs(conn);
	kvfree(conn->qp.sq.bufs);
	kvfree(conn->qp.rq.bufs);
	mlx5_wq_destroy(&conn->qp.wq_ctrl);
}

static inline int mlx5_fpga_conn_reset_qp(struct mlx5_fpga_conn *conn)
{
	struct mlx5_core_dev *mdev = conn->fdev->mdev;

	mlx5_fpga_dbg(conn->fdev, "Modifying QP %u to RST\n", conn->qp.mqp.qpn);

	return mlx5_core_qp_modify(mdev, MLX5_CMD_OP_2RST_QP, 0, NULL,
				   &conn->qp.mqp);
}

static inline int mlx5_fpga_conn_init_qp(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	u32 *qpc = NULL;
	int err;

	mlx5_fpga_dbg(conn->fdev, "Modifying QP %u to INIT\n", conn->qp.mqp.qpn);

	qpc = kzalloc(MLX5_ST_SZ_BYTES(qpc), GFP_KERNEL);
	if (!qpc) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, primary_address_path.pkey_index, MLX5_FPGA_PKEY_INDEX);
	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, MLX5_FPGA_PORT_NUM);
	MLX5_SET(qpc, qpc, pd, conn->fdev->conn_res.pdn);
	MLX5_SET(qpc, qpc, cqn_snd, conn->cq.mcq.cqn);
	MLX5_SET(qpc, qpc, cqn_rcv, conn->cq.mcq.cqn);
	MLX5_SET64(qpc, qpc, dbr_addr, conn->qp.wq_ctrl.db.dma);

	err = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_RST2INIT_QP, 0, qpc,
				  &conn->qp.mqp);
	if (err) {
		mlx5_fpga_warn(fdev, "qp_modify RST2INIT failed: %d\n", err);
		goto out;
	}

out:
	kfree(qpc);
	return err;
}

static inline int mlx5_fpga_conn_rtr_qp(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	u32 *qpc = NULL;
	int err;

	mlx5_fpga_dbg(conn->fdev, "QP RTR\n");

	qpc = kzalloc(MLX5_ST_SZ_BYTES(qpc), GFP_KERNEL);
	if (!qpc) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(qpc, qpc, mtu, MLX5_QPC_MTU_1K_BYTES);
	MLX5_SET(qpc, qpc, log_msg_max, (u8)MLX5_CAP_GEN(mdev, log_max_msg));
	MLX5_SET(qpc, qpc, remote_qpn, conn->fpga_qpn);
	MLX5_SET(qpc, qpc, next_rcv_psn,
		 MLX5_GET(fpga_qpc, conn->fpga_qpc, next_send_psn));
	MLX5_SET(qpc, qpc, primary_address_path.pkey_index, MLX5_FPGA_PKEY_INDEX);
	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, MLX5_FPGA_PORT_NUM);
	ether_addr_copy(MLX5_ADDR_OF(qpc, qpc, primary_address_path.rmac_47_32),
			MLX5_ADDR_OF(fpga_qpc, conn->fpga_qpc, fpga_mac_47_32));
	MLX5_SET(qpc, qpc, primary_address_path.udp_sport,
		 MLX5_CAP_ROCE(mdev, r_roce_min_src_udp_port));
	MLX5_SET(qpc, qpc, primary_address_path.src_addr_index,
		 conn->qp.sgid_index);
	MLX5_SET(qpc, qpc, primary_address_path.hop_limit, 0);
	memcpy(MLX5_ADDR_OF(qpc, qpc, primary_address_path.rgid_rip),
	       MLX5_ADDR_OF(fpga_qpc, conn->fpga_qpc, fpga_ip),
	       MLX5_FLD_SZ_BYTES(qpc, primary_address_path.rgid_rip));

	err = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_INIT2RTR_QP, 0, qpc,
				  &conn->qp.mqp);
	if (err) {
		mlx5_fpga_warn(fdev, "qp_modify RST2INIT failed: %d\n", err);
		goto out;
	}

out:
	kfree(qpc);
	return err;
}

static inline int mlx5_fpga_conn_rts_qp(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	u32 *qpc = NULL;
	u32 opt_mask;
	int err;

	mlx5_fpga_dbg(conn->fdev, "QP RTS\n");

	qpc = kzalloc(MLX5_ST_SZ_BYTES(qpc), GFP_KERNEL);
	if (!qpc) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(qpc, qpc, log_ack_req_freq, 8);
	MLX5_SET(qpc, qpc, min_rnr_nak, 0x12);
	MLX5_SET(qpc, qpc, primary_address_path.ack_timeout, 0x12); /* ~1.07s */
	MLX5_SET(qpc, qpc, next_send_psn,
		 MLX5_GET(fpga_qpc, conn->fpga_qpc, next_rcv_psn));
	MLX5_SET(qpc, qpc, retry_count, 7);
	MLX5_SET(qpc, qpc, rnr_retry, 7); /* Infinite retry if RNR NACK */

	opt_mask = MLX5_QP_OPTPAR_RNR_TIMEOUT;
	err = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_RTR2RTS_QP, opt_mask, qpc,
				  &conn->qp.mqp);
	if (err) {
		mlx5_fpga_warn(fdev, "qp_modify RST2INIT failed: %d\n", err);
		goto out;
	}

out:
	kfree(qpc);
	return err;
}

static int mlx5_fpga_conn_connect(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	int err;

	MLX5_SET(fpga_qpc, conn->fpga_qpc, state, MLX5_FPGA_QPC_STATE_ACTIVE);
	err = mlx5_fpga_modify_qp(conn->fdev->mdev, conn->fpga_qpn,
				  MLX5_FPGA_QPC_STATE, &conn->fpga_qpc);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to activate FPGA RC QP: %d\n", err);
		goto out;
	}

	err = mlx5_fpga_conn_reset_qp(conn);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to change QP state to reset\n");
		goto err_fpga_qp;
	}

	err = mlx5_fpga_conn_init_qp(conn);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to modify QP from RESET to INIT\n");
		goto err_fpga_qp;
	}
	conn->qp.active = true;

	while (!mlx5_fpga_conn_post_recv_buf(conn))
		;

	err = mlx5_fpga_conn_rtr_qp(conn);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to change QP state from INIT to RTR\n");
		goto err_recv_bufs;
	}

	err = mlx5_fpga_conn_rts_qp(conn);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to change QP state from RTR to RTS\n");
		goto err_recv_bufs;
	}
	goto out;

err_recv_bufs:
	mlx5_fpga_conn_free_recv_bufs(conn);
err_fpga_qp:
	MLX5_SET(fpga_qpc, conn->fpga_qpc, state, MLX5_FPGA_QPC_STATE_INIT);
	if (mlx5_fpga_modify_qp(conn->fdev->mdev, conn->fpga_qpn,
				MLX5_FPGA_QPC_STATE, &conn->fpga_qpc))
		mlx5_fpga_err(fdev, "Failed to revert FPGA QP to INIT\n");
out:
	return err;
}

struct mlx5_fpga_conn *mlx5_fpga_conn_create(struct mlx5_fpga_device *fdev,
					     struct mlx5_fpga_conn_attr *attr,
					     enum mlx5_ifc_fpga_qp_type qp_type)
{
	struct mlx5_fpga_conn *ret, *conn;
	u8 *remote_mac, *remote_ip;
	int err;

	if (!attr->recv_cb)
		return ERR_PTR(-EINVAL);

	conn = kzalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn)
		return ERR_PTR(-ENOMEM);

	conn->fdev = fdev;
	INIT_LIST_HEAD(&conn->qp.sq.backlog);

	spin_lock_init(&conn->qp.sq.lock);

	conn->recv_cb = attr->recv_cb;
	conn->cb_arg = attr->cb_arg;

	remote_mac = MLX5_ADDR_OF(fpga_qpc, conn->fpga_qpc, remote_mac_47_32);
	err = mlx5_query_nic_vport_mac_address(fdev->mdev, 0, remote_mac);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to query local MAC: %d\n", err);
		ret = ERR_PTR(err);
		goto err;
	}

	/* Build Modified EUI-64 IPv6 address from the MAC address */
	remote_ip = MLX5_ADDR_OF(fpga_qpc, conn->fpga_qpc, remote_ip);
	remote_ip[0] = 0xfe;
	remote_ip[1] = 0x80;
	addrconf_addr_eui48(&remote_ip[8], remote_mac);

	err = mlx5_core_reserved_gid_alloc(fdev->mdev, &conn->qp.sgid_index);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to allocate SGID: %d\n", err);
		ret = ERR_PTR(err);
		goto err;
	}

	err = mlx5_core_roce_gid_set(fdev->mdev, conn->qp.sgid_index,
				     MLX5_ROCE_VERSION_2,
				     MLX5_ROCE_L3_TYPE_IPV6,
				     remote_ip, remote_mac, true, 0,
				     MLX5_FPGA_PORT_NUM);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to set SGID: %d\n", err);
		ret = ERR_PTR(err);
		goto err_rsvd_gid;
	}
	mlx5_fpga_dbg(fdev, "Reserved SGID index %u\n", conn->qp.sgid_index);

	/* Allow for one cqe per rx/tx wqe, plus one cqe for the next wqe,
	 * created during processing of the cqe
	 */
	err = mlx5_fpga_conn_create_cq(conn,
				       (attr->tx_size + attr->rx_size) * 2);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to create CQ: %d\n", err);
		ret = ERR_PTR(err);
		goto err_gid;
	}

	mlx5_fpga_conn_arm_cq(conn);

	err = mlx5_fpga_conn_create_qp(conn, attr->tx_size, attr->rx_size);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to create QP: %d\n", err);
		ret = ERR_PTR(err);
		goto err_cq;
	}

	MLX5_SET(fpga_qpc, conn->fpga_qpc, state, MLX5_FPGA_QPC_STATE_INIT);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, qp_type, qp_type);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, st, MLX5_FPGA_QPC_ST_RC);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, ether_type, ETH_P_8021Q);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, vid, 0);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, next_rcv_psn, 1);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, next_send_psn, 0);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, pkey, MLX5_FPGA_PKEY);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, remote_qpn, conn->qp.mqp.qpn);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, rnr_retry, 7);
	MLX5_SET(fpga_qpc, conn->fpga_qpc, retry_count, 7);

	err = mlx5_fpga_create_qp(fdev->mdev, &conn->fpga_qpc,
				  &conn->fpga_qpn);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to create FPGA RC QP: %d\n", err);
		ret = ERR_PTR(err);
		goto err_qp;
	}

	err = mlx5_fpga_conn_connect(conn);
	if (err) {
		ret = ERR_PTR(err);
		goto err_conn;
	}

	mlx5_fpga_dbg(fdev, "FPGA QPN is %u\n", conn->fpga_qpn);
	ret = conn;
	goto out;

err_conn:
	mlx5_fpga_destroy_qp(conn->fdev->mdev, conn->fpga_qpn);
err_qp:
	mlx5_fpga_conn_destroy_qp(conn);
err_cq:
	mlx5_fpga_conn_destroy_cq(conn);
err_gid:
	mlx5_core_roce_gid_set(fdev->mdev, conn->qp.sgid_index, 0, 0, NULL,
			       NULL, false, 0, MLX5_FPGA_PORT_NUM);
err_rsvd_gid:
	mlx5_core_reserved_gid_free(fdev->mdev, conn->qp.sgid_index);
err:
	kfree(conn);
out:
	return ret;
}

void mlx5_fpga_conn_destroy(struct mlx5_fpga_conn *conn)
{
	struct mlx5_fpga_device *fdev = conn->fdev;
	struct mlx5_core_dev *mdev = fdev->mdev;
	int err = 0;

	conn->qp.active = false;
	tasklet_disable(&conn->cq.tasklet);
	synchronize_irq(conn->cq.mcq.irqn);

	mlx5_fpga_destroy_qp(conn->fdev->mdev, conn->fpga_qpn);
	err = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_2ERR_QP, 0, NULL,
				  &conn->qp.mqp);
	if (err)
		mlx5_fpga_warn(fdev, "qp_modify 2ERR failed: %d\n", err);
	mlx5_fpga_conn_destroy_qp(conn);
	mlx5_fpga_conn_destroy_cq(conn);

	mlx5_core_roce_gid_set(conn->fdev->mdev, conn->qp.sgid_index, 0, 0,
			       NULL, NULL, false, 0, MLX5_FPGA_PORT_NUM);
	mlx5_core_reserved_gid_free(conn->fdev->mdev, conn->qp.sgid_index);
	kfree(conn);
}

int mlx5_fpga_conn_device_init(struct mlx5_fpga_device *fdev)
{
	int err;

	err = mlx5_nic_vport_enable_roce(fdev->mdev);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to enable RoCE: %d\n", err);
		goto out;
	}

	fdev->conn_res.uar = mlx5_get_uars_page(fdev->mdev);
	if (IS_ERR(fdev->conn_res.uar)) {
		err = PTR_ERR(fdev->conn_res.uar);
		mlx5_fpga_err(fdev, "get_uars_page failed, %d\n", err);
		goto err_roce;
	}
	mlx5_fpga_dbg(fdev, "Allocated UAR index %u\n",
		      fdev->conn_res.uar->index);

	err = mlx5_core_alloc_pd(fdev->mdev, &fdev->conn_res.pdn);
	if (err) {
		mlx5_fpga_err(fdev, "alloc pd failed, %d\n", err);
		goto err_uar;
	}
	mlx5_fpga_dbg(fdev, "Allocated PD %u\n", fdev->conn_res.pdn);

	err = mlx5_fpga_conn_create_mkey(fdev->mdev, fdev->conn_res.pdn,
					 &fdev->conn_res.mkey);
	if (err) {
		mlx5_fpga_err(fdev, "create mkey failed, %d\n", err);
		goto err_dealloc_pd;
	}
	mlx5_fpga_dbg(fdev, "Created mkey 0x%x\n", fdev->conn_res.mkey.key);

	return 0;

err_dealloc_pd:
	mlx5_core_dealloc_pd(fdev->mdev, fdev->conn_res.pdn);
err_uar:
	mlx5_put_uars_page(fdev->mdev, fdev->conn_res.uar);
err_roce:
	mlx5_nic_vport_disable_roce(fdev->mdev);
out:
	return err;
}

void mlx5_fpga_conn_device_cleanup(struct mlx5_fpga_device *fdev)
{
	mlx5_core_destroy_mkey(fdev->mdev, &fdev->conn_res.mkey);
	mlx5_core_dealloc_pd(fdev->mdev, fdev->conn_res.pdn);
	mlx5_put_uars_page(fdev->mdev, fdev->conn_res.uar);
	mlx5_nic_vport_disable_roce(fdev->mdev);
}
