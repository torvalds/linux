/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#include <linux/hardirq.h>
#include <linux/export.h>

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/cq.h>

#include "mlx4.h"
#include "icm.h"

#define MLX4_CQ_STATUS_OK		( 0 << 28)
#define MLX4_CQ_STATUS_OVERFLOW		( 9 << 28)
#define MLX4_CQ_STATUS_WRITE_FAIL	(10 << 28)
#define MLX4_CQ_FLAG_CC			( 1 << 18)
#define MLX4_CQ_FLAG_OI			( 1 << 17)
#define MLX4_CQ_STATE_ARMED		( 9 <<  8)
#define MLX4_CQ_STATE_ARMED_SOL		( 6 <<  8)
#define MLX4_EQ_STATE_FIRED		(10 <<  8)

#define TASKLET_MAX_TIME 2
#define TASKLET_MAX_TIME_JIFFIES msecs_to_jiffies(TASKLET_MAX_TIME)

void mlx4_cq_tasklet_cb(unsigned long data)
{
	unsigned long flags;
	unsigned long end = jiffies + TASKLET_MAX_TIME_JIFFIES;
	struct mlx4_eq_tasklet *ctx = (struct mlx4_eq_tasklet *)data;
	struct mlx4_cq *mcq, *temp;

	spin_lock_irqsave(&ctx->lock, flags);
	list_splice_tail_init(&ctx->list, &ctx->process_list);
	spin_unlock_irqrestore(&ctx->lock, flags);

	list_for_each_entry_safe(mcq, temp, &ctx->process_list, tasklet_ctx.list) {
		list_del_init(&mcq->tasklet_ctx.list);
		mcq->tasklet_ctx.comp(mcq);
		if (atomic_dec_and_test(&mcq->refcount))
			complete(&mcq->free);
		if (time_after(jiffies, end))
			break;
	}

	if (!list_empty(&ctx->process_list))
		tasklet_schedule(&ctx->task);
}

static void mlx4_add_cq_to_tasklet(struct mlx4_cq *cq)
{
	unsigned long flags;
	struct mlx4_eq_tasklet *tasklet_ctx = cq->tasklet_ctx.priv;

	spin_lock_irqsave(&tasklet_ctx->lock, flags);
	/* When migrating CQs between EQs will be implemented, please note
	 * that you need to sync this point. It is possible that
	 * while migrating a CQ, completions on the old EQs could
	 * still arrive.
	 */
	if (list_empty_careful(&cq->tasklet_ctx.list)) {
		atomic_inc(&cq->refcount);
		list_add_tail(&cq->tasklet_ctx.list, &tasklet_ctx->list);
	}
	spin_unlock_irqrestore(&tasklet_ctx->lock, flags);
}

void mlx4_cq_completion(struct mlx4_dev *dev, u32 cqn)
{
	struct mlx4_cq *cq;

	rcu_read_lock();
	cq = radix_tree_lookup(&mlx4_priv(dev)->cq_table.tree,
			       cqn & (dev->caps.num_cqs - 1));
	rcu_read_unlock();

	if (!cq) {
		mlx4_dbg(dev, "Completion event for bogus CQ %08x\n", cqn);
		return;
	}

	/* Acessing the CQ outside of rcu_read_lock is safe, because
	 * the CQ is freed only after interrupt handling is completed.
	 */
	++cq->arm_sn;

	cq->comp(cq);
}

void mlx4_cq_event(struct mlx4_dev *dev, u32 cqn, int event_type)
{
	struct mlx4_cq_table *cq_table = &mlx4_priv(dev)->cq_table;
	struct mlx4_cq *cq;

	rcu_read_lock();
	cq = radix_tree_lookup(&cq_table->tree, cqn & (dev->caps.num_cqs - 1));
	rcu_read_unlock();

	if (!cq) {
		mlx4_dbg(dev, "Async event for bogus CQ %08x\n", cqn);
		return;
	}

	/* Acessing the CQ outside of rcu_read_lock is safe, because
	 * the CQ is freed only after interrupt handling is completed.
	 */
	cq->event(cq, event_type);
}

static int mlx4_SW2HW_CQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int cq_num)
{
	return mlx4_cmd(dev, mailbox->dma, cq_num, 0,
			MLX4_CMD_SW2HW_CQ, MLX4_CMD_TIME_CLASS_A,
			MLX4_CMD_WRAPPED);
}

static int mlx4_MODIFY_CQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int cq_num, u32 opmod)
{
	return mlx4_cmd(dev, mailbox->dma, cq_num, opmod, MLX4_CMD_MODIFY_CQ,
			MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
}

static int mlx4_HW2SW_CQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int cq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox ? mailbox->dma : 0,
			    cq_num, mailbox ? 0 : 1, MLX4_CMD_HW2SW_CQ,
			    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
}

int mlx4_cq_modify(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   u16 count, u16 period)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_cq_context *cq_context;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cq_context = mailbox->buf;
	cq_context->cq_max_count = cpu_to_be16(count);
	cq_context->cq_period    = cpu_to_be16(period);

	err = mlx4_MODIFY_CQ(dev, mailbox, cq->cqn, 1);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_cq_modify);

int mlx4_cq_resize(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   int entries, struct mlx4_mtt *mtt)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_cq_context *cq_context;
	u64 mtt_addr;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cq_context = mailbox->buf;
	cq_context->logsize_usrpage = cpu_to_be32(ilog2(entries) << 24);
	cq_context->log_page_size   = mtt->page_shift - 12;
	mtt_addr = mlx4_mtt_addr(dev, mtt);
	cq_context->mtt_base_addr_h = mtt_addr >> 32;
	cq_context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);

	err = mlx4_MODIFY_CQ(dev, mailbox, cq->cqn, 0);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_cq_resize);

int __mlx4_cq_alloc_icm(struct mlx4_dev *dev, int *cqn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cq_table *cq_table = &priv->cq_table;
	int err;

	*cqn = mlx4_bitmap_alloc(&cq_table->bitmap);
	if (*cqn == -1)
		return -ENOMEM;

	err = mlx4_table_get(dev, &cq_table->table, *cqn, GFP_KERNEL);
	if (err)
		goto err_out;

	err = mlx4_table_get(dev, &cq_table->cmpt_table, *cqn, GFP_KERNEL);
	if (err)
		goto err_put;
	return 0;

err_put:
	mlx4_table_put(dev, &cq_table->table, *cqn);

err_out:
	mlx4_bitmap_free(&cq_table->bitmap, *cqn, MLX4_NO_RR);
	return err;
}

static int mlx4_cq_alloc_icm(struct mlx4_dev *dev, int *cqn)
{
	u64 out_param;
	int err;

	if (mlx4_is_mfunc(dev)) {
		err = mlx4_cmd_imm(dev, 0, &out_param, RES_CQ,
				   RES_OP_RESERVE_AND_MAP, MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (err)
			return err;
		else {
			*cqn = get_param_l(&out_param);
			return 0;
		}
	}
	return __mlx4_cq_alloc_icm(dev, cqn);
}

void __mlx4_cq_free_icm(struct mlx4_dev *dev, int cqn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cq_table *cq_table = &priv->cq_table;

	mlx4_table_put(dev, &cq_table->cmpt_table, cqn);
	mlx4_table_put(dev, &cq_table->table, cqn);
	mlx4_bitmap_free(&cq_table->bitmap, cqn, MLX4_NO_RR);
}

static void mlx4_cq_free_icm(struct mlx4_dev *dev, int cqn)
{
	u64 in_param = 0;
	int err;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, cqn);
		err = mlx4_cmd(dev, in_param, RES_CQ, RES_OP_RESERVE_AND_MAP,
			       MLX4_CMD_FREE_RES,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (err)
			mlx4_warn(dev, "Failed freeing cq:%d\n", cqn);
	} else
		__mlx4_cq_free_icm(dev, cqn);
}

int mlx4_cq_alloc(struct mlx4_dev *dev, int nent,
		  struct mlx4_mtt *mtt, struct mlx4_uar *uar, u64 db_rec,
		  struct mlx4_cq *cq, unsigned vector, int collapsed,
		  int timestamp_en)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cq_table *cq_table = &priv->cq_table;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_cq_context *cq_context;
	u64 mtt_addr;
	int err;

	if (vector >= dev->caps.num_comp_vectors)
		return -EINVAL;

	cq->vector = vector;

	err = mlx4_cq_alloc_icm(dev, &cq->cqn);
	if (err)
		return err;

	spin_lock(&cq_table->lock);
	err = radix_tree_insert(&cq_table->tree, cq->cqn, cq);
	spin_unlock(&cq_table->lock);
	if (err)
		goto err_icm;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_radix;
	}

	cq_context = mailbox->buf;
	cq_context->flags	    = cpu_to_be32(!!collapsed << 18);
	if (timestamp_en)
		cq_context->flags  |= cpu_to_be32(1 << 19);

	cq_context->logsize_usrpage = cpu_to_be32((ilog2(nent) << 24) | uar->index);
	cq_context->comp_eqn	    = priv->eq_table.eq[MLX4_CQ_TO_EQ_VECTOR(vector)].eqn;
	cq_context->log_page_size   = mtt->page_shift - MLX4_ICM_PAGE_SHIFT;

	mtt_addr = mlx4_mtt_addr(dev, mtt);
	cq_context->mtt_base_addr_h = mtt_addr >> 32;
	cq_context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);
	cq_context->db_rec_addr     = cpu_to_be64(db_rec);

	err = mlx4_SW2HW_CQ(dev, mailbox, cq->cqn);
	mlx4_free_cmd_mailbox(dev, mailbox);
	if (err)
		goto err_radix;

	cq->cons_index = 0;
	cq->arm_sn     = 1;
	cq->uar        = uar;
	atomic_set(&cq->refcount, 1);
	init_completion(&cq->free);
	cq->comp = mlx4_add_cq_to_tasklet;
	cq->tasklet_ctx.priv =
		&priv->eq_table.eq[MLX4_CQ_TO_EQ_VECTOR(vector)].tasklet_ctx;
	INIT_LIST_HEAD(&cq->tasklet_ctx.list);


	cq->irq = priv->eq_table.eq[MLX4_CQ_TO_EQ_VECTOR(vector)].irq;
	return 0;

err_radix:
	spin_lock(&cq_table->lock);
	radix_tree_delete(&cq_table->tree, cq->cqn);
	spin_unlock(&cq_table->lock);

err_icm:
	mlx4_cq_free_icm(dev, cq->cqn);

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_cq_alloc);

void mlx4_cq_free(struct mlx4_dev *dev, struct mlx4_cq *cq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cq_table *cq_table = &priv->cq_table;
	int err;

	err = mlx4_HW2SW_CQ(dev, NULL, cq->cqn);
	if (err)
		mlx4_warn(dev, "HW2SW_CQ failed (%d) for CQN %06x\n", err, cq->cqn);

	spin_lock(&cq_table->lock);
	radix_tree_delete(&cq_table->tree, cq->cqn);
	spin_unlock(&cq_table->lock);

	synchronize_irq(priv->eq_table.eq[MLX4_CQ_TO_EQ_VECTOR(cq->vector)].irq);
	if (priv->eq_table.eq[MLX4_CQ_TO_EQ_VECTOR(cq->vector)].irq !=
	    priv->eq_table.eq[MLX4_EQ_ASYNC].irq)
		synchronize_irq(priv->eq_table.eq[MLX4_EQ_ASYNC].irq);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
	wait_for_completion(&cq->free);

	mlx4_cq_free_icm(dev, cq->cqn);
}
EXPORT_SYMBOL_GPL(mlx4_cq_free);

int mlx4_init_cq_table(struct mlx4_dev *dev)
{
	struct mlx4_cq_table *cq_table = &mlx4_priv(dev)->cq_table;
	int err;

	spin_lock_init(&cq_table->lock);
	INIT_RADIX_TREE(&cq_table->tree, GFP_ATOMIC);
	if (mlx4_is_slave(dev))
		return 0;

	err = mlx4_bitmap_init(&cq_table->bitmap, dev->caps.num_cqs,
			       dev->caps.num_cqs - 1, dev->caps.reserved_cqs, 0);
	if (err)
		return err;

	return 0;
}

void mlx4_cleanup_cq_table(struct mlx4_dev *dev)
{
	if (mlx4_is_slave(dev))
		return;
	/* Nothing to do to clean up radix_tree */
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->cq_table.bitmap);
}
