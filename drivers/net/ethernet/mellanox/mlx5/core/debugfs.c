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

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "lib/eq.h"

enum {
	QP_PID,
	QP_STATE,
	QP_XPORT,
	QP_MTU,
	QP_N_RECV,
	QP_RECV_SZ,
	QP_N_SEND,
	QP_LOG_PG_SZ,
	QP_RQPN,
};

static char *qp_fields[] = {
	[QP_PID]	= "pid",
	[QP_STATE]	= "state",
	[QP_XPORT]	= "transport",
	[QP_MTU]	= "mtu",
	[QP_N_RECV]	= "num_recv",
	[QP_RECV_SZ]	= "rcv_wqe_sz",
	[QP_N_SEND]	= "num_send",
	[QP_LOG_PG_SZ]	= "log2_page_sz",
	[QP_RQPN]	= "remote_qpn",
};

enum {
	EQ_NUM_EQES,
	EQ_INTR,
	EQ_LOG_PG_SZ,
};

static char *eq_fields[] = {
	[EQ_NUM_EQES]	= "num_eqes",
	[EQ_INTR]	= "intr",
	[EQ_LOG_PG_SZ]	= "log_page_size",
};

enum {
	CQ_PID,
	CQ_NUM_CQES,
	CQ_LOG_PG_SZ,
};

static char *cq_fields[] = {
	[CQ_PID]	= "pid",
	[CQ_NUM_CQES]	= "num_cqes",
	[CQ_LOG_PG_SZ]	= "log_page_size",
};

struct dentry *mlx5_debugfs_root;
EXPORT_SYMBOL(mlx5_debugfs_root);

void mlx5_register_debugfs(void)
{
	mlx5_debugfs_root = debugfs_create_dir("mlx5", NULL);
}

void mlx5_unregister_debugfs(void)
{
	debugfs_remove(mlx5_debugfs_root);
}

void mlx5_qp_debugfs_init(struct mlx5_core_dev *dev)
{
	dev->priv.qp_debugfs = debugfs_create_dir("QPs",  dev->priv.dbg_root);
}
EXPORT_SYMBOL(mlx5_qp_debugfs_init);

void mlx5_qp_debugfs_cleanup(struct mlx5_core_dev *dev)
{
	debugfs_remove_recursive(dev->priv.qp_debugfs);
}
EXPORT_SYMBOL(mlx5_qp_debugfs_cleanup);

void mlx5_eq_debugfs_init(struct mlx5_core_dev *dev)
{
	dev->priv.eq_debugfs = debugfs_create_dir("EQs",  dev->priv.dbg_root);
}

void mlx5_eq_debugfs_cleanup(struct mlx5_core_dev *dev)
{
	debugfs_remove_recursive(dev->priv.eq_debugfs);
}

static ssize_t average_read(struct file *filp, char __user *buf, size_t count,
			    loff_t *pos)
{
	struct mlx5_cmd_stats *stats;
	u64 field = 0;
	int ret;
	char tbuf[22];

	stats = filp->private_data;
	spin_lock_irq(&stats->lock);
	if (stats->n)
		field = div64_u64(stats->sum, stats->n);
	spin_unlock_irq(&stats->lock);
	ret = snprintf(tbuf, sizeof(tbuf), "%llu\n", field);
	return simple_read_from_buffer(buf, count, pos, tbuf, ret);
}

static ssize_t average_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct mlx5_cmd_stats *stats;

	stats = filp->private_data;
	spin_lock_irq(&stats->lock);
	stats->sum = 0;
	stats->n = 0;
	spin_unlock_irq(&stats->lock);

	*pos += count;

	return count;
}

static const struct file_operations stats_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.read	= average_read,
	.write	= average_write,
};

void mlx5_cmdif_debugfs_init(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd_stats *stats;
	struct dentry **cmd;
	const char *namep;
	int i;

	cmd = &dev->priv.cmdif_debugfs;
	*cmd = debugfs_create_dir("commands", dev->priv.dbg_root);

	for (i = 0; i < MLX5_CMD_OP_MAX; i++) {
		stats = &dev->cmd.stats[i];
		namep = mlx5_command_str(i);
		if (strcmp(namep, "unknown command opcode")) {
			stats->root = debugfs_create_dir(namep, *cmd);

			debugfs_create_file("average", 0400, stats->root, stats,
					    &stats_fops);
			debugfs_create_u64("n", 0400, stats->root, &stats->n);
		}
	}
}

void mlx5_cmdif_debugfs_cleanup(struct mlx5_core_dev *dev)
{
	debugfs_remove_recursive(dev->priv.cmdif_debugfs);
}

void mlx5_cq_debugfs_init(struct mlx5_core_dev *dev)
{
	dev->priv.cq_debugfs = debugfs_create_dir("CQs",  dev->priv.dbg_root);
}

void mlx5_cq_debugfs_cleanup(struct mlx5_core_dev *dev)
{
	debugfs_remove_recursive(dev->priv.cq_debugfs);
}

static u64 qp_read_field(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp,
			 int index, int *is_str)
{
	int outlen = MLX5_ST_SZ_BYTES(query_qp_out);
	u32 in[MLX5_ST_SZ_DW(query_qp_in)] = {};
	u64 param = 0;
	u32 *out;
	int state;
	u32 *qpc;
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return 0;

	MLX5_SET(query_qp_in, in, opcode, MLX5_CMD_OP_QUERY_QP);
	MLX5_SET(query_qp_in, in, qpn, qp->qpn);
	err = mlx5_cmd_exec_inout(dev, query_qp, in, out);
	if (err)
		goto out;

	*is_str = 0;

	qpc = MLX5_ADDR_OF(query_qp_out, out, qpc);
	switch (index) {
	case QP_PID:
		param = qp->pid;
		break;
	case QP_STATE:
		state = MLX5_GET(qpc, qpc, state);
		param = (unsigned long)mlx5_qp_state_str(state);
		*is_str = 1;
		break;
	case QP_XPORT:
		param = (unsigned long)mlx5_qp_type_str(MLX5_GET(qpc, qpc, st));
		*is_str = 1;
		break;
	case QP_MTU:
		switch (MLX5_GET(qpc, qpc, mtu)) {
		case IB_MTU_256:
			param = 256;
			break;
		case IB_MTU_512:
			param = 512;
			break;
		case IB_MTU_1024:
			param = 1024;
			break;
		case IB_MTU_2048:
			param = 2048;
			break;
		case IB_MTU_4096:
			param = 4096;
			break;
		default:
			param = 0;
		}
		break;
	case QP_N_RECV:
		param = 1 << MLX5_GET(qpc, qpc, log_rq_size);
		break;
	case QP_RECV_SZ:
		param = 1 << (MLX5_GET(qpc, qpc, log_rq_stride) + 4);
		break;
	case QP_N_SEND:
		if (!MLX5_GET(qpc, qpc, no_sq))
			param = 1 << MLX5_GET(qpc, qpc, log_sq_size);
		break;
	case QP_LOG_PG_SZ:
		param = MLX5_GET(qpc, qpc, log_page_size) + 12;
		break;
	case QP_RQPN:
		param = MLX5_GET(qpc, qpc, remote_qpn);
		break;
	}
out:
	kfree(out);
	return param;
}

static u64 eq_read_field(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
			 int index)
{
	int outlen = MLX5_ST_SZ_BYTES(query_eq_out);
	u32 in[MLX5_ST_SZ_DW(query_eq_in)] = {};
	u64 param = 0;
	void *ctx;
	u32 *out;
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return param;

	MLX5_SET(query_eq_in, in, opcode, MLX5_CMD_OP_QUERY_EQ);
	MLX5_SET(query_eq_in, in, eq_number, eq->eqn);
	err = mlx5_cmd_exec_inout(dev, query_eq, in, out);
	if (err) {
		mlx5_core_warn(dev, "failed to query eq\n");
		goto out;
	}
	ctx = MLX5_ADDR_OF(query_eq_out, out, eq_context_entry);

	switch (index) {
	case EQ_NUM_EQES:
		param = 1 << MLX5_GET(eqc, ctx, log_eq_size);
		break;
	case EQ_INTR:
		param = MLX5_GET(eqc, ctx, intr);
		break;
	case EQ_LOG_PG_SZ:
		param = MLX5_GET(eqc, ctx, log_page_size) + 12;
		break;
	}

out:
	kfree(out);
	return param;
}

static u64 cq_read_field(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			 int index)
{
	int outlen = MLX5_ST_SZ_BYTES(query_cq_out);
	u64 param = 0;
	void *ctx;
	u32 *out;
	int err;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return param;

	err = mlx5_core_query_cq(dev, cq, out);
	if (err) {
		mlx5_core_warn(dev, "failed to query cq\n");
		goto out;
	}
	ctx = MLX5_ADDR_OF(query_cq_out, out, cq_context);

	switch (index) {
	case CQ_PID:
		param = cq->pid;
		break;
	case CQ_NUM_CQES:
		param = 1 << MLX5_GET(cqc, ctx, log_cq_size);
		break;
	case CQ_LOG_PG_SZ:
		param = MLX5_GET(cqc, ctx, log_page_size);
		break;
	}

out:
	kvfree(out);
	return param;
}

static ssize_t dbg_read(struct file *filp, char __user *buf, size_t count,
			loff_t *pos)
{
	struct mlx5_field_desc *desc;
	struct mlx5_rsc_debug *d;
	char tbuf[18];
	int is_str = 0;
	u64 field;
	int ret;

	desc = filp->private_data;
	d = (void *)(desc - desc->i) - sizeof(*d);
	switch (d->type) {
	case MLX5_DBG_RSC_QP:
		field = qp_read_field(d->dev, d->object, desc->i, &is_str);
		break;

	case MLX5_DBG_RSC_EQ:
		field = eq_read_field(d->dev, d->object, desc->i);
		break;

	case MLX5_DBG_RSC_CQ:
		field = cq_read_field(d->dev, d->object, desc->i);
		break;

	default:
		mlx5_core_warn(d->dev, "invalid resource type %d\n", d->type);
		return -EINVAL;
	}

	if (is_str)
		ret = snprintf(tbuf, sizeof(tbuf), "%s\n", (const char *)(unsigned long)field);
	else
		ret = snprintf(tbuf, sizeof(tbuf), "0x%llx\n", field);

	return simple_read_from_buffer(buf, count, pos, tbuf, ret);
}

static const struct file_operations fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.read	= dbg_read,
};

static int add_res_tree(struct mlx5_core_dev *dev, enum dbg_rsc_type type,
			struct dentry *root, struct mlx5_rsc_debug **dbg,
			int rsn, char **field, int nfile, void *data)
{
	struct mlx5_rsc_debug *d;
	char resn[32];
	int i;

	d = kzalloc(struct_size(d, fields, nfile), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->dev = dev;
	d->object = data;
	d->type = type;
	sprintf(resn, "0x%x", rsn);
	d->root = debugfs_create_dir(resn,  root);

	for (i = 0; i < nfile; i++) {
		d->fields[i].i = i;
		debugfs_create_file(field[i], 0400, d->root, &d->fields[i],
				    &fops);
	}
	*dbg = d;

	return 0;
}

static void rem_res_tree(struct mlx5_rsc_debug *d)
{
	debugfs_remove_recursive(d->root);
	kfree(d);
}

int mlx5_debug_qp_add(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp)
{
	int err;

	if (!mlx5_debugfs_root)
		return 0;

	err = add_res_tree(dev, MLX5_DBG_RSC_QP, dev->priv.qp_debugfs,
			   &qp->dbg, qp->qpn, qp_fields,
			   ARRAY_SIZE(qp_fields), qp);
	if (err)
		qp->dbg = NULL;

	return err;
}
EXPORT_SYMBOL(mlx5_debug_qp_add);

void mlx5_debug_qp_remove(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp)
{
	if (!mlx5_debugfs_root)
		return;

	if (qp->dbg)
		rem_res_tree(qp->dbg);
}
EXPORT_SYMBOL(mlx5_debug_qp_remove);

int mlx5_debug_eq_add(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	int err;

	if (!mlx5_debugfs_root)
		return 0;

	err = add_res_tree(dev, MLX5_DBG_RSC_EQ, dev->priv.eq_debugfs,
			   &eq->dbg, eq->eqn, eq_fields,
			   ARRAY_SIZE(eq_fields), eq);
	if (err)
		eq->dbg = NULL;

	return err;
}

void mlx5_debug_eq_remove(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	if (!mlx5_debugfs_root)
		return;

	if (eq->dbg)
		rem_res_tree(eq->dbg);
}

int mlx5_debug_cq_add(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq)
{
	int err;

	if (!mlx5_debugfs_root)
		return 0;

	err = add_res_tree(dev, MLX5_DBG_RSC_CQ, dev->priv.cq_debugfs,
			   &cq->dbg, cq->cqn, cq_fields,
			   ARRAY_SIZE(cq_fields), cq);
	if (err)
		cq->dbg = NULL;

	return err;
}

void mlx5_debug_cq_remove(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq)
{
	if (!mlx5_debugfs_root)
		return;

	if (cq->dbg) {
		rem_res_tree(cq->dbg);
		cq->dbg = NULL;
	}
}
