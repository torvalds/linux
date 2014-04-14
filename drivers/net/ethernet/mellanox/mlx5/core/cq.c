/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <rdma/ib_verbs.h>
#include <linux/mlx5/cq.h>
#include "mlx5_core.h"

void mlx5_cq_completion(struct mlx5_core_dev *dev, u32 cqn)
{
	struct mlx5_core_cq *cq;
	struct mlx5_cq_table *table = &dev->priv.cq_table;

	spin_lock(&table->lock);
	cq = radix_tree_lookup(&table->tree, cqn);
	if (likely(cq))
		atomic_inc(&cq->refcount);
	spin_unlock(&table->lock);

	if (!cq) {
		mlx5_core_warn(dev, "Completion event for bogus CQ 0x%x\n", cqn);
		return;
	}

	++cq->arm_sn;

	cq->comp(cq);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
}

void mlx5_cq_event(struct mlx5_core_dev *dev, u32 cqn, int event_type)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	struct mlx5_core_cq *cq;

	spin_lock(&table->lock);

	cq = radix_tree_lookup(&table->tree, cqn);
	if (cq)
		atomic_inc(&cq->refcount);

	spin_unlock(&table->lock);

	if (!cq) {
		mlx5_core_warn(dev, "Async event for bogus CQ 0x%x\n", cqn);
		return;
	}

	cq->event(cq, event_type);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
}


int mlx5_core_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			struct mlx5_create_cq_mbox_in *in, int inlen)
{
	int err;
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	struct mlx5_create_cq_mbox_out out;
	struct mlx5_destroy_cq_mbox_in din;
	struct mlx5_destroy_cq_mbox_out dout;

	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_CREATE_CQ);
	memset(&out, 0, sizeof(out));
	err = mlx5_cmd_exec(dev, in, inlen, &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	cq->cqn = be32_to_cpu(out.cqn) & 0xffffff;
	cq->cons_index = 0;
	cq->arm_sn     = 0;
	atomic_set(&cq->refcount, 1);
	init_completion(&cq->free);

	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, cq->cqn, cq);
	spin_unlock_irq(&table->lock);
	if (err)
		goto err_cmd;

	cq->pid = current->pid;
	err = mlx5_debug_cq_add(dev, cq);
	if (err)
		mlx5_core_dbg(dev, "failed adding CP 0x%x to debug file system\n",
			      cq->cqn);

	return 0;

err_cmd:
	memset(&din, 0, sizeof(din));
	memset(&dout, 0, sizeof(dout));
	din.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_CQ);
	mlx5_cmd_exec(dev, &din, sizeof(din), &dout, sizeof(dout));
	return err;
}
EXPORT_SYMBOL(mlx5_core_create_cq);

int mlx5_core_destroy_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	struct mlx5_destroy_cq_mbox_in in;
	struct mlx5_destroy_cq_mbox_out out;
	struct mlx5_core_cq *tmp;
	int err;

	spin_lock_irq(&table->lock);
	tmp = radix_tree_delete(&table->tree, cq->cqn);
	spin_unlock_irq(&table->lock);
	if (!tmp) {
		mlx5_core_warn(dev, "cq 0x%x not found in tree\n", cq->cqn);
		return -EINVAL;
	}
	if (tmp != cq) {
		mlx5_core_warn(dev, "corruption on srqn 0x%x\n", cq->cqn);
		return -EINVAL;
	}

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_CQ);
	in.cqn = cpu_to_be32(cq->cqn);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	synchronize_irq(cq->irqn);

	mlx5_debug_cq_remove(dev, cq);
	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
	wait_for_completion(&cq->free);

	return 0;
}
EXPORT_SYMBOL(mlx5_core_destroy_cq);

int mlx5_core_query_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		       struct mlx5_query_cq_mbox_out *out)
{
	struct mlx5_query_cq_mbox_in in;
	int err;

	memset(&in, 0, sizeof(in));
	memset(out, 0, sizeof(*out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_CQ);
	in.cqn = cpu_to_be32(cq->cqn);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, sizeof(*out));
	if (err)
		return err;

	if (out->hdr.status)
		return mlx5_cmd_status_to_err(&out->hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_query_cq);


int mlx5_core_modify_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			struct mlx5_modify_cq_mbox_in *in, int in_sz)
{
	struct mlx5_modify_cq_mbox_out out;
	int err;

	memset(&out, 0, sizeof(out));
	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_MODIFY_CQ);
	err = mlx5_cmd_exec(dev, in, in_sz, &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return 0;
}
EXPORT_SYMBOL(mlx5_core_modify_cq);

int mlx5_init_cq_table(struct mlx5_core_dev *dev)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	int err;

	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
	err = mlx5_cq_debugfs_init(dev);

	return err;
}

void mlx5_cleanup_cq_table(struct mlx5_core_dev *dev)
{
	mlx5_cq_debugfs_cleanup(dev);
}
