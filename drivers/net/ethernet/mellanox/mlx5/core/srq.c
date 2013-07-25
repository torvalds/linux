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
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <linux/mlx5/srq.h>
#include <rdma/ib_verbs.h>
#include "mlx5_core.h"

void mlx5_srq_event(struct mlx5_core_dev *dev, u32 srqn, int event_type)
{
	struct mlx5_srq_table *table = &dev->priv.srq_table;
	struct mlx5_core_srq *srq;

	spin_lock(&table->lock);

	srq = radix_tree_lookup(&table->tree, srqn);
	if (srq)
		atomic_inc(&srq->refcount);

	spin_unlock(&table->lock);

	if (!srq) {
		mlx5_core_warn(dev, "Async event for bogus SRQ 0x%08x\n", srqn);
		return;
	}

	srq->event(srq, event_type);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
}

struct mlx5_core_srq *mlx5_core_get_srq(struct mlx5_core_dev *dev, u32 srqn)
{
	struct mlx5_srq_table *table = &dev->priv.srq_table;
	struct mlx5_core_srq *srq;

	spin_lock(&table->lock);

	srq = radix_tree_lookup(&table->tree, srqn);
	if (srq)
		atomic_inc(&srq->refcount);

	spin_unlock(&table->lock);

	return srq;
}
EXPORT_SYMBOL(mlx5_core_get_srq);

int mlx5_core_create_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_create_srq_mbox_in *in, int inlen)
{
	struct mlx5_create_srq_mbox_out out;
	struct mlx5_srq_table *table = &dev->priv.srq_table;
	struct mlx5_destroy_srq_mbox_in din;
	struct mlx5_destroy_srq_mbox_out dout;
	int err;

	memset(&out, 0, sizeof(out));
	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_CREATE_SRQ);
	err = mlx5_cmd_exec(dev, in, inlen, &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	srq->srqn = be32_to_cpu(out.srqn) & 0xffffff;

	atomic_set(&srq->refcount, 1);
	init_completion(&srq->free);

	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, srq->srqn, srq);
	spin_unlock_irq(&table->lock);
	if (err) {
		mlx5_core_warn(dev, "err %d, srqn 0x%x\n", err, srq->srqn);
		goto err_cmd;
	}

	return 0;

err_cmd:
	memset(&din, 0, sizeof(din));
	memset(&dout, 0, sizeof(dout));
	din.srqn = cpu_to_be32(srq->srqn);
	din.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_SRQ);
	mlx5_cmd_exec(dev, &din, sizeof(din), &dout, sizeof(dout));
	return err;
}
EXPORT_SYMBOL(mlx5_core_create_srq);

int mlx5_core_destroy_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq)
{
	struct mlx5_destroy_srq_mbox_in in;
	struct mlx5_destroy_srq_mbox_out out;
	struct mlx5_srq_table *table = &dev->priv.srq_table;
	struct mlx5_core_srq *tmp;
	int err;

	spin_lock_irq(&table->lock);
	tmp = radix_tree_delete(&table->tree, srq->srqn);
	spin_unlock_irq(&table->lock);
	if (!tmp) {
		mlx5_core_warn(dev, "srq 0x%x not found in tree\n", srq->srqn);
		return -EINVAL;
	}
	if (tmp != srq) {
		mlx5_core_warn(dev, "corruption on srqn 0x%x\n", srq->srqn);
		return -EINVAL;
	}

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_SRQ);
	in.srqn = cpu_to_be32(srq->srqn);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	return 0;
}
EXPORT_SYMBOL(mlx5_core_destroy_srq);

int mlx5_core_query_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_query_srq_mbox_out *out)
{
	struct mlx5_query_srq_mbox_in in;
	int err;

	memset(&in, 0, sizeof(in));
	memset(out, 0, sizeof(*out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_SRQ);
	in.srqn = cpu_to_be32(srq->srqn);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, sizeof(*out));
	if (err)
		return err;

	if (out->hdr.status)
		return mlx5_cmd_status_to_err(&out->hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_query_srq);

int mlx5_core_arm_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
		      u16 lwm, int is_srq)
{
	struct mlx5_arm_srq_mbox_in	in;
	struct mlx5_arm_srq_mbox_out	out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_ARM_RQ);
	in.hdr.opmod = cpu_to_be16(!!is_srq);
	in.srqn = cpu_to_be32(srq->srqn);
	in.lwm = cpu_to_be16(lwm);

	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_arm_srq);

void mlx5_init_srq_table(struct mlx5_core_dev *dev)
{
	struct mlx5_srq_table *table = &dev->priv.srq_table;

	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
}

void mlx5_cleanup_srq_table(struct mlx5_core_dev *dev)
{
	/* nothing */
}
