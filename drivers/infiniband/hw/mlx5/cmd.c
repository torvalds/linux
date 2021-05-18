// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2017-2020, Mellanox Technologies inc. All rights reserved.
 */

#include "cmd.h"

int mlx5_cmd_dump_fill_mkey(struct mlx5_core_dev *dev, u32 *mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)] = {};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec_inout(dev, query_special_contexts, in, out);
	if (!err)
		*mkey = MLX5_GET(query_special_contexts_out, out,
				 dump_fill_mkey);
	return err;
}

int mlx5_cmd_null_mkey(struct mlx5_core_dev *dev, u32 *null_mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)] = {};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec_inout(dev, query_special_contexts, in, out);
	if (!err)
		*null_mkey = MLX5_GET(query_special_contexts_out, out,
				      null_mkey);
	return err;
}

int mlx5_cmd_query_cong_params(struct mlx5_core_dev *dev, int cong_point,
			       void *out)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_params_in)] = {};

	MLX5_SET(query_cong_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_PARAMS);
	MLX5_SET(query_cong_params_in, in, cong_protocol, cong_point);

	return mlx5_cmd_exec_inout(dev, query_cong_params, in, out);
}

void mlx5_cmd_destroy_tir(struct mlx5_core_dev *dev, u32 tirn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tir_in)] = {};

	MLX5_SET(destroy_tir_in, in, opcode, MLX5_CMD_OP_DESTROY_TIR);
	MLX5_SET(destroy_tir_in, in, tirn, tirn);
	MLX5_SET(destroy_tir_in, in, uid, uid);
	mlx5_cmd_exec_in(dev, destroy_tir, in);
}

void mlx5_cmd_destroy_tis(struct mlx5_core_dev *dev, u32 tisn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tis_in)] = {};

	MLX5_SET(destroy_tis_in, in, opcode, MLX5_CMD_OP_DESTROY_TIS);
	MLX5_SET(destroy_tis_in, in, tisn, tisn);
	MLX5_SET(destroy_tis_in, in, uid, uid);
	mlx5_cmd_exec_in(dev, destroy_tis, in);
}

int mlx5_cmd_destroy_rqt(struct mlx5_core_dev *dev, u32 rqtn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rqt_in)] = {};

	MLX5_SET(destroy_rqt_in, in, opcode, MLX5_CMD_OP_DESTROY_RQT);
	MLX5_SET(destroy_rqt_in, in, rqtn, rqtn);
	MLX5_SET(destroy_rqt_in, in, uid, uid);
	return mlx5_cmd_exec_in(dev, destroy_rqt, in);
}

int mlx5_cmd_alloc_transport_domain(struct mlx5_core_dev *dev, u32 *tdn,
				    u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(alloc_transport_domain_in)] = {};
	u32 out[MLX5_ST_SZ_DW(alloc_transport_domain_out)] = {};
	int err;

	MLX5_SET(alloc_transport_domain_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(alloc_transport_domain_in, in, uid, uid);

	err = mlx5_cmd_exec_inout(dev, alloc_transport_domain, in, out);
	if (!err)
		*tdn = MLX5_GET(alloc_transport_domain_out, out,
				transport_domain);

	return err;
}

void mlx5_cmd_dealloc_transport_domain(struct mlx5_core_dev *dev, u32 tdn,
				       u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_transport_domain_in)] = {};

	MLX5_SET(dealloc_transport_domain_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(dealloc_transport_domain_in, in, uid, uid);
	MLX5_SET(dealloc_transport_domain_in, in, transport_domain, tdn);
	mlx5_cmd_exec_in(dev, dealloc_transport_domain, in);
}

int mlx5_cmd_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_pd_in)] = {};

	MLX5_SET(dealloc_pd_in, in, opcode, MLX5_CMD_OP_DEALLOC_PD);
	MLX5_SET(dealloc_pd_in, in, pd, pdn);
	MLX5_SET(dealloc_pd_in, in, uid, uid);
	return mlx5_cmd_exec_in(dev, dealloc_pd, in);
}

int mlx5_cmd_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid,
			u32 qpn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(attach_to_mcg_in)] = {};
	void *gid;

	MLX5_SET(attach_to_mcg_in, in, opcode, MLX5_CMD_OP_ATTACH_TO_MCG);
	MLX5_SET(attach_to_mcg_in, in, qpn, qpn);
	MLX5_SET(attach_to_mcg_in, in, uid, uid);
	gid = MLX5_ADDR_OF(attach_to_mcg_in, in, multicast_gid);
	memcpy(gid, mgid, sizeof(*mgid));
	return mlx5_cmd_exec_in(dev, attach_to_mcg, in);
}

int mlx5_cmd_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid,
			u32 qpn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(detach_from_mcg_in)] = {};
	void *gid;

	MLX5_SET(detach_from_mcg_in, in, opcode, MLX5_CMD_OP_DETACH_FROM_MCG);
	MLX5_SET(detach_from_mcg_in, in, qpn, qpn);
	MLX5_SET(detach_from_mcg_in, in, uid, uid);
	gid = MLX5_ADDR_OF(detach_from_mcg_in, in, multicast_gid);
	memcpy(gid, mgid, sizeof(*mgid));
	return mlx5_cmd_exec_in(dev, detach_from_mcg, in);
}

int mlx5_cmd_xrcd_alloc(struct mlx5_core_dev *dev, u32 *xrcdn, u16 uid)
{
	u32 out[MLX5_ST_SZ_DW(alloc_xrcd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_xrcd_in)] = {};
	int err;

	MLX5_SET(alloc_xrcd_in, in, opcode, MLX5_CMD_OP_ALLOC_XRCD);
	MLX5_SET(alloc_xrcd_in, in, uid, uid);
	err = mlx5_cmd_exec_inout(dev, alloc_xrcd, in, out);
	if (!err)
		*xrcdn = MLX5_GET(alloc_xrcd_out, out, xrcd);
	return err;
}

int mlx5_cmd_xrcd_dealloc(struct mlx5_core_dev *dev, u32 xrcdn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_xrcd_in)] = {};

	MLX5_SET(dealloc_xrcd_in, in, opcode, MLX5_CMD_OP_DEALLOC_XRCD);
	MLX5_SET(dealloc_xrcd_in, in, xrcd, xrcdn);
	MLX5_SET(dealloc_xrcd_in, in, uid, uid);
	return mlx5_cmd_exec_in(dev, dealloc_xrcd, in);
}

int mlx5_cmd_mad_ifc(struct mlx5_core_dev *dev, const void *inb, void *outb,
		     u16 opmod, u8 port)
{
	int outlen = MLX5_ST_SZ_BYTES(mad_ifc_out);
	int inlen = MLX5_ST_SZ_BYTES(mad_ifc_in);
	int err = -ENOMEM;
	void *data;
	void *resp;
	u32 *out;
	u32 *in;

	in = kzalloc(inlen, GFP_KERNEL);
	out = kzalloc(outlen, GFP_KERNEL);
	if (!in || !out)
		goto out;

	MLX5_SET(mad_ifc_in, in, opcode, MLX5_CMD_OP_MAD_IFC);
	MLX5_SET(mad_ifc_in, in, op_mod, opmod);
	MLX5_SET(mad_ifc_in, in, port, port);

	data = MLX5_ADDR_OF(mad_ifc_in, in, mad);
	memcpy(data, inb, MLX5_FLD_SZ_BYTES(mad_ifc_in, mad));

	err = mlx5_cmd_exec_inout(dev, mad_ifc, in, out);
	if (err)
		goto out;

	resp = MLX5_ADDR_OF(mad_ifc_out, out, response_mad_packet);
	memcpy(outb, resp,
	       MLX5_FLD_SZ_BYTES(mad_ifc_out, response_mad_packet));

out:
	kfree(out);
	kfree(in);
	return err;
}
