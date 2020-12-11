// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2013-2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/mlx5/driver.h>
#include "mlx5_ib.h"
#include "srq.h"
#include "qp.h"

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
	u32 rq_num_pas    = DIV_ROUND_UP(rq_sz_po, page_size);

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

struct mlx5_core_srq *mlx5_cmd_get_srq(struct mlx5_ib_dev *dev, u32 srqn)
{
	struct mlx5_srq_table *table = &dev->srq_table;
	struct mlx5_core_srq *srq;

	xa_lock_irq(&table->array);
	srq = xa_load(&table->array, srqn);
	if (srq)
		refcount_inc(&srq->common.refcount);
	xa_unlock_irq(&table->array);

	return srq;
}

static int create_srq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
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

	MLX5_SET(create_srq_in, create_in, uid, in->uid);
	srqc = MLX5_ADDR_OF(create_srq_in, create_in, srq_context_entry);
	pas = MLX5_ADDR_OF(create_srq_in, create_in, pas);

	set_srqc(srqc, in);
	memcpy(pas, in->pas, pas_size);

	MLX5_SET(create_srq_in, create_in, opcode,
		 MLX5_CMD_OP_CREATE_SRQ);

	err = mlx5_cmd_exec(dev->mdev, create_in, inlen, create_out,
			    sizeof(create_out));
	kvfree(create_in);
	if (!err) {
		srq->srqn = MLX5_GET(create_srq_out, create_out, srqn);
		srq->uid = in->uid;
	}

	return err;
}

static int destroy_srq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_srq_in)] = {};

	MLX5_SET(destroy_srq_in, in, opcode, MLX5_CMD_OP_DESTROY_SRQ);
	MLX5_SET(destroy_srq_in, in, srqn, srq->srqn);
	MLX5_SET(destroy_srq_in, in, uid, srq->uid);

	return mlx5_cmd_exec_in(dev->mdev, destroy_srq, in);
}

static int arm_srq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		       u16 lwm, int is_srq)
{
	u32 in[MLX5_ST_SZ_DW(arm_rq_in)] = {};

	MLX5_SET(arm_rq_in, in, opcode, MLX5_CMD_OP_ARM_RQ);
	MLX5_SET(arm_rq_in, in, op_mod, MLX5_ARM_RQ_IN_OP_MOD_SRQ);
	MLX5_SET(arm_rq_in, in, srq_number, srq->srqn);
	MLX5_SET(arm_rq_in, in, lwm, lwm);
	MLX5_SET(arm_rq_in, in, uid, srq->uid);

	return mlx5_cmd_exec_in(dev->mdev, arm_rq, in);
}

static int query_srq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *out)
{
	u32 in[MLX5_ST_SZ_DW(query_srq_in)] = {};
	u32 *srq_out;
	void *srqc;
	int err;

	srq_out = kvzalloc(MLX5_ST_SZ_BYTES(query_srq_out), GFP_KERNEL);
	if (!srq_out)
		return -ENOMEM;

	MLX5_SET(query_srq_in, in, opcode, MLX5_CMD_OP_QUERY_SRQ);
	MLX5_SET(query_srq_in, in, srqn, srq->srqn);
	err = mlx5_cmd_exec_inout(dev->mdev, query_srq, in, srq_out);
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

static int create_xrc_srq_cmd(struct mlx5_ib_dev *dev,
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

	MLX5_SET(create_xrc_srq_in, create_in, uid, in->uid);
	xrc_srqc = MLX5_ADDR_OF(create_xrc_srq_in, create_in,
				xrc_srq_context_entry);
	pas	 = MLX5_ADDR_OF(create_xrc_srq_in, create_in, pas);

	set_srqc(xrc_srqc, in);
	MLX5_SET(xrc_srqc, xrc_srqc, user_index, in->user_index);
	memcpy(pas, in->pas, pas_size);
	MLX5_SET(create_xrc_srq_in, create_in, opcode,
		 MLX5_CMD_OP_CREATE_XRC_SRQ);

	memset(create_out, 0, sizeof(create_out));
	err = mlx5_cmd_exec(dev->mdev, create_in, inlen, create_out,
			    sizeof(create_out));
	if (err)
		goto out;

	srq->srqn = MLX5_GET(create_xrc_srq_out, create_out, xrc_srqn);
	srq->uid = in->uid;
out:
	kvfree(create_in);
	return err;
}

static int destroy_xrc_srq_cmd(struct mlx5_ib_dev *dev,
			       struct mlx5_core_srq *srq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_xrc_srq_in)] = {};

	MLX5_SET(destroy_xrc_srq_in, in, opcode, MLX5_CMD_OP_DESTROY_XRC_SRQ);
	MLX5_SET(destroy_xrc_srq_in, in, xrc_srqn, srq->srqn);
	MLX5_SET(destroy_xrc_srq_in, in, uid, srq->uid);

	return mlx5_cmd_exec_in(dev->mdev, destroy_xrc_srq, in);
}

static int arm_xrc_srq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			   u16 lwm)
{
	u32 in[MLX5_ST_SZ_DW(arm_xrc_srq_in)] = {};

	MLX5_SET(arm_xrc_srq_in, in, opcode, MLX5_CMD_OP_ARM_XRC_SRQ);
	MLX5_SET(arm_xrc_srq_in, in, op_mod,
		 MLX5_ARM_XRC_SRQ_IN_OP_MOD_XRC_SRQ);
	MLX5_SET(arm_xrc_srq_in, in, xrc_srqn, srq->srqn);
	MLX5_SET(arm_xrc_srq_in, in, lwm, lwm);
	MLX5_SET(arm_xrc_srq_in, in, uid, srq->uid);

	return  mlx5_cmd_exec_in(dev->mdev, arm_xrc_srq, in);
}

static int query_xrc_srq_cmd(struct mlx5_ib_dev *dev,
			     struct mlx5_core_srq *srq,
			     struct mlx5_srq_attr *out)
{
	u32 in[MLX5_ST_SZ_DW(query_xrc_srq_in)] = {};
	u32 *xrcsrq_out;
	void *xrc_srqc;
	int err;

	xrcsrq_out = kvzalloc(MLX5_ST_SZ_BYTES(query_xrc_srq_out), GFP_KERNEL);
	if (!xrcsrq_out)
		return -ENOMEM;

	MLX5_SET(query_xrc_srq_in, in, opcode, MLX5_CMD_OP_QUERY_XRC_SRQ);
	MLX5_SET(query_xrc_srq_in, in, xrc_srqn, srq->srqn);

	err = mlx5_cmd_exec_inout(dev->mdev, query_xrc_srq, in, xrcsrq_out);
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

static int create_rmp_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			  struct mlx5_srq_attr *in)
{
	void *create_out = NULL;
	void *create_in = NULL;
	void *rmpc;
	void *wq;
	int pas_size;
	int outlen;
	int inlen;
	int err;

	pas_size = get_pas_size(in);
	inlen = MLX5_ST_SZ_BYTES(create_rmp_in) + pas_size;
	outlen = MLX5_ST_SZ_BYTES(create_rmp_out);
	create_in = kvzalloc(inlen, GFP_KERNEL);
	create_out = kvzalloc(outlen, GFP_KERNEL);
	if (!create_in || !create_out) {
		err = -ENOMEM;
		goto out;
	}

	rmpc = MLX5_ADDR_OF(create_rmp_in, create_in, ctx);
	wq = MLX5_ADDR_OF(rmpc, rmpc, wq);

	MLX5_SET(rmpc, rmpc, state, MLX5_RMPC_STATE_RDY);
	MLX5_SET(create_rmp_in, create_in, uid, in->uid);
	set_wq(wq, in);
	memcpy(MLX5_ADDR_OF(rmpc, rmpc, wq.pas), in->pas, pas_size);

	MLX5_SET(create_rmp_in, create_in, opcode, MLX5_CMD_OP_CREATE_RMP);
	err = mlx5_cmd_exec(dev->mdev, create_in, inlen, create_out, outlen);
	if (!err) {
		srq->srqn = MLX5_GET(create_rmp_out, create_out, rmpn);
		srq->uid = in->uid;
	}

out:
	kvfree(create_in);
	kvfree(create_out);
	return err;
}

static int destroy_rmp_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rmp_in)] = {};

	MLX5_SET(destroy_rmp_in, in, opcode, MLX5_CMD_OP_DESTROY_RMP);
	MLX5_SET(destroy_rmp_in, in, rmpn, srq->srqn);
	MLX5_SET(destroy_rmp_in, in, uid, srq->uid);
	return mlx5_cmd_exec_in(dev->mdev, destroy_rmp, in);
}

static int arm_rmp_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		       u16 lwm)
{
	void *out = NULL;
	void *in = NULL;
	void *rmpc;
	void *wq;
	void *bitmask;
	int outlen;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rmp_in);
	outlen = MLX5_ST_SZ_BYTES(modify_rmp_out);

	in = kvzalloc(inlen, GFP_KERNEL);
	out = kvzalloc(outlen, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	rmpc =	  MLX5_ADDR_OF(modify_rmp_in,   in,   ctx);
	bitmask = MLX5_ADDR_OF(modify_rmp_in,   in,   bitmask);
	wq   =	  MLX5_ADDR_OF(rmpc,	        rmpc, wq);

	MLX5_SET(modify_rmp_in, in,	 rmp_state, MLX5_RMPC_STATE_RDY);
	MLX5_SET(modify_rmp_in, in,	 rmpn,      srq->srqn);
	MLX5_SET(modify_rmp_in, in, uid, srq->uid);
	MLX5_SET(wq,		wq,	 lwm,	    lwm);
	MLX5_SET(rmp_bitmask,	bitmask, lwm,	    1);
	MLX5_SET(rmpc, rmpc, state, MLX5_RMPC_STATE_RDY);
	MLX5_SET(modify_rmp_in, in, opcode, MLX5_CMD_OP_MODIFY_RMP);

	err = mlx5_cmd_exec_inout(dev->mdev, modify_rmp, in, out);

out:
	kvfree(in);
	kvfree(out);
	return err;
}

static int query_rmp_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *out)
{
	u32 *rmp_out = NULL;
	u32 *rmp_in = NULL;
	void *rmpc;
	int outlen;
	int inlen;
	int err;

	outlen = MLX5_ST_SZ_BYTES(query_rmp_out);
	inlen = MLX5_ST_SZ_BYTES(query_rmp_in);

	rmp_out = kvzalloc(outlen, GFP_KERNEL);
	rmp_in = kvzalloc(inlen, GFP_KERNEL);
	if (!rmp_out || !rmp_in) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(query_rmp_in, rmp_in, opcode, MLX5_CMD_OP_QUERY_RMP);
	MLX5_SET(query_rmp_in, rmp_in, rmpn,   srq->srqn);
	err = mlx5_cmd_exec_inout(dev->mdev, query_rmp, rmp_in, rmp_out);
	if (err)
		goto out;

	rmpc = MLX5_ADDR_OF(query_rmp_out, rmp_out, rmp_context);
	get_wq(MLX5_ADDR_OF(rmpc, rmpc, wq), out);
	if (MLX5_GET(rmpc, rmpc, state) != MLX5_RMPC_STATE_RDY)
		out->flags |= MLX5_SRQ_FLAG_ERR;

out:
	kvfree(rmp_out);
	kvfree(rmp_in);
	return err;
}

static int create_xrq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			  struct mlx5_srq_attr *in)
{
	u32 create_out[MLX5_ST_SZ_DW(create_xrq_out)] = {0};
	void *create_in;
	void *xrqc;
	void *wq;
	int pas_size;
	int inlen;
	int err;

	pas_size = get_pas_size(in);
	inlen = MLX5_ST_SZ_BYTES(create_xrq_in) + pas_size;
	create_in = kvzalloc(inlen, GFP_KERNEL);
	if (!create_in)
		return -ENOMEM;

	xrqc = MLX5_ADDR_OF(create_xrq_in, create_in, xrq_context);
	wq = MLX5_ADDR_OF(xrqc, xrqc, wq);

	set_wq(wq, in);
	memcpy(MLX5_ADDR_OF(xrqc, xrqc, wq.pas), in->pas, pas_size);

	if (in->type == IB_SRQT_TM) {
		MLX5_SET(xrqc, xrqc, topology, MLX5_XRQC_TOPOLOGY_TAG_MATCHING);
		if (in->flags & MLX5_SRQ_FLAG_RNDV)
			MLX5_SET(xrqc, xrqc, offload, MLX5_XRQC_OFFLOAD_RNDV);
		MLX5_SET(xrqc, xrqc,
			 tag_matching_topology_context.log_matching_list_sz,
			 in->tm_log_list_size);
	}
	MLX5_SET(xrqc, xrqc, user_index, in->user_index);
	MLX5_SET(xrqc, xrqc, cqn, in->cqn);
	MLX5_SET(create_xrq_in, create_in, opcode, MLX5_CMD_OP_CREATE_XRQ);
	MLX5_SET(create_xrq_in, create_in, uid, in->uid);
	err = mlx5_cmd_exec(dev->mdev, create_in, inlen, create_out,
			    sizeof(create_out));
	kvfree(create_in);
	if (!err) {
		srq->srqn = MLX5_GET(create_xrq_out, create_out, xrqn);
		srq->uid = in->uid;
	}

	return err;
}

static int destroy_xrq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq)
{
	u32 in[MLX5_ST_SZ_DW(destroy_xrq_in)] = {};

	MLX5_SET(destroy_xrq_in, in, opcode, MLX5_CMD_OP_DESTROY_XRQ);
	MLX5_SET(destroy_xrq_in, in, xrqn, srq->srqn);
	MLX5_SET(destroy_xrq_in, in, uid, srq->uid);

	return mlx5_cmd_exec_in(dev->mdev, destroy_xrq, in);
}

static int arm_xrq_cmd(struct mlx5_ib_dev *dev,
		       struct mlx5_core_srq *srq,
		       u16 lwm)
{
	u32 in[MLX5_ST_SZ_DW(arm_rq_in)] = {};

	MLX5_SET(arm_rq_in, in, opcode, MLX5_CMD_OP_ARM_RQ);
	MLX5_SET(arm_rq_in, in, op_mod, MLX5_ARM_RQ_IN_OP_MOD_XRQ);
	MLX5_SET(arm_rq_in, in, srq_number, srq->srqn);
	MLX5_SET(arm_rq_in, in, lwm, lwm);
	MLX5_SET(arm_rq_in, in, uid, srq->uid);

	return mlx5_cmd_exec_in(dev->mdev, arm_rq, in);
}

static int query_xrq_cmd(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *out)
{
	u32 in[MLX5_ST_SZ_DW(query_xrq_in)] = {};
	u32 *xrq_out;
	int outlen = MLX5_ST_SZ_BYTES(query_xrq_out);
	void *xrqc;
	int err;

	xrq_out = kvzalloc(outlen, GFP_KERNEL);
	if (!xrq_out)
		return -ENOMEM;

	MLX5_SET(query_xrq_in, in, opcode, MLX5_CMD_OP_QUERY_XRQ);
	MLX5_SET(query_xrq_in, in, xrqn, srq->srqn);

	err = mlx5_cmd_exec_inout(dev->mdev, query_xrq, in, xrq_out);
	if (err)
		goto out;

	xrqc = MLX5_ADDR_OF(query_xrq_out, xrq_out, xrq_context);
	get_wq(MLX5_ADDR_OF(xrqc, xrqc, wq), out);
	if (MLX5_GET(xrqc, xrqc, state) != MLX5_XRQC_STATE_GOOD)
		out->flags |= MLX5_SRQ_FLAG_ERR;
	out->tm_next_tag =
		MLX5_GET(xrqc, xrqc,
			 tag_matching_topology_context.append_next_index);
	out->tm_hw_phase_cnt =
		MLX5_GET(xrqc, xrqc,
			 tag_matching_topology_context.hw_phase_cnt);
	out->tm_sw_phase_cnt =
		MLX5_GET(xrqc, xrqc,
			 tag_matching_topology_context.sw_phase_cnt);

out:
	kvfree(xrq_out);
	return err;
}

static int create_srq_split(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			    struct mlx5_srq_attr *in)
{
	if (!dev->mdev->issi)
		return create_srq_cmd(dev, srq, in);
	switch (srq->common.res) {
	case MLX5_RES_XSRQ:
		return create_xrc_srq_cmd(dev, srq, in);
	case MLX5_RES_XRQ:
		return create_xrq_cmd(dev, srq, in);
	default:
		return create_rmp_cmd(dev, srq, in);
	}
}

static int destroy_srq_split(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq)
{
	if (!dev->mdev->issi)
		return destroy_srq_cmd(dev, srq);
	switch (srq->common.res) {
	case MLX5_RES_XSRQ:
		return destroy_xrc_srq_cmd(dev, srq);
	case MLX5_RES_XRQ:
		return destroy_xrq_cmd(dev, srq);
	default:
		return destroy_rmp_cmd(dev, srq);
	}
}

int mlx5_cmd_create_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_srq_attr *in)
{
	struct mlx5_srq_table *table = &dev->srq_table;
	int err;

	switch (in->type) {
	case IB_SRQT_XRC:
		srq->common.res = MLX5_RES_XSRQ;
		break;
	case IB_SRQT_TM:
		srq->common.res = MLX5_RES_XRQ;
		break;
	default:
		srq->common.res = MLX5_RES_SRQ;
	}

	err = create_srq_split(dev, srq, in);
	if (err)
		return err;

	refcount_set(&srq->common.refcount, 1);
	init_completion(&srq->common.free);

	err = xa_err(xa_store_irq(&table->array, srq->srqn, srq, GFP_KERNEL));
	if (err)
		goto err_destroy_srq_split;

	return 0;

err_destroy_srq_split:
	destroy_srq_split(dev, srq);

	return err;
}

int mlx5_cmd_destroy_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq)
{
	struct mlx5_srq_table *table = &dev->srq_table;
	struct mlx5_core_srq *tmp;
	int err;

	/* Delete entry, but leave index occupied */
	tmp = xa_cmpxchg_irq(&table->array, srq->srqn, srq, XA_ZERO_ENTRY, 0);
	if (WARN_ON(tmp != srq))
		return xa_err(tmp) ?: -EINVAL;

	err = destroy_srq_split(dev, srq);
	if (err) {
		/*
		 * We don't need to check returned result for an error,
		 * because  we are storing in pre-allocated space xarray
		 * entry and it can't fail at this stage.
		 */
		xa_cmpxchg_irq(&table->array, srq->srqn, XA_ZERO_ENTRY, srq, 0);
		return err;
	}
	xa_erase_irq(&table->array, srq->srqn);

	mlx5_core_res_put(&srq->common);
	wait_for_completion(&srq->common.free);
	return 0;
}

int mlx5_cmd_query_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		       struct mlx5_srq_attr *out)
{
	if (!dev->mdev->issi)
		return query_srq_cmd(dev, srq, out);
	switch (srq->common.res) {
	case MLX5_RES_XSRQ:
		return query_xrc_srq_cmd(dev, srq, out);
	case MLX5_RES_XRQ:
		return query_xrq_cmd(dev, srq, out);
	default:
		return query_rmp_cmd(dev, srq, out);
	}
}

int mlx5_cmd_arm_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		     u16 lwm, int is_srq)
{
	if (!dev->mdev->issi)
		return arm_srq_cmd(dev, srq, lwm, is_srq);
	switch (srq->common.res) {
	case MLX5_RES_XSRQ:
		return arm_xrc_srq_cmd(dev, srq, lwm);
	case MLX5_RES_XRQ:
		return arm_xrq_cmd(dev, srq, lwm);
	default:
		return arm_rmp_cmd(dev, srq, lwm);
	}
}

static int srq_event_notifier(struct notifier_block *nb,
			      unsigned long type, void *data)
{
	struct mlx5_srq_table *table;
	struct mlx5_core_srq *srq;
	struct mlx5_eqe *eqe;
	u32 srqn;

	if (type != MLX5_EVENT_TYPE_SRQ_CATAS_ERROR &&
	    type != MLX5_EVENT_TYPE_SRQ_RQ_LIMIT)
		return NOTIFY_DONE;

	table = container_of(nb, struct mlx5_srq_table, nb);

	eqe = data;
	srqn = be32_to_cpu(eqe->data.qp_srq.qp_srq_n) & 0xffffff;

	xa_lock(&table->array);
	srq = xa_load(&table->array, srqn);
	if (srq)
		refcount_inc(&srq->common.refcount);
	xa_unlock(&table->array);

	if (!srq)
		return NOTIFY_OK;

	srq->event(srq, eqe->type);

	mlx5_core_res_put(&srq->common);

	return NOTIFY_OK;
}

int mlx5_init_srq_table(struct mlx5_ib_dev *dev)
{
	struct mlx5_srq_table *table = &dev->srq_table;

	memset(table, 0, sizeof(*table));
	xa_init_flags(&table->array, XA_FLAGS_LOCK_IRQ);

	table->nb.notifier_call = srq_event_notifier;
	mlx5_notifier_register(dev->mdev, &table->nb);

	return 0;
}

void mlx5_cleanup_srq_table(struct mlx5_ib_dev *dev)
{
	struct mlx5_srq_table *table = &dev->srq_table;

	mlx5_notifier_unregister(dev->mdev, &table->nb);
}
