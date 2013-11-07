/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/init.h>

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/srq.h>
#include <linux/export.h>
#include <linux/gfp.h>

#include "mlx4.h"
#include "icm.h"

void mlx4_srq_event(struct mlx4_dev *dev, u32 srqn, int event_type)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	struct mlx4_srq *srq;

	spin_lock(&srq_table->lock);

	srq = radix_tree_lookup(&srq_table->tree, srqn & (dev->caps.num_srqs - 1));
	if (srq)
		atomic_inc(&srq->refcount);

	spin_unlock(&srq_table->lock);

	if (!srq) {
		mlx4_warn(dev, "Async event for bogus SRQ %08x\n", srqn);
		return;
	}

	srq->event(srq, event_type);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
}

static int mlx4_SW2HW_SRQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int srq_num)
{
	return mlx4_cmd(dev, mailbox->dma, srq_num, 0,
			MLX4_CMD_SW2HW_SRQ, MLX4_CMD_TIME_CLASS_A,
			MLX4_CMD_WRAPPED);
}

static int mlx4_HW2SW_SRQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int srq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox ? mailbox->dma : 0, srq_num,
			    mailbox ? 0 : 1, MLX4_CMD_HW2SW_SRQ,
			    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
}

static int mlx4_ARM_SRQ(struct mlx4_dev *dev, int srq_num, int limit_watermark)
{
	return mlx4_cmd(dev, limit_watermark, srq_num, 0, MLX4_CMD_ARM_SRQ,
			MLX4_CMD_TIME_CLASS_B, MLX4_CMD_WRAPPED);
}

static int mlx4_QUERY_SRQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int srq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox->dma, srq_num, 0, MLX4_CMD_QUERY_SRQ,
			    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
}

int __mlx4_srq_alloc_icm(struct mlx4_dev *dev, int *srqn)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	int err;


	*srqn = mlx4_bitmap_alloc(&srq_table->bitmap);
	if (*srqn == -1)
		return -ENOMEM;

	err = mlx4_table_get(dev, &srq_table->table, *srqn);
	if (err)
		goto err_out;

	err = mlx4_table_get(dev, &srq_table->cmpt_table, *srqn);
	if (err)
		goto err_put;
	return 0;

err_put:
	mlx4_table_put(dev, &srq_table->table, *srqn);

err_out:
	mlx4_bitmap_free(&srq_table->bitmap, *srqn);
	return err;
}

static int mlx4_srq_alloc_icm(struct mlx4_dev *dev, int *srqn)
{
	u64 out_param;
	int err;

	if (mlx4_is_mfunc(dev)) {
		err = mlx4_cmd_imm(dev, 0, &out_param, RES_SRQ,
				   RES_OP_RESERVE_AND_MAP,
				   MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (!err)
			*srqn = get_param_l(&out_param);

		return err;
	}
	return __mlx4_srq_alloc_icm(dev, srqn);
}

void __mlx4_srq_free_icm(struct mlx4_dev *dev, int srqn)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;

	mlx4_table_put(dev, &srq_table->cmpt_table, srqn);
	mlx4_table_put(dev, &srq_table->table, srqn);
	mlx4_bitmap_free(&srq_table->bitmap, srqn);
}

static void mlx4_srq_free_icm(struct mlx4_dev *dev, int srqn)
{
	u64 in_param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, srqn);
		if (mlx4_cmd(dev, in_param, RES_SRQ, RES_OP_RESERVE_AND_MAP,
			     MLX4_CMD_FREE_RES,
			     MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED))
			mlx4_warn(dev, "Failed freeing cq:%d\n", srqn);
		return;
	}
	__mlx4_srq_free_icm(dev, srqn);
}

int mlx4_srq_alloc(struct mlx4_dev *dev, u32 pdn, u32 cqn, u16 xrcd,
		   struct mlx4_mtt *mtt, u64 db_rec, struct mlx4_srq *srq)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_srq_context *srq_context;
	u64 mtt_addr;
	int err;

	err = mlx4_srq_alloc_icm(dev, &srq->srqn);
	if (err)
		return err;

	spin_lock_irq(&srq_table->lock);
	err = radix_tree_insert(&srq_table->tree, srq->srqn, srq);
	spin_unlock_irq(&srq_table->lock);
	if (err)
		goto err_icm;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_radix;
	}

	srq_context = mailbox->buf;
	srq_context->state_logsize_srqn = cpu_to_be32((ilog2(srq->max) << 24) |
						      srq->srqn);
	srq_context->logstride          = srq->wqe_shift - 4;
	srq_context->xrcd		= cpu_to_be16(xrcd);
	srq_context->pg_offset_cqn	= cpu_to_be32(cqn & 0xffffff);
	srq_context->log_page_size      = mtt->page_shift - MLX4_ICM_PAGE_SHIFT;

	mtt_addr = mlx4_mtt_addr(dev, mtt);
	srq_context->mtt_base_addr_h    = mtt_addr >> 32;
	srq_context->mtt_base_addr_l    = cpu_to_be32(mtt_addr & 0xffffffff);
	srq_context->pd			= cpu_to_be32(pdn);
	srq_context->db_rec_addr        = cpu_to_be64(db_rec);

	err = mlx4_SW2HW_SRQ(dev, mailbox, srq->srqn);
	mlx4_free_cmd_mailbox(dev, mailbox);
	if (err)
		goto err_radix;

	atomic_set(&srq->refcount, 1);
	init_completion(&srq->free);

	return 0;

err_radix:
	spin_lock_irq(&srq_table->lock);
	radix_tree_delete(&srq_table->tree, srq->srqn);
	spin_unlock_irq(&srq_table->lock);

err_icm:
	mlx4_srq_free_icm(dev, srq->srqn);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_srq_alloc);

void mlx4_srq_free(struct mlx4_dev *dev, struct mlx4_srq *srq)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	int err;

	err = mlx4_HW2SW_SRQ(dev, NULL, srq->srqn);
	if (err)
		mlx4_warn(dev, "HW2SW_SRQ failed (%d) for SRQN %06x\n", err, srq->srqn);

	spin_lock_irq(&srq_table->lock);
	radix_tree_delete(&srq_table->tree, srq->srqn);
	spin_unlock_irq(&srq_table->lock);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	mlx4_srq_free_icm(dev, srq->srqn);
}
EXPORT_SYMBOL_GPL(mlx4_srq_free);

int mlx4_srq_arm(struct mlx4_dev *dev, struct mlx4_srq *srq, int limit_watermark)
{
	return mlx4_ARM_SRQ(dev, srq->srqn, limit_watermark);
}
EXPORT_SYMBOL_GPL(mlx4_srq_arm);

int mlx4_srq_query(struct mlx4_dev *dev, struct mlx4_srq *srq, int *limit_watermark)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_srq_context *srq_context;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	srq_context = mailbox->buf;

	err = mlx4_QUERY_SRQ(dev, mailbox, srq->srqn);
	if (err)
		goto err_out;
	*limit_watermark = be16_to_cpu(srq_context->limit_watermark);

err_out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_srq_query);

int mlx4_init_srq_table(struct mlx4_dev *dev)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	int err;

	spin_lock_init(&srq_table->lock);
	INIT_RADIX_TREE(&srq_table->tree, GFP_ATOMIC);
	if (mlx4_is_slave(dev))
		return 0;

	err = mlx4_bitmap_init(&srq_table->bitmap, dev->caps.num_srqs,
			       dev->caps.num_srqs - 1, dev->caps.reserved_srqs, 0);
	if (err)
		return err;

	return 0;
}

void mlx4_cleanup_srq_table(struct mlx4_dev *dev)
{
	if (mlx4_is_slave(dev))
		return;
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->srq_table.bitmap);
}

struct mlx4_srq *mlx4_srq_lookup(struct mlx4_dev *dev, u32 srqn)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	struct mlx4_srq *srq;
	unsigned long flags;

	spin_lock_irqsave(&srq_table->lock, flags);
	srq = radix_tree_lookup(&srq_table->tree,
				srqn & (dev->caps.num_srqs - 1));
	spin_unlock_irqrestore(&srq_table->lock, flags);

	return srq;
}
EXPORT_SYMBOL_GPL(mlx4_srq_lookup);
