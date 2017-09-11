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
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <linux/mlx5/srq.h>
#include <rdma/ib_verbs.h>
#include "mlx5_core.h"
#include <linux/mlx5/transobj.h>

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

static int get_pas_size(struct mlx5_srq_attr *in)
{
	u32 log_page_size = in->log_page_size + 12;
	u32 log_srq_size  = in->log_size;
	u32 log_rq_stride = in->wqe_shift;
	u32 page_offset   = in->page_offset;
	u32 po_quanta	  = 1 << (log_page_size - 6);
	u32 rq_sz	  = 1 << (log_srq_size + 4 + log_rq_stride);
	u32 page_size	  = 1 << log_page_size;
	u32 rq_sz_po      = rq_sz + (page_offset * po_quanta);
	u32 rq_num_pas	  = (rq_sz_po + page_size - 1) / page_size;

	return rq_num_pas * sizeof(u64);
}

static void set_wq(void *wq, struct mlx5_srq_attr *in)
{
	MLX5_SET(wq,   wq, wq_signature,  !!(in->flags
		 & MLX5_SRQ_FLAG_WQ_SIG));
	MLX5_SET(wq,   wq, log_wq_pg_sz,  in->log_page_size);
	MLX5_SET(wq,   wq, log_wq_stride, in->wqe_shift + 4);
	MLX5_SET(wq,   wq, log_wq_sz,     in->log_size);
	MLX5_SET(wq,   wq, page_offset,   in->page_offset);
	MLX5_SET(wq,   wq, lwm,		  in->lwm);
	MLX5_SET(wq,   wq, pd,		  in->pd);
	MLX5_SET64(wq, wq, dbr_addr,	  in->db_record);
}

static void set_srqc(void *srqc, struct mlx5_srq_attr *in)
{
	MLX5_SET(srqc,   srqc, wq_signature,  !!(in->flags
		 & MLX5_SRQ_FLAG_WQ_SIG));
	MLX5_SET(srqc,   srqc, log_page_size, in->log_page_size);
	MLX5_SET(srqc,   srqc, log_rq_stride, in->wqe_shift);
	MLX5_SET(srqc,   srqc, log_srq_size,  in->log_size);
	MLX5_SET(srqc,   srqc, page_offset,   in->page_offset);
	MLX5_SET(srqc,	 srqc, lwm,	      in->lwm);
	MLX5_SET(srqc,	 srqc, pd,	      in->pd);
	MLX5_SET64(srqc, srqc, dbr_addr,      in->db_record);
	MLX5_SET(srqc,	 srqc, xrcd,	      in->xrcd);
	MLX5_SET(srqc,	 srqc, cqn,	      in->cqn);
}

static void get_wq(void *wq, struct mlx5_srq_attr *in)
{
	if (MLX5_GET(wq, wq, wq_signature))
		in->flags &= MLX5_SRQ_FLAG_WQ_SIG;
	in->log_page_size = MLX5_GET(wq,   wq, log_wq_pg_sz);
	in->wqe_shift	  = MLX5_GET(wq,   wq, log_wq_stride) - 4;
	in->log_size	  = MLX5_GET(wq,   wq, log_wq_sz);
	in->page_offset   = MLX5_GET(wq,   wq, page_offset);
	in->lwm		  = MLX5_GET(wq,   wq, lwm);
	in->pd		  = MLX5_GET(wq,   wq, pd);
	in->db_record	  = MLX5_GET64(wq, wq, dbr_addr);
}

static void get_srqc(void *srqc, struct mlx5_srq_attr *in)
{
	if (MLX5_GET(srqc, srqc, wq_signature))
		in->flags &= MLX5_SRQ_FLAG_WQ_SIG;
	in->log_page_size = MLX5_GET(srqc,   srqc, log_page_size);
	in->wqe_shift	  = MLX5_GET(srqc,   srqc, log_rq_stride);
	in->log_size	  = MLX5_GET(srqc,   srqc, log_srq_size);
	in->page_offset   = MLX5_GET(srqc,   srqc, page_offset);
	in->lwm		  = MLX5_GET(srqc,   srqc, lwm);
	in->pd		  = MLX5_GET(srqc,   srqc, pd);
	in->db_record	  = MLX5_GET64(srqc, srqc, dbr_addr);
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

static int create_srq_cmd(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			  struct mlx5_srq_attr *in)
{
	u32 create_out[MLX5_ST_SZ_DW(create_srq_out)] = {0};
	void *create_in;
	void *srqc;
	void *pas;
	int pas_size;
	int inlen;
	int err;

	pas_size  = get_pas_size(in);
	inlen	  = MLX5_ST_SZ_BYTES(create_srq_in) + pas_size;
	create_in = kvzalloc(inlen, GFP_KERNEL);
	if (!create_in)
		return -ENOMEM;

	srqc = MLX5_ADDR_OF(create_srq_in, create_in, srq_context_entry);
	pas = MLX5_ADDR_OF(create_srq_in, create_in, pas);

	set_srqc(srqc, in);
	memcpy(pas, in->pas, pas_size);

	MLX5_SET(create_srq_in, create_in, opcode,
		 MLX5_CMD_OP_CREATE_SRQ);

	err = mlx5_cmd_exec(dev, create_in, inlen, create_out,
			    sizeof(create_out));
	kvfree(create_in);
	if (!err)
		srq->srqn = MLX5_GET(create_srq_out, create_out, srqn);

	return err;
}

static int destroy_srq_cmd(struct mlx5_core_dev *dev,
			   struct mlx5_core_srq *srq)
{
	u32 srq_in[MLX5_ST_SZ_DW(destroy_srq_in)] = {0};
	u32 srq_out[MLX5_ST_SZ_DW(destroy_srq_out)] = {0};

	MLX5_SET(destroy_srq_in, srq_in, opcode,
		 MLX5_CMD_OP_DESTROY_SRQ);
	MLX5_SET(destroy_srq_in, srq_in, srqn, srq->srqn);

	return mlx5_cmd_exec(dev, srq_in, sizeof(srq_in),
			     srq_out, sizeof(srq_out));
}

static int arm_srq_cmd(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
		       u16 lwm, int is_srq)
{
	u32 srq_in[MLX5_ST_SZ_DW(arm_rq_in)] = {0};
	u32 srq_out[MLX5_ST_SZ_DW(arm_rq_out)] = {0};

	MLX5_SET(arm_rq_in, srq_in, opcode, MLX5_CMD_OP_ARM_RQ);
	MLX5_SET(arm_rq_in, srq_in, op_mod, MLX5_ARM_RQ_IN_OP_MOD_SRQ);
	MLX5_SET(arm_rq_in, srq_in, srq_number, srq->srqn);
	MLX5_SET(arm_rq_in, srq_in, lwm,      lwm);

	return  mlx5_cmd_exec(dev, srq_in, sizeof(srq_in),
			      srq_out, sizeof(srq_out));
}

static int query_srq_cmd(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *out)
{
	u32 srq_in[MLX5_ST_SZ_DW(query_srq_in)] = {0};
	u32 *srq_out;
	void *srqc;
	int err;

	srq_out = kvzalloc(MLX5_ST_SZ_BYTES(query_srq_out), GFP_KERNEL);
	if (!srq_out)
		return -ENOMEM;

	MLX5_SET(query_srq_in, srq_in, opcode,
		 MLX5_CMD_OP_QUERY_SRQ);
	MLX5_SET(query_srq_in, srq_in, srqn, srq->srqn);
	err =  mlx5_cmd_exec(dev, srq_in, sizeof(srq_in),
			     srq_out, MLX5_ST_SZ_BYTES(query_srq_out));
	if (err)
		goto out;

	srqc = MLX5_ADDR_OF(query_srq_out, srq_out, srq_context_entry);
	get_srqc(srqc, out);
	if (MLX5_GET(srqc, srqc, state) != MLX5_SRQC_STATE_GOOD)
		out->flags |= MLX5_SRQ_FLAG_ERR;
out:
	kvfree(srq_out);
	return err;
}

static int create_xrc_srq_cmd(struct mlx5_core_dev *dev,
			      struct mlx5_core_srq *srq,
			      struct mlx5_srq_attr *in)
{
	u32 create_out[MLX5_ST_SZ_DW(create_xrc_srq_out)];
	void *create_in;
	void *xrc_srqc;
	void *pas;
	int pas_size;
	int inlen;
	int err;

	pas_size  = get_pas_size(in);
	inlen	  = MLX5_ST_SZ_BYTES(create_xrc_srq_in) + pas_size;
	create_in = kvzalloc(inlen, GFP_KERNEL);
	if (!create_in)
		return -ENOMEM;

	xrc_srqc = MLX5_ADDR_OF(create_xrc_srq_in, create_in,
				xrc_srq_context_entry);
	pas	 = MLX5_ADDR_OF(create_xrc_srq_in, create_in, pas);

	set_srqc(xrc_srqc, in);
	MLX5_SET(xrc_srqc, xrc_srqc, user_index, in->user_index);
	memcpy(pas, in->pas, pas_size);
	MLX5_SET(create_xrc_srq_in, create_in, opcode,
		 MLX5_CMD_OP_CREATE_XRC_SRQ);

	memset(create_out, 0, sizeof(create_out));
	err = mlx5_cmd_exec(dev, create_in, inlen, create_out,
			    sizeof(create_out));
	if (err)
		goto out;

	srq->srqn = MLX5_GET(create_xrc_srq_out, create_out, xrc_srqn);
out:
	kvfree(create_in);
	return err;
}

static int destroy_xrc_srq_cmd(struct mlx5_core_dev *dev,
			       struct mlx5_core_srq *srq)
{
	u32 xrcsrq_in[MLX5_ST_SZ_DW(destroy_xrc_srq_in)]   = {0};
	u32 xrcsrq_out[MLX5_ST_SZ_DW(destroy_xrc_srq_out)] = {0};

	MLX5_SET(destroy_xrc_srq_in, xrcsrq_in, opcode,
		 MLX5_CMD_OP_DESTROY_XRC_SRQ);
	MLX5_SET(destroy_xrc_srq_in, xrcsrq_in, xrc_srqn, srq->srqn);

	return mlx5_cmd_exec(dev, xrcsrq_in, sizeof(xrcsrq_in),
			     xrcsrq_out, sizeof(xrcsrq_out));
}

static int arm_xrc_srq_cmd(struct mlx5_core_dev *dev,
			   struct mlx5_core_srq *srq, u16 lwm)
{
	u32 xrcsrq_in[MLX5_ST_SZ_DW(arm_xrc_srq_in)]   = {0};
	u32 xrcsrq_out[MLX5_ST_SZ_DW(arm_xrc_srq_out)] = {0};

	MLX5_SET(arm_xrc_srq_in, xrcsrq_in, opcode,   MLX5_CMD_OP_ARM_XRC_SRQ);
	MLX5_SET(arm_xrc_srq_in, xrcsrq_in, op_mod,   MLX5_ARM_XRC_SRQ_IN_OP_MOD_XRC_SRQ);
	MLX5_SET(arm_xrc_srq_in, xrcsrq_in, xrc_srqn, srq->srqn);
	MLX5_SET(arm_xrc_srq_in, xrcsrq_in, lwm,      lwm);

	return  mlx5_cmd_exec(dev, xrcsrq_in, sizeof(xrcsrq_in),
			      xrcsrq_out, sizeof(xrcsrq_out));
}

static int query_xrc_srq_cmd(struct mlx5_core_dev *dev,
			     struct mlx5_core_srq *srq,
			     struct mlx5_srq_attr *out)
{
	u32 xrcsrq_in[MLX5_ST_SZ_DW(query_xrc_srq_in)];
	u32 *xrcsrq_out;
	void *xrc_srqc;
	int err;

	xrcsrq_out = kvzalloc(MLX5_ST_SZ_BYTES(query_xrc_srq_out), GFP_KERNEL);
	if (!xrcsrq_out)
		return -ENOMEM;
	memset(xrcsrq_in, 0, sizeof(xrcsrq_in));

	MLX5_SET(query_xrc_srq_in, xrcsrq_in, opcode,
		 MLX5_CMD_OP_QUERY_XRC_SRQ);
	MLX5_SET(query_xrc_srq_in, xrcsrq_in, xrc_srqn, srq->srqn);

	err =  mlx5_cmd_exec(dev, xrcsrq_in, sizeof(xrcsrq_in), xrcsrq_out,
			     MLX5_ST_SZ_BYTES(query_xrc_srq_out));
	if (err)
		goto out;

	xrc_srqc = MLX5_ADDR_OF(query_xrc_srq_out, xrcsrq_out,
				xrc_srq_context_entry);
	get_srqc(xrc_srqc, out);
	if (MLX5_GET(xrc_srqc, xrc_srqc, state) != MLX5_XRC_SRQC_STATE_GOOD)
		out->flags |= MLX5_SRQ_FLAG_ERR;

out:
	kvfree(xrcsrq_out);
	return err;
}

static int create_rmp_cmd(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			  struct mlx5_srq_attr *in)
{
	void *create_in;
	void *rmpc;
	void *wq;
	int pas_size;
	int inlen;
	int err;

	pas_size = get_pas_size(in);
	inlen = MLX5_ST_SZ_BYTES(create_rmp_in) + pas_size;
	create_in = kvzalloc(inlen, GFP_KERNEL);
	if (!create_in)
		return -ENOMEM;

	rmpc = MLX5_ADDR_OF(create_rmp_in, create_in, ctx);
	wq = MLX5_ADDR_OF(rmpc, rmpc, wq);

	MLX5_SET(rmpc, rmpc, state, MLX5_RMPC_STATE_RDY);
	set_wq(wq, in);
	memcpy(MLX5_ADDR_OF(rmpc, rmpc, wq.pas), in->pas, pas_size);

	err = mlx5_core_create_rmp(dev, create_in, inlen, &srq->srqn);

	kvfree(create_in);
	return err;
}

static int destroy_rmp_cmd(struct mlx5_core_dev *dev,
			   struct mlx5_core_srq *srq)
{
	return mlx5_core_destroy_rmp(dev, srq->srqn);
}

static int arm_rmp_cmd(struct mlx5_core_dev *dev,
		       struct mlx5_core_srq *srq,
		       u16 lwm)
{
	void *in;
	void *rmpc;
	void *wq;
	void *bitmask;
	int err;

	in = kvzalloc(MLX5_ST_SZ_BYTES(modify_rmp_in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rmpc =	  MLX5_ADDR_OF(modify_rmp_in,   in,   ctx);
	bitmask = MLX5_ADDR_OF(modify_rmp_in,   in,   bitmask);
	wq   =	  MLX5_ADDR_OF(rmpc,	        rmpc, wq);

	MLX5_SET(modify_rmp_in, in,	 rmp_state, MLX5_RMPC_STATE_RDY);
	MLX5_SET(modify_rmp_in, in,	 rmpn,      srq->srqn);
	MLX5_SET(wq,		wq,	 lwm,	    lwm);
	MLX5_SET(rmp_bitmask,	bitmask, lwm,	    1);
	MLX5_SET(rmpc, rmpc, state, MLX5_RMPC_STATE_RDY);

	err = mlx5_core_modify_rmp(dev, in, MLX5_ST_SZ_BYTES(modify_rmp_in));

	kvfree(in);
	return err;
}

static int query_rmp_cmd(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *out)
{
	u32 *rmp_out;
	void *rmpc;
	int err;

	rmp_out =  kvzalloc(MLX5_ST_SZ_BYTES(query_rmp_out), GFP_KERNEL);
	if (!rmp_out)
		return -ENOMEM;

	err = mlx5_core_query_rmp(dev, srq->srqn, rmp_out);
	if (err)
		goto out;

	rmpc = MLX5_ADDR_OF(query_rmp_out, rmp_out, rmp_context);
	get_wq(MLX5_ADDR_OF(rmpc, rmpc, wq), out);
	if (MLX5_GET(rmpc, rmpc, state) != MLX5_RMPC_STATE_RDY)
		out->flags |= MLX5_SRQ_FLAG_ERR;

out:
	kvfree(rmp_out);
	return err;
}

static int create_srq_split(struct mlx5_core_dev *dev,
			    struct mlx5_core_srq *srq,
			    struct mlx5_srq_attr *in)
{
	if (!dev->issi)
		return create_srq_cmd(dev, srq, in);
	else if (srq->common.res == MLX5_RES_XSRQ)
		return create_xrc_srq_cmd(dev, srq, in);
	else
		return create_rmp_cmd(dev, srq, in);
}

static int destroy_srq_split(struct mlx5_core_dev *dev,
			     struct mlx5_core_srq *srq)
{
	if (!dev->issi)
		return destroy_srq_cmd(dev, srq);
	else if (srq->common.res == MLX5_RES_XSRQ)
		return destroy_xrc_srq_cmd(dev, srq);
	else
		return destroy_rmp_cmd(dev, srq);
}

int mlx5_core_create_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *in)
{
	int err;
	struct mlx5_srq_table *table = &dev->priv.srq_table;

	if (in->type == IB_SRQT_XRC)
		srq->common.res = MLX5_RES_XSRQ;
	else
		srq->common.res = MLX5_RES_SRQ;

	err = create_srq_split(dev, srq, in);
	if (err)
		return err;

	atomic_set(&srq->refcount, 1);
	init_completion(&srq->free);

	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, srq->srqn, srq);
	spin_unlock_irq(&table->lock);
	if (err) {
		mlx5_core_warn(dev, "err %d, srqn 0x%x\n", err, srq->srqn);
		goto err_destroy_srq_split;
	}

	return 0;

err_destroy_srq_split:
	destroy_srq_split(dev, srq);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_srq);

int mlx5_core_destroy_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq)
{
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

	err = destroy_srq_split(dev, srq);
	if (err)
		return err;

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	return 0;
}
EXPORT_SYMBOL(mlx5_core_destroy_srq);

int mlx5_core_query_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_srq_attr *out)
{
	if (!dev->issi)
		return query_srq_cmd(dev, srq, out);
	else if (srq->common.res == MLX5_RES_XSRQ)
		return query_xrc_srq_cmd(dev, srq, out);
	else
		return query_rmp_cmd(dev, srq, out);
}
EXPORT_SYMBOL(mlx5_core_query_srq);

int mlx5_core_arm_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
		      u16 lwm, int is_srq)
{
	if (!dev->issi)
		return arm_srq_cmd(dev, srq, lwm, is_srq);
	else if (srq->common.res == MLX5_RES_XSRQ)
		return arm_xrc_srq_cmd(dev, srq, lwm);
	else
		return arm_rmp_cmd(dev, srq, lwm);
}
EXPORT_SYMBOL(mlx5_core_arm_srq);

void mlx5_init_srq_table(struct mlx5_core_dev *dev)
{
	struct mlx5_srq_table *table = &dev->priv.srq_table;

	memset(table, 0, sizeof(*table));
	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
}

void mlx5_cleanup_srq_table(struct mlx5_core_dev *dev)
{
	/* nothing */
}
