/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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


#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/mlx5/cmd.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/transobj.h>

#include "mlx5_core.h"

static struct mlx5_core_rsc_common *mlx5_get_rsc(struct mlx5_core_dev *dev,
						 u32 rsn)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	struct mlx5_core_rsc_common *common;

	spin_lock(&table->lock);

	common = radix_tree_lookup(&table->tree, rsn);
	if (common)
		atomic_inc(&common->refcount);

	spin_unlock(&table->lock);

	if (!common) {
		mlx5_core_warn(dev, "Async event for bogus resource 0x%x\n",
			       rsn);
		return NULL;
	}
	return common;
}

void mlx5_core_put_rsc(struct mlx5_core_rsc_common *common)
{
	if (atomic_dec_and_test(&common->refcount))
		complete(&common->free);
}

static u64 qp_allowed_event_types(void)
{
	u64 mask;

	mask = BIT(MLX5_EVENT_TYPE_PATH_MIG) |
	       BIT(MLX5_EVENT_TYPE_COMM_EST) |
	       BIT(MLX5_EVENT_TYPE_SQ_DRAINED) |
	       BIT(MLX5_EVENT_TYPE_SRQ_LAST_WQE) |
	       BIT(MLX5_EVENT_TYPE_WQ_CATAS_ERROR) |
	       BIT(MLX5_EVENT_TYPE_PATH_MIG_FAILED) |
	       BIT(MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR) |
	       BIT(MLX5_EVENT_TYPE_WQ_ACCESS_ERROR);

	return mask;
}

static u64 rq_allowed_event_types(void)
{
	u64 mask;

	mask = BIT(MLX5_EVENT_TYPE_SRQ_LAST_WQE) |
	       BIT(MLX5_EVENT_TYPE_WQ_CATAS_ERROR);

	return mask;
}

static u64 sq_allowed_event_types(void)
{
	return BIT(MLX5_EVENT_TYPE_WQ_CATAS_ERROR);
}

static bool is_event_type_allowed(int rsc_type, int event_type)
{
	switch (rsc_type) {
	case MLX5_EVENT_QUEUE_TYPE_QP:
		return BIT(event_type) & qp_allowed_event_types();
	case MLX5_EVENT_QUEUE_TYPE_RQ:
		return BIT(event_type) & rq_allowed_event_types();
	case MLX5_EVENT_QUEUE_TYPE_SQ:
		return BIT(event_type) & sq_allowed_event_types();
	default:
		WARN(1, "Event arrived for unknown resource type");
		return false;
	}
}

void mlx5_rsc_event(struct mlx5_core_dev *dev, u32 rsn, int event_type)
{
	struct mlx5_core_rsc_common *common = mlx5_get_rsc(dev, rsn);
	struct mlx5_core_qp *qp;

	if (!common)
		return;

	if (!is_event_type_allowed((rsn >> MLX5_USER_INDEX_LEN), event_type)) {
		mlx5_core_warn(dev, "event 0x%.2x is not allowed on resource 0x%.8x\n",
			       event_type, rsn);
		return;
	}

	switch (common->res) {
	case MLX5_RES_QP:
	case MLX5_RES_RQ:
	case MLX5_RES_SQ:
		qp = (struct mlx5_core_qp *)common;
		qp->event(qp, event_type);
		break;

	default:
		mlx5_core_warn(dev, "invalid resource type for 0x%x\n", rsn);
	}

	mlx5_core_put_rsc(common);
}

static int create_qprqsq_common(struct mlx5_core_dev *dev,
				struct mlx5_core_qp *qp,
				int rsc_type)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	int err;

	qp->common.res = rsc_type;
	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree,
				qp->qpn | (rsc_type << MLX5_USER_INDEX_LEN),
				qp);
	spin_unlock_irq(&table->lock);
	if (err)
		return err;

	atomic_set(&qp->common.refcount, 1);
	init_completion(&qp->common.free);
	qp->pid = current->pid;

	return 0;
}

static void destroy_qprqsq_common(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *qp)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	unsigned long flags;

	spin_lock_irqsave(&table->lock, flags);
	radix_tree_delete(&table->tree,
			  qp->qpn | (qp->common.res << MLX5_USER_INDEX_LEN));
	spin_unlock_irqrestore(&table->lock, flags);
	mlx5_core_put_rsc((struct mlx5_core_rsc_common *)qp);
	wait_for_completion(&qp->common.free);
}

int mlx5_core_create_qp(struct mlx5_core_dev *dev,
			struct mlx5_core_qp *qp,
			u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {0};
	u32 dout[MLX5_ST_SZ_DW(destroy_qp_out)];
	u32 din[MLX5_ST_SZ_DW(destroy_qp_in)];
	int err;

	MLX5_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		return err;

	qp->qpn = MLX5_GET(create_qp_out, out, qpn);
	mlx5_core_dbg(dev, "qpn = 0x%x\n", qp->qpn);

	err = create_qprqsq_common(dev, qp, MLX5_RES_QP);
	if (err)
		goto err_cmd;

	err = mlx5_debug_qp_add(dev, qp);
	if (err)
		mlx5_core_dbg(dev, "failed adding QP 0x%x to debug file system\n",
			      qp->qpn);

	atomic_inc(&dev->num_qps);

	return 0;

err_cmd:
	memset(din, 0, sizeof(din));
	memset(dout, 0, sizeof(dout));
	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, qp->qpn);
	mlx5_cmd_exec(dev, din, sizeof(din), dout, sizeof(dout));
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_create_qp);

int mlx5_core_destroy_qp(struct mlx5_core_dev *dev,
			 struct mlx5_core_qp *qp)
{
	u32 out[MLX5_ST_SZ_DW(destroy_qp_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_qp_in)]   = {0};
	int err;

	mlx5_debug_qp_remove(dev, qp);

	destroy_qprqsq_common(dev, qp);

	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, qp->qpn);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	atomic_dec(&dev->num_qps);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_core_destroy_qp);

struct mbox_info {
	u32 *in;
	u32 *out;
	int inlen;
	int outlen;
};

static int mbox_alloc(struct mbox_info *mbox, int inlen, int outlen)
{
	mbox->inlen  = inlen;
	mbox->outlen = outlen;
	mbox->in = kzalloc(mbox->inlen, GFP_KERNEL);
	mbox->out = kzalloc(mbox->outlen, GFP_KERNEL);
	if (!mbox->in || !mbox->out) {
		kfree(mbox->in);
		kfree(mbox->out);
		return -ENOMEM;
	}

	return 0;
}

static void mbox_free(struct mbox_info *mbox)
{
	kfree(mbox->in);
	kfree(mbox->out);
}

static int modify_qp_mbox_alloc(struct mlx5_core_dev *dev, u16 opcode, int qpn,
				u32 opt_param_mask, void *qpc,
				struct mbox_info *mbox)
{
	mbox->out = NULL;
	mbox->in = NULL;

#define MBOX_ALLOC(mbox, typ)  \
	mbox_alloc(mbox, MLX5_ST_SZ_BYTES(typ##_in), MLX5_ST_SZ_BYTES(typ##_out))

#define MOD_QP_IN_SET(typ, in, _opcode, _qpn) \
	MLX5_SET(typ##_in, in, opcode, _opcode); \
	MLX5_SET(typ##_in, in, qpn, _qpn)

#define MOD_QP_IN_SET_QPC(typ, in, _opcode, _qpn, _opt_p, _qpc) \
	MOD_QP_IN_SET(typ, in, _opcode, _qpn); \
	MLX5_SET(typ##_in, in, opt_param_mask, _opt_p); \
	memcpy(MLX5_ADDR_OF(typ##_in, in, qpc), _qpc, MLX5_ST_SZ_BYTES(qpc))

	switch (opcode) {
	/* 2RST & 2ERR */
	case MLX5_CMD_OP_2RST_QP:
		if (MBOX_ALLOC(mbox, qp_2rst))
			return -ENOMEM;
		MOD_QP_IN_SET(qp_2rst, mbox->in, opcode, qpn);
		break;
	case MLX5_CMD_OP_2ERR_QP:
		if (MBOX_ALLOC(mbox, qp_2err))
			return -ENOMEM;
		MOD_QP_IN_SET(qp_2err, mbox->in, opcode, qpn);
		break;

	/* MODIFY with QPC */
	case MLX5_CMD_OP_RST2INIT_QP:
		if (MBOX_ALLOC(mbox, rst2init_qp))
			return -ENOMEM;
		 MOD_QP_IN_SET_QPC(rst2init_qp, mbox->in, opcode, qpn,
				   opt_param_mask, qpc);
		 break;
	case MLX5_CMD_OP_INIT2RTR_QP:
		if (MBOX_ALLOC(mbox, init2rtr_qp))
			return -ENOMEM;
		 MOD_QP_IN_SET_QPC(init2rtr_qp, mbox->in, opcode, qpn,
				   opt_param_mask, qpc);
		 break;
	case MLX5_CMD_OP_RTR2RTS_QP:
		if (MBOX_ALLOC(mbox, rtr2rts_qp))
			return -ENOMEM;
		 MOD_QP_IN_SET_QPC(rtr2rts_qp, mbox->in, opcode, qpn,
				   opt_param_mask, qpc);
		 break;
	case MLX5_CMD_OP_RTS2RTS_QP:
		if (MBOX_ALLOC(mbox, rts2rts_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(rts2rts_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_SQERR2RTS_QP:
		if (MBOX_ALLOC(mbox, sqerr2rts_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(sqerr2rts_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_INIT2INIT_QP:
		if (MBOX_ALLOC(mbox, init2init_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(init2init_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	default:
		mlx5_core_err(dev, "Unknown transition for modify QP: OP(0x%x) QPN(0x%x)\n",
			      opcode, qpn);
		return -EINVAL;
	}
	return 0;
}

int mlx5_core_qp_modify(struct mlx5_core_dev *dev, u16 opcode,
			u32 opt_param_mask, void *qpc,
			struct mlx5_core_qp *qp)
{
	struct mbox_info mbox;
	int err;

	err = modify_qp_mbox_alloc(dev, opcode, qp->qpn,
				   opt_param_mask, qpc, &mbox);
	if (err)
		return err;

	err = mlx5_cmd_exec(dev, mbox.in, mbox.inlen, mbox.out, mbox.outlen);
	mbox_free(&mbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_qp_modify);

void mlx5_init_qp_table(struct mlx5_core_dev *dev)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;

	memset(table, 0, sizeof(*table));
	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
	mlx5_qp_debugfs_init(dev);
}

void mlx5_cleanup_qp_table(struct mlx5_core_dev *dev)
{
	mlx5_qp_debugfs_cleanup(dev);
}

int mlx5_core_qp_query(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp,
		       u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_qp_in)] = {0};

	MLX5_SET(query_qp_in, in, opcode, MLX5_CMD_OP_QUERY_QP);
	MLX5_SET(query_qp_in, in, qpn, qp->qpn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}
EXPORT_SYMBOL_GPL(mlx5_core_qp_query);

int mlx5_core_xrcd_alloc(struct mlx5_core_dev *dev, u32 *xrcdn)
{
	u32 out[MLX5_ST_SZ_DW(alloc_xrcd_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(alloc_xrcd_in)]   = {0};
	int err;

	MLX5_SET(alloc_xrcd_in, in, opcode, MLX5_CMD_OP_ALLOC_XRCD);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*xrcdn = MLX5_GET(alloc_xrcd_out, out, xrcd);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_xrcd_alloc);

int mlx5_core_xrcd_dealloc(struct mlx5_core_dev *dev, u32 xrcdn)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_xrcd_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(dealloc_xrcd_in)]   = {0};

	MLX5_SET(dealloc_xrcd_in, in, opcode, MLX5_CMD_OP_DEALLOC_XRCD);
	MLX5_SET(dealloc_xrcd_in, in, xrcd, xrcdn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_core_xrcd_dealloc);

int mlx5_core_create_rq_tracked(struct mlx5_core_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *rq)
{
	int err;
	u32 rqn;

	err = mlx5_core_create_rq(dev, in, inlen, &rqn);
	if (err)
		return err;

	rq->qpn = rqn;
	err = create_qprqsq_common(dev, rq, MLX5_RES_RQ);
	if (err)
		goto err_destroy_rq;

	return 0;

err_destroy_rq:
	mlx5_core_destroy_rq(dev, rq->qpn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_rq_tracked);

void mlx5_core_destroy_rq_tracked(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *rq)
{
	destroy_qprqsq_common(dev, rq);
	mlx5_core_destroy_rq(dev, rq->qpn);
}
EXPORT_SYMBOL(mlx5_core_destroy_rq_tracked);

int mlx5_core_create_sq_tracked(struct mlx5_core_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *sq)
{
	int err;
	u32 sqn;

	err = mlx5_core_create_sq(dev, in, inlen, &sqn);
	if (err)
		return err;

	sq->qpn = sqn;
	err = create_qprqsq_common(dev, sq, MLX5_RES_SQ);
	if (err)
		goto err_destroy_sq;

	return 0;

err_destroy_sq:
	mlx5_core_destroy_sq(dev, sq->qpn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_sq_tracked);

void mlx5_core_destroy_sq_tracked(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *sq)
{
	destroy_qprqsq_common(dev, sq);
	mlx5_core_destroy_sq(dev, sq->qpn);
}
EXPORT_SYMBOL(mlx5_core_destroy_sq_tracked);

int mlx5_core_alloc_q_counter(struct mlx5_core_dev *dev, u16 *counter_id)
{
	u32 in[MLX5_ST_SZ_DW(alloc_q_counter_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(alloc_q_counter_out)] = {0};
	int err;

	MLX5_SET(alloc_q_counter_in, in, opcode, MLX5_CMD_OP_ALLOC_Q_COUNTER);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*counter_id = MLX5_GET(alloc_q_counter_out, out,
				       counter_set_id);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_alloc_q_counter);

int mlx5_core_dealloc_q_counter(struct mlx5_core_dev *dev, u16 counter_id)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_q_counter_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(dealloc_q_counter_out)] = {0};

	MLX5_SET(dealloc_q_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_Q_COUNTER);
	MLX5_SET(dealloc_q_counter_in, in, counter_set_id, counter_id);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_core_dealloc_q_counter);

int mlx5_core_query_q_counter(struct mlx5_core_dev *dev, u16 counter_id,
			      int reset, void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_q_counter_in)] = {0};

	MLX5_SET(query_q_counter_in, in, opcode, MLX5_CMD_OP_QUERY_Q_COUNTER);
	MLX5_SET(query_q_counter_in, in, clear, reset);
	MLX5_SET(query_q_counter_in, in, counter_set_id, counter_id);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}
EXPORT_SYMBOL_GPL(mlx5_core_query_q_counter);

int mlx5_core_query_out_of_buffer(struct mlx5_core_dev *dev, u16 counter_id,
				  u32 *out_of_buffer)
{
	int outlen = MLX5_ST_SZ_BYTES(query_q_counter_out);
	void *out;
	int err;

	out = mlx5_vzalloc(outlen);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_query_q_counter(dev, counter_id, 0, out, outlen);
	if (!err)
		*out_of_buffer = MLX5_GET(query_q_counter_out, out,
					  out_of_buffer);

	kfree(out);
	return err;
}
