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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/mlx5/driver.h>
#include <rdma/ib_verbs.h>
#include <linux/mlx5/cq.h>
#include "mlx5_core.h"
#include "lib/eq.h"

#define TASKLET_MAX_TIME 2
#define TASKLET_MAX_TIME_JIFFIES msecs_to_jiffies(TASKLET_MAX_TIME)

void mlx5_cq_tasklet_cb(struct tasklet_struct *t)
{
	unsigned long flags;
	unsigned long end = jiffies + TASKLET_MAX_TIME_JIFFIES;
	struct mlx5_eq_tasklet *ctx = from_tasklet(ctx, t, task);
	struct mlx5_core_cq *mcq;
	struct mlx5_core_cq *temp;

	spin_lock_irqsave(&ctx->lock, flags);
	list_splice_tail_init(&ctx->list, &ctx->process_list);
	spin_unlock_irqrestore(&ctx->lock, flags);

	list_for_each_entry_safe(mcq, temp, &ctx->process_list,
				 tasklet_ctx.list) {
		list_del_init(&mcq->tasklet_ctx.list);
		mcq->tasklet_ctx.comp(mcq, NULL);
		mlx5_cq_put(mcq);
		if (time_after(jiffies, end))
			break;
	}

	if (!list_empty(&ctx->process_list))
		tasklet_schedule(&ctx->task);
}

static void mlx5_add_cq_to_tasklet(struct mlx5_core_cq *cq,
				   struct mlx5_eqe *eqe)
{
	unsigned long flags;
	struct mlx5_eq_tasklet *tasklet_ctx = cq->tasklet_ctx.priv;

	spin_lock_irqsave(&tasklet_ctx->lock, flags);
	/* When migrating CQs between EQs will be implemented, please note
	 * that you need to sync this point. It is possible that
	 * while migrating a CQ, completions on the old EQs could
	 * still arrive.
	 */
	if (list_empty_careful(&cq->tasklet_ctx.list)) {
		mlx5_cq_hold(cq);
		list_add_tail(&cq->tasklet_ctx.list, &tasklet_ctx->list);
	}
	spin_unlock_irqrestore(&tasklet_ctx->lock, flags);
}

/* Callers must verify outbox status in case of err */
int mlx5_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		   u32 *in, int inlen, u32 *out, int outlen)
{
	int eqn = MLX5_GET(cqc, MLX5_ADDR_OF(create_cq_in, in, cq_context),
			   c_eqn_or_apu_element);
	u32 din[MLX5_ST_SZ_DW(destroy_cq_in)] = {};
	struct mlx5_eq_comp *eq;
	int err;

	eq = mlx5_eqn2comp_eq(dev, eqn);
	if (IS_ERR(eq))
		return PTR_ERR(eq);

	memset(out, 0, outlen);
	MLX5_SET(create_cq_in, in, opcode, MLX5_CMD_OP_CREATE_CQ);
	err = mlx5_cmd_do(dev, in, inlen, out, outlen);
	if (err)
		return err;

	cq->cqn = MLX5_GET(create_cq_out, out, cqn);
	cq->cons_index = 0;
	cq->arm_sn     = 0;
	cq->eq         = eq;
	cq->uid = MLX5_GET(create_cq_in, in, uid);
	refcount_set(&cq->refcount, 1);
	init_completion(&cq->free);
	if (!cq->comp)
		cq->comp = mlx5_add_cq_to_tasklet;
	/* assuming CQ will be deleted before the EQ */
	cq->tasklet_ctx.priv = &eq->tasklet_ctx;
	INIT_LIST_HEAD(&cq->tasklet_ctx.list);

	/* Add to comp EQ CQ tree to recv comp events */
	err = mlx5_eq_add_cq(&eq->core, cq);
	if (err)
		goto err_cmd;

	/* Add to async EQ CQ tree to recv async events */
	err = mlx5_eq_add_cq(mlx5_get_async_eq(dev), cq);
	if (err)
		goto err_cq_add;

	cq->pid = current->pid;
	err = mlx5_debug_cq_add(dev, cq);
	if (err)
		mlx5_core_dbg(dev, "failed adding CP 0x%x to debug file system\n",
			      cq->cqn);

	cq->uar = dev->priv.uar;
	cq->irqn = eq->core.irqn;

	return 0;

err_cq_add:
	mlx5_eq_del_cq(&eq->core, cq);
err_cmd:
	MLX5_SET(destroy_cq_in, din, opcode, MLX5_CMD_OP_DESTROY_CQ);
	MLX5_SET(destroy_cq_in, din, cqn, cq->cqn);
	MLX5_SET(destroy_cq_in, din, uid, cq->uid);
	mlx5_cmd_exec_in(dev, destroy_cq, din);
	return err;
}
EXPORT_SYMBOL(mlx5_create_cq);

/* oubox is checked and err val is normalized */
int mlx5_core_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			u32 *in, int inlen, u32 *out, int outlen)
{
	int err = mlx5_create_cq(dev, cq, in, inlen, out, outlen);

	return mlx5_cmd_check(dev, err, in, out);
}
EXPORT_SYMBOL(mlx5_core_create_cq);

int mlx5_core_destroy_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_cq_in)] = {};
	int err;

	mlx5_debug_cq_remove(dev, cq);

	mlx5_eq_del_cq(mlx5_get_async_eq(dev), cq);
	mlx5_eq_del_cq(&cq->eq->core, cq);

	MLX5_SET(destroy_cq_in, in, opcode, MLX5_CMD_OP_DESTROY_CQ);
	MLX5_SET(destroy_cq_in, in, cqn, cq->cqn);
	MLX5_SET(destroy_cq_in, in, uid, cq->uid);
	err = mlx5_cmd_exec_in(dev, destroy_cq, in);
	if (err)
		return err;

	synchronize_irq(cq->irqn);
	mlx5_cq_put(cq);
	wait_for_completion(&cq->free);

	return 0;
}
EXPORT_SYMBOL(mlx5_core_destroy_cq);

int mlx5_core_query_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		       u32 *out)
{
	u32 in[MLX5_ST_SZ_DW(query_cq_in)] = {};

	MLX5_SET(query_cq_in, in, opcode, MLX5_CMD_OP_QUERY_CQ);
	MLX5_SET(query_cq_in, in, cqn, cq->cqn);
	return mlx5_cmd_exec_inout(dev, query_cq, in, out);
}
EXPORT_SYMBOL(mlx5_core_query_cq);

int mlx5_core_modify_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_cq_out)] = {};

	MLX5_SET(modify_cq_in, in, opcode, MLX5_CMD_OP_MODIFY_CQ);
	MLX5_SET(modify_cq_in, in, uid, cq->uid);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_modify_cq);

int mlx5_core_modify_cq_moderation(struct mlx5_core_dev *dev,
				   struct mlx5_core_cq *cq,
				   u16 cq_period,
				   u16 cq_max_count)
{
	u32 in[MLX5_ST_SZ_DW(modify_cq_in)] = {};
	void *cqc;

	MLX5_SET(modify_cq_in, in, cqn, cq->cqn);
	cqc = MLX5_ADDR_OF(modify_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, cq_period, cq_period);
	MLX5_SET(cqc, cqc, cq_max_count, cq_max_count);
	MLX5_SET(modify_cq_in, in,
		 modify_field_select_resize_field_select.modify_field_select.modify_field_select,
		 MLX5_CQ_MODIFY_PERIOD | MLX5_CQ_MODIFY_COUNT);

	return mlx5_core_modify_cq(dev, cq, in, sizeof(in));
}
EXPORT_SYMBOL(mlx5_core_modify_cq_moderation);
