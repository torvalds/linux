/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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

#include <linux/init.h>

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/qp.h>

#include "mlx4.h"
#include "icm.h"

void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	struct mlx4_qp *qp;

	spin_lock(&qp_table->lock);

	qp = __mlx4_qp_lookup(dev, qpn);
	if (qp)
		atomic_inc(&qp->refcount);

	spin_unlock(&qp_table->lock);

	if (!qp) {
		mlx4_warn(dev, "Async event for bogus QP %08x\n", qpn);
		return;
	}

	qp->event(qp, event_type);

	if (atomic_dec_and_test(&qp->refcount))
		complete(&qp->free);
}

int mlx4_qp_modify(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
		   struct mlx4_qp_context *context, enum mlx4_qp_optpar optpar,
		   int sqd_event, struct mlx4_qp *qp)
{
	static const u16 op[MLX4_QP_NUM_STATE][MLX4_QP_NUM_STATE] = {
		[MLX4_QP_STATE_RST] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_INIT]	= MLX4_CMD_RST2INIT_QP,
		},
		[MLX4_QP_STATE_INIT]  = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_INIT]	= MLX4_CMD_INIT2INIT_QP,
			[MLX4_QP_STATE_RTR]	= MLX4_CMD_INIT2RTR_QP,
		},
		[MLX4_QP_STATE_RTR]   = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_RTR2RTS_QP,
		},
		[MLX4_QP_STATE_RTS]   = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_RTS2RTS_QP,
			[MLX4_QP_STATE_SQD]	= MLX4_CMD_RTS2SQD_QP,
		},
		[MLX4_QP_STATE_SQD] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_SQD2RTS_QP,
			[MLX4_QP_STATE_SQD]	= MLX4_CMD_SQD2SQD_QP,
		},
		[MLX4_QP_STATE_SQER] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_SQERR2RTS_QP,
		},
		[MLX4_QP_STATE_ERR] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
		}
	};

	struct mlx4_cmd_mailbox *mailbox;
	int ret = 0;

	if (cur_state >= MLX4_QP_NUM_STATE || new_state >= MLX4_QP_NUM_STATE ||
	    !op[cur_state][new_state])
		return -EINVAL;

	if (op[cur_state][new_state] == MLX4_CMD_2RST_QP)
		return mlx4_cmd(dev, 0, qp->qpn, 2,
				MLX4_CMD_2RST_QP, MLX4_CMD_TIME_CLASS_A);

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (cur_state == MLX4_QP_STATE_RST && new_state == MLX4_QP_STATE_INIT) {
		u64 mtt_addr = mlx4_mtt_addr(dev, mtt);
		context->mtt_base_addr_h = mtt_addr >> 32;
		context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);
		context->log_page_size   = mtt->page_shift - MLX4_ICM_PAGE_SHIFT;
	}

	*(__be32 *) mailbox->buf = cpu_to_be32(optpar);
	memcpy(mailbox->buf + 8, context, sizeof *context);

	((struct mlx4_qp_context *) (mailbox->buf + 8))->local_qpn =
		cpu_to_be32(qp->qpn);

	ret = mlx4_cmd(dev, mailbox->dma, qp->qpn | (!!sqd_event << 31),
		       new_state == MLX4_QP_STATE_RST ? 2 : 0,
		       op[cur_state][new_state], MLX4_CMD_TIME_CLASS_C);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return ret;
}
EXPORT_SYMBOL_GPL(mlx4_qp_modify);

int mlx4_qp_alloc(struct mlx4_dev *dev, int sqpn, struct mlx4_qp *qp)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;
	int err;

	if (sqpn)
		qp->qpn = sqpn;
	else {
		qp->qpn = mlx4_bitmap_alloc(&qp_table->bitmap);
		if (qp->qpn == -1)
			return -ENOMEM;
	}

	err = mlx4_table_get(dev, &qp_table->qp_table, qp->qpn);
	if (err)
		goto err_out;

	err = mlx4_table_get(dev, &qp_table->auxc_table, qp->qpn);
	if (err)
		goto err_put_qp;

	err = mlx4_table_get(dev, &qp_table->altc_table, qp->qpn);
	if (err)
		goto err_put_auxc;

	err = mlx4_table_get(dev, &qp_table->rdmarc_table, qp->qpn);
	if (err)
		goto err_put_altc;

	err = mlx4_table_get(dev, &qp_table->cmpt_table, qp->qpn);
	if (err)
		goto err_put_rdmarc;

	spin_lock_irq(&qp_table->lock);
	err = radix_tree_insert(&dev->qp_table_tree, qp->qpn & (dev->caps.num_qps - 1), qp);
	spin_unlock_irq(&qp_table->lock);
	if (err)
		goto err_put_cmpt;

	atomic_set(&qp->refcount, 1);
	init_completion(&qp->free);

	return 0;

err_put_cmpt:
	mlx4_table_put(dev, &qp_table->cmpt_table, qp->qpn);

err_put_rdmarc:
	mlx4_table_put(dev, &qp_table->rdmarc_table, qp->qpn);

err_put_altc:
	mlx4_table_put(dev, &qp_table->altc_table, qp->qpn);

err_put_auxc:
	mlx4_table_put(dev, &qp_table->auxc_table, qp->qpn);

err_put_qp:
	mlx4_table_put(dev, &qp_table->qp_table, qp->qpn);

err_out:
	if (!sqpn)
		mlx4_bitmap_free(&qp_table->bitmap, qp->qpn);

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_qp_alloc);

void mlx4_qp_remove(struct mlx4_dev *dev, struct mlx4_qp *qp)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	unsigned long flags;

	spin_lock_irqsave(&qp_table->lock, flags);
	radix_tree_delete(&dev->qp_table_tree, qp->qpn & (dev->caps.num_qps - 1));
	spin_unlock_irqrestore(&qp_table->lock, flags);
}
EXPORT_SYMBOL_GPL(mlx4_qp_remove);

void mlx4_qp_free(struct mlx4_dev *dev, struct mlx4_qp *qp)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;

	if (atomic_dec_and_test(&qp->refcount))
		complete(&qp->free);
	wait_for_completion(&qp->free);

	mlx4_table_put(dev, &qp_table->cmpt_table, qp->qpn);
	mlx4_table_put(dev, &qp_table->rdmarc_table, qp->qpn);
	mlx4_table_put(dev, &qp_table->altc_table, qp->qpn);
	mlx4_table_put(dev, &qp_table->auxc_table, qp->qpn);
	mlx4_table_put(dev, &qp_table->qp_table, qp->qpn);

	if (qp->qpn >= dev->caps.sqp_start + 8)
		mlx4_bitmap_free(&qp_table->bitmap, qp->qpn);
}
EXPORT_SYMBOL_GPL(mlx4_qp_free);

static int mlx4_CONF_SPECIAL_QP(struct mlx4_dev *dev, u32 base_qpn)
{
	return mlx4_cmd(dev, 0, base_qpn, 0, MLX4_CMD_CONF_SPECIAL_QP,
			MLX4_CMD_TIME_CLASS_B);
}

int mlx4_init_qp_table(struct mlx4_dev *dev)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	int err;

	spin_lock_init(&qp_table->lock);
	INIT_RADIX_TREE(&dev->qp_table_tree, GFP_ATOMIC);

	/*
	 * We reserve 2 extra QPs per port for the special QPs.  The
	 * block of special QPs must be aligned to a multiple of 8, so
	 * round up.
	 */
	dev->caps.sqp_start = ALIGN(dev->caps.reserved_qps, 8);
	err = mlx4_bitmap_init(&qp_table->bitmap, dev->caps.num_qps,
			       (1 << 24) - 1, dev->caps.sqp_start + 8);
	if (err)
		return err;

	return mlx4_CONF_SPECIAL_QP(dev, dev->caps.sqp_start);
}

void mlx4_cleanup_qp_table(struct mlx4_dev *dev)
{
	mlx4_CONF_SPECIAL_QP(dev, 0);
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->qp_table.bitmap);
}

int mlx4_qp_query(struct mlx4_dev *dev, struct mlx4_qp *qp,
		  struct mlx4_qp_context *context)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(dev, 0, mailbox->dma, qp->qpn, 0,
			   MLX4_CMD_QUERY_QP, MLX4_CMD_TIME_CLASS_A);
	if (!err)
		memcpy(context, mailbox->buf + 8, sizeof *context);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_qp_query);

int mlx4_qp_to_ready(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		     struct mlx4_qp_context *context,
		     struct mlx4_qp *qp, enum mlx4_qp_state *qp_state)
{
	int err;
	int i;
	enum mlx4_qp_state states[] = {
		MLX4_QP_STATE_RST,
		MLX4_QP_STATE_INIT,
		MLX4_QP_STATE_RTR,
		MLX4_QP_STATE_RTS
	};

	for (i = 0; i < ARRAY_SIZE(states) - 1; i++) {
		context->flags &= cpu_to_be32(~(0xf << 28));
		context->flags |= cpu_to_be32(states[i + 1] << 28);
		err = mlx4_qp_modify(dev, mtt, states[i], states[i + 1],
				     context, 0, 0, qp);
		if (err) {
			mlx4_err(dev, "Failed to bring QP to state: "
				 "%d with error: %d\n",
				 states[i + 1], err);
			return err;
		}

		*qp_state = states[i + 1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_qp_to_ready);
