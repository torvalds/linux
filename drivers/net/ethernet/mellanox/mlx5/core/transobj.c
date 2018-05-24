/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include <linux/mlx5/transobj.h>

int mlx5_core_alloc_transport_domain(struct mlx5_core_dev *dev, u32 *tdn)
{
	u32 in[MLX5_ST_SZ_DW(alloc_transport_domain_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(alloc_transport_domain_out)] = {0};
	int err;

	MLX5_SET(alloc_transport_domain_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*tdn = MLX5_GET(alloc_transport_domain_out, out,
				transport_domain);

	return err;
}
EXPORT_SYMBOL(mlx5_core_alloc_transport_domain);

void mlx5_core_dealloc_transport_domain(struct mlx5_core_dev *dev, u32 tdn)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_transport_domain_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(dealloc_transport_domain_out)] = {0};

	MLX5_SET(dealloc_transport_domain_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(dealloc_transport_domain_in, in, transport_domain, tdn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_dealloc_transport_domain);

int mlx5_core_create_rq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *rqn)
{
	u32 out[MLX5_ST_SZ_DW(create_rq_out)] = {0};
	int err;

	MLX5_SET(create_rq_in, in, opcode, MLX5_CMD_OP_CREATE_RQ);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*rqn = MLX5_GET(create_rq_out, out, rqn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_rq);

int mlx5_core_modify_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_rq_out)];

	MLX5_SET(modify_rq_in, in, rqn, rqn);
	MLX5_SET(modify_rq_in, in, opcode, MLX5_CMD_OP_MODIFY_RQ);

	memset(out, 0, sizeof(out));
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_modify_rq);

void mlx5_core_destroy_rq(struct mlx5_core_dev *dev, u32 rqn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rq_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_rq_out)] = {0};

	MLX5_SET(destroy_rq_in, in, opcode, MLX5_CMD_OP_DESTROY_RQ);
	MLX5_SET(destroy_rq_in, in, rqn, rqn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_destroy_rq);

int mlx5_core_query_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *out)
{
	u32 in[MLX5_ST_SZ_DW(query_rq_in)] = {0};
	int outlen = MLX5_ST_SZ_BYTES(query_rq_out);

	MLX5_SET(query_rq_in, in, opcode, MLX5_CMD_OP_QUERY_RQ);
	MLX5_SET(query_rq_in, in, rqn, rqn);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}
EXPORT_SYMBOL(mlx5_core_query_rq);

int mlx5_core_create_sq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *sqn)
{
	u32 out[MLX5_ST_SZ_DW(create_sq_out)] = {0};
	int err;

	MLX5_SET(create_sq_in, in, opcode, MLX5_CMD_OP_CREATE_SQ);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*sqn = MLX5_GET(create_sq_out, out, sqn);

	return err;
}

int mlx5_core_modify_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_sq_out)] = {0};

	MLX5_SET(modify_sq_in, in, sqn, sqn);
	MLX5_SET(modify_sq_in, in, opcode, MLX5_CMD_OP_MODIFY_SQ);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_modify_sq);

void mlx5_core_destroy_sq(struct mlx5_core_dev *dev, u32 sqn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_sq_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_sq_out)] = {0};

	MLX5_SET(destroy_sq_in, in, opcode, MLX5_CMD_OP_DESTROY_SQ);
	MLX5_SET(destroy_sq_in, in, sqn, sqn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_core_query_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *out)
{
	u32 in[MLX5_ST_SZ_DW(query_sq_in)] = {0};
	int outlen = MLX5_ST_SZ_BYTES(query_sq_out);

	MLX5_SET(query_sq_in, in, opcode, MLX5_CMD_OP_QUERY_SQ);
	MLX5_SET(query_sq_in, in, sqn, sqn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}
EXPORT_SYMBOL(mlx5_core_query_sq);

int mlx5_core_query_sq_state(struct mlx5_core_dev *dev, u32 sqn, u8 *state)
{
	void *out;
	void *sqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(query_sq_out);
	out = kvzalloc(inlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_query_sq(dev, sqn, out);
	if (err)
		goto out;

	sqc = MLX5_ADDR_OF(query_sq_out, out, sq_context);
	*state = MLX5_GET(sqc, sqc, state);

out:
	kvfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_query_sq_state);

int mlx5_core_create_tir(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *tirn)
{
	u32 out[MLX5_ST_SZ_DW(create_tir_out)] = {0};
	int err;

	MLX5_SET(create_tir_in, in, opcode, MLX5_CMD_OP_CREATE_TIR);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*tirn = MLX5_GET(create_tir_out, out, tirn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_tir);

int mlx5_core_modify_tir(struct mlx5_core_dev *dev, u32 tirn, u32 *in,
			 int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_tir_out)] = {0};

	MLX5_SET(modify_tir_in, in, tirn, tirn);
	MLX5_SET(modify_tir_in, in, opcode, MLX5_CMD_OP_MODIFY_TIR);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}

void mlx5_core_destroy_tir(struct mlx5_core_dev *dev, u32 tirn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tir_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_tir_out)] = {0};

	MLX5_SET(destroy_tir_in, in, opcode, MLX5_CMD_OP_DESTROY_TIR);
	MLX5_SET(destroy_tir_in, in, tirn, tirn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_destroy_tir);

int mlx5_core_create_tis(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *tisn)
{
	u32 out[MLX5_ST_SZ_DW(create_tis_out)] = {0};
	int err;

	MLX5_SET(create_tis_in, in, opcode, MLX5_CMD_OP_CREATE_TIS);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*tisn = MLX5_GET(create_tis_out, out, tisn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_tis);

int mlx5_core_modify_tis(struct mlx5_core_dev *dev, u32 tisn, u32 *in,
			 int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_tis_out)] = {0};

	MLX5_SET(modify_tis_in, in, tisn, tisn);
	MLX5_SET(modify_tis_in, in, opcode, MLX5_CMD_OP_MODIFY_TIS);

	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_modify_tis);

void mlx5_core_destroy_tis(struct mlx5_core_dev *dev, u32 tisn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tis_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_tis_out)] = {0};

	MLX5_SET(destroy_tis_in, in, opcode, MLX5_CMD_OP_DESTROY_TIS);
	MLX5_SET(destroy_tis_in, in, tisn, tisn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_destroy_tis);

int mlx5_core_create_rmp(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *rmpn)
{
	u32 out[MLX5_ST_SZ_DW(create_rmp_out)] = {0};
	int err;

	MLX5_SET(create_rmp_in, in, opcode, MLX5_CMD_OP_CREATE_RMP);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*rmpn = MLX5_GET(create_rmp_out, out, rmpn);

	return err;
}

int mlx5_core_modify_rmp(struct mlx5_core_dev *dev, u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_rmp_out)] = {0};

	MLX5_SET(modify_rmp_in, in, opcode, MLX5_CMD_OP_MODIFY_RMP);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}

int mlx5_core_destroy_rmp(struct mlx5_core_dev *dev, u32 rmpn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rmp_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_rmp_out)] = {0};

	MLX5_SET(destroy_rmp_in, in, opcode, MLX5_CMD_OP_DESTROY_RMP);
	MLX5_SET(destroy_rmp_in, in, rmpn, rmpn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out,
					  sizeof(out));
}

int mlx5_core_query_rmp(struct mlx5_core_dev *dev, u32 rmpn, u32 *out)
{
	u32 in[MLX5_ST_SZ_DW(query_rmp_in)] = {0};
	int outlen = MLX5_ST_SZ_BYTES(query_rmp_out);

	MLX5_SET(query_rmp_in, in, opcode, MLX5_CMD_OP_QUERY_RMP);
	MLX5_SET(query_rmp_in, in, rmpn,   rmpn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

int mlx5_core_arm_rmp(struct mlx5_core_dev *dev, u32 rmpn, u16 lwm)
{
	void *in;
	void *rmpc;
	void *wq;
	void *bitmask;
	int  err;

	in = kvzalloc(MLX5_ST_SZ_BYTES(modify_rmp_in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rmpc    = MLX5_ADDR_OF(modify_rmp_in,   in,   ctx);
	bitmask = MLX5_ADDR_OF(modify_rmp_in,   in,   bitmask);
	wq      = MLX5_ADDR_OF(rmpc,	        rmpc, wq);

	MLX5_SET(modify_rmp_in, in,	 rmp_state, MLX5_RMPC_STATE_RDY);
	MLX5_SET(modify_rmp_in, in,	 rmpn,      rmpn);
	MLX5_SET(wq,		wq,	 lwm,	    lwm);
	MLX5_SET(rmp_bitmask,	bitmask, lwm,	    1);
	MLX5_SET(rmpc,		rmpc,	 state,	    MLX5_RMPC_STATE_RDY);

	err =  mlx5_core_modify_rmp(dev, in, MLX5_ST_SZ_BYTES(modify_rmp_in));

	kvfree(in);

	return err;
}

int mlx5_core_create_xsrq(struct mlx5_core_dev *dev, u32 *in, int inlen,
			  u32 *xsrqn)
{
	u32 out[MLX5_ST_SZ_DW(create_xrc_srq_out)] = {0};
	int err;

	MLX5_SET(create_xrc_srq_in, in, opcode,     MLX5_CMD_OP_CREATE_XRC_SRQ);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*xsrqn = MLX5_GET(create_xrc_srq_out, out, xrc_srqn);

	return err;
}

int mlx5_core_destroy_xsrq(struct mlx5_core_dev *dev, u32 xsrqn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_xrc_srq_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_xrc_srq_out)] = {0};

	MLX5_SET(destroy_xrc_srq_in, in, opcode,   MLX5_CMD_OP_DESTROY_XRC_SRQ);
	MLX5_SET(destroy_xrc_srq_in, in, xrc_srqn, xsrqn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_core_arm_xsrq(struct mlx5_core_dev *dev, u32 xsrqn, u16 lwm)
{
	u32 in[MLX5_ST_SZ_DW(arm_xrc_srq_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(arm_xrc_srq_out)] = {0};

	MLX5_SET(arm_xrc_srq_in, in, opcode,   MLX5_CMD_OP_ARM_XRC_SRQ);
	MLX5_SET(arm_xrc_srq_in, in, xrc_srqn, xsrqn);
	MLX5_SET(arm_xrc_srq_in, in, lwm,      lwm);
	MLX5_SET(arm_xrc_srq_in, in, op_mod,
		 MLX5_ARM_XRC_SRQ_IN_OP_MOD_XRC_SRQ);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_core_create_rqt(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *rqtn)
{
	u32 out[MLX5_ST_SZ_DW(create_rqt_out)] = {0};
	int err;

	MLX5_SET(create_rqt_in, in, opcode, MLX5_CMD_OP_CREATE_RQT);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*rqtn = MLX5_GET(create_rqt_out, out, rqtn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_rqt);

int mlx5_core_modify_rqt(struct mlx5_core_dev *dev, u32 rqtn, u32 *in,
			 int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_rqt_out)] = {0};

	MLX5_SET(modify_rqt_in, in, rqtn, rqtn);
	MLX5_SET(modify_rqt_in, in, opcode, MLX5_CMD_OP_MODIFY_RQT);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}

void mlx5_core_destroy_rqt(struct mlx5_core_dev *dev, u32 rqtn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rqt_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_rqt_out)] = {0};

	MLX5_SET(destroy_rqt_in, in, opcode, MLX5_CMD_OP_DESTROY_RQT);
	MLX5_SET(destroy_rqt_in, in, rqtn, rqtn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_destroy_rqt);

static int mlx5_hairpin_create_rq(struct mlx5_core_dev *mdev,
				  struct mlx5_hairpin_params *params, u32 *rqn)
{
	u32 in[MLX5_ST_SZ_DW(create_rq_in)] = {0};
	void *rqc, *wq;

	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	wq  = MLX5_ADDR_OF(rqc, rqc, wq);

	MLX5_SET(rqc, rqc, hairpin, 1);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RST);
	MLX5_SET(rqc, rqc, counter_set_id, params->q_counter);

	MLX5_SET(wq, wq, log_hairpin_data_sz, params->log_data_size);
	MLX5_SET(wq, wq, log_hairpin_num_packets, params->log_num_packets);

	return mlx5_core_create_rq(mdev, in, MLX5_ST_SZ_BYTES(create_rq_in), rqn);
}

static int mlx5_hairpin_create_sq(struct mlx5_core_dev *mdev,
				  struct mlx5_hairpin_params *params, u32 *sqn)
{
	u32 in[MLX5_ST_SZ_DW(create_sq_in)] = {0};
	void *sqc, *wq;

	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq  = MLX5_ADDR_OF(sqc, sqc, wq);

	MLX5_SET(sqc, sqc, hairpin, 1);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RST);

	MLX5_SET(wq, wq, log_hairpin_data_sz, params->log_data_size);
	MLX5_SET(wq, wq, log_hairpin_num_packets, params->log_num_packets);

	return mlx5_core_create_sq(mdev, in, MLX5_ST_SZ_BYTES(create_sq_in), sqn);
}

static int mlx5_hairpin_create_queues(struct mlx5_hairpin *hp,
				      struct mlx5_hairpin_params *params)
{
	int i, j, err;

	for (i = 0; i < hp->num_channels; i++) {
		err = mlx5_hairpin_create_rq(hp->func_mdev, params, &hp->rqn[i]);
		if (err)
			goto out_err_rq;
	}

	for (i = 0; i < hp->num_channels; i++) {
		err = mlx5_hairpin_create_sq(hp->peer_mdev, params, &hp->sqn[i]);
		if (err)
			goto out_err_sq;
	}

	return 0;

out_err_sq:
	for (j = 0; j < i; j++)
		mlx5_core_destroy_sq(hp->peer_mdev, hp->sqn[j]);
	i = hp->num_channels;
out_err_rq:
	for (j = 0; j < i; j++)
		mlx5_core_destroy_rq(hp->func_mdev, hp->rqn[j]);
	return err;
}

static void mlx5_hairpin_destroy_queues(struct mlx5_hairpin *hp)
{
	int i;

	for (i = 0; i < hp->num_channels; i++) {
		mlx5_core_destroy_rq(hp->func_mdev, hp->rqn[i]);
		mlx5_core_destroy_sq(hp->peer_mdev, hp->sqn[i]);
	}
}

static int mlx5_hairpin_modify_rq(struct mlx5_core_dev *func_mdev, u32 rqn,
				  int curr_state, int next_state,
				  u16 peer_vhca, u32 peer_sq)
{
	u32 in[MLX5_ST_SZ_DW(modify_rq_in)] = {0};
	void *rqc;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	if (next_state == MLX5_RQC_STATE_RDY) {
		MLX5_SET(rqc, rqc, hairpin_peer_sq, peer_sq);
		MLX5_SET(rqc, rqc, hairpin_peer_vhca, peer_vhca);
	}

	MLX5_SET(modify_rq_in, in, rq_state, curr_state);
	MLX5_SET(rqc, rqc, state, next_state);

	return mlx5_core_modify_rq(func_mdev, rqn,
				   in, MLX5_ST_SZ_BYTES(modify_rq_in));
}

static int mlx5_hairpin_modify_sq(struct mlx5_core_dev *peer_mdev, u32 sqn,
				  int curr_state, int next_state,
				  u16 peer_vhca, u32 peer_rq)
{
	u32 in[MLX5_ST_SZ_DW(modify_sq_in)] = {0};
	void *sqc;

	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);

	if (next_state == MLX5_RQC_STATE_RDY) {
		MLX5_SET(sqc, sqc, hairpin_peer_rq, peer_rq);
		MLX5_SET(sqc, sqc, hairpin_peer_vhca, peer_vhca);
	}

	MLX5_SET(modify_sq_in, in, sq_state, curr_state);
	MLX5_SET(sqc, sqc, state, next_state);

	return mlx5_core_modify_sq(peer_mdev, sqn,
				   in, MLX5_ST_SZ_BYTES(modify_sq_in));
}

static int mlx5_hairpin_pair_queues(struct mlx5_hairpin *hp)
{
	int i, j, err;

	/* set peer SQs */
	for (i = 0; i < hp->num_channels; i++) {
		err = mlx5_hairpin_modify_sq(hp->peer_mdev, hp->sqn[i],
					     MLX5_SQC_STATE_RST, MLX5_SQC_STATE_RDY,
					     MLX5_CAP_GEN(hp->func_mdev, vhca_id), hp->rqn[i]);
		if (err)
			goto err_modify_sq;
	}

	/* set func RQs */
	for (i = 0; i < hp->num_channels; i++) {
		err = mlx5_hairpin_modify_rq(hp->func_mdev, hp->rqn[i],
					     MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY,
					     MLX5_CAP_GEN(hp->peer_mdev, vhca_id), hp->sqn[i]);
		if (err)
			goto err_modify_rq;
	}

	return 0;

err_modify_rq:
	for (j = 0; j < i; j++)
		mlx5_hairpin_modify_rq(hp->func_mdev, hp->rqn[j], MLX5_RQC_STATE_RDY,
				       MLX5_RQC_STATE_RST, 0, 0);
	i = hp->num_channels;
err_modify_sq:
	for (j = 0; j < i; j++)
		mlx5_hairpin_modify_sq(hp->peer_mdev, hp->sqn[j], MLX5_SQC_STATE_RDY,
				       MLX5_SQC_STATE_RST, 0, 0);
	return err;
}

static void mlx5_hairpin_unpair_queues(struct mlx5_hairpin *hp)
{
	int i;

	/* unset func RQs */
	for (i = 0; i < hp->num_channels; i++)
		mlx5_hairpin_modify_rq(hp->func_mdev, hp->rqn[i], MLX5_RQC_STATE_RDY,
				       MLX5_RQC_STATE_RST, 0, 0);

	/* unset peer SQs */
	for (i = 0; i < hp->num_channels; i++)
		mlx5_hairpin_modify_sq(hp->peer_mdev, hp->sqn[i], MLX5_SQC_STATE_RDY,
				       MLX5_SQC_STATE_RST, 0, 0);
}

struct mlx5_hairpin *
mlx5_core_hairpin_create(struct mlx5_core_dev *func_mdev,
			 struct mlx5_core_dev *peer_mdev,
			 struct mlx5_hairpin_params *params)
{
	struct mlx5_hairpin *hp;
	int size, err;

	size = sizeof(*hp) + params->num_channels * 2 * sizeof(u32);
	hp = kzalloc(size, GFP_KERNEL);
	if (!hp)
		return ERR_PTR(-ENOMEM);

	hp->func_mdev = func_mdev;
	hp->peer_mdev = peer_mdev;
	hp->num_channels = params->num_channels;

	hp->rqn = (void *)hp + sizeof(*hp);
	hp->sqn = hp->rqn + params->num_channels;

	/* alloc and pair func --> peer hairpin */
	err = mlx5_hairpin_create_queues(hp, params);
	if (err)
		goto err_create_queues;

	err = mlx5_hairpin_pair_queues(hp);
	if (err)
		goto err_pair_queues;

	return hp;

err_pair_queues:
	mlx5_hairpin_destroy_queues(hp);
err_create_queues:
	kfree(hp);
	return ERR_PTR(err);
}

void mlx5_core_hairpin_destroy(struct mlx5_hairpin *hp)
{
	mlx5_hairpin_unpair_queues(hp);
	mlx5_hairpin_destroy_queues(hp);
	kfree(hp);
}
