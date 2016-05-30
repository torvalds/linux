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

#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"

int mlx5_core_access_reg(struct mlx5_core_dev *dev, void *data_in,
			 int size_in, void *data_out, int size_out,
			 u16 reg_num, int arg, int write)
{
	struct mlx5_access_reg_mbox_in *in = NULL;
	struct mlx5_access_reg_mbox_out *out = NULL;
	int err = -ENOMEM;

	in = mlx5_vzalloc(sizeof(*in) + size_in);
	if (!in)
		return -ENOMEM;

	out = mlx5_vzalloc(sizeof(*out) + size_out);
	if (!out)
		goto ex1;

	memcpy(in->data, data_in, size_in);
	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_ACCESS_REG);
	in->hdr.opmod = cpu_to_be16(!write);
	in->arg = cpu_to_be32(arg);
	in->register_id = cpu_to_be16(reg_num);
	err = mlx5_cmd_exec(dev, in, sizeof(*in) + size_in, out,
			    sizeof(*out) + size_out);
	if (err)
		goto ex2;

	if (out->hdr.status)
		err = mlx5_cmd_status_to_err(&out->hdr);

	if (!err)
		memcpy(data_out, out->data, size_out);

ex2:
	kvfree(out);
ex1:
	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_access_reg);


struct mlx5_reg_pcap {
	u8			rsvd0;
	u8			port_num;
	u8			rsvd1[2];
	__be32			caps_127_96;
	__be32			caps_95_64;
	__be32			caps_63_32;
	__be32			caps_31_0;
};

int mlx5_set_port_caps(struct mlx5_core_dev *dev, u8 port_num, u32 caps)
{
	struct mlx5_reg_pcap in;
	struct mlx5_reg_pcap out;

	memset(&in, 0, sizeof(in));
	in.caps_127_96 = cpu_to_be32(caps);
	in.port_num = port_num;

	return mlx5_core_access_reg(dev, &in, sizeof(in), &out,
				    sizeof(out), MLX5_REG_PCAP, 0, 1);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_caps);

int mlx5_query_port_ptys(struct mlx5_core_dev *dev, u32 *ptys,
			 int ptys_size, int proto_mask, u8 local_port)
{
	u32 in[MLX5_ST_SZ_DW(ptys_reg)];

	memset(in, 0, sizeof(in));
	MLX5_SET(ptys_reg, in, local_port, local_port);
	MLX5_SET(ptys_reg, in, proto_mask, proto_mask);

	return mlx5_core_access_reg(dev, in, sizeof(in), ptys,
				    ptys_size, MLX5_REG_PTYS, 0, 0);
}
EXPORT_SYMBOL_GPL(mlx5_query_port_ptys);

int mlx5_query_port_proto_cap(struct mlx5_core_dev *dev,
			      u32 *proto_cap, int proto_mask)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), proto_mask, 1);
	if (err)
		return err;

	if (proto_mask == MLX5_PTYS_EN)
		*proto_cap = MLX5_GET(ptys_reg, out, eth_proto_capability);
	else
		*proto_cap = MLX5_GET(ptys_reg, out, ib_proto_capability);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_proto_cap);

int mlx5_query_port_proto_admin(struct mlx5_core_dev *dev,
				u32 *proto_admin, int proto_mask)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), proto_mask, 1);
	if (err)
		return err;

	if (proto_mask == MLX5_PTYS_EN)
		*proto_admin = MLX5_GET(ptys_reg, out, eth_proto_admin);
	else
		*proto_admin = MLX5_GET(ptys_reg, out, ib_proto_admin);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_proto_admin);

int mlx5_query_port_link_width_oper(struct mlx5_core_dev *dev,
				    u8 *link_width_oper, u8 local_port)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), MLX5_PTYS_IB, local_port);
	if (err)
		return err;

	*link_width_oper = MLX5_GET(ptys_reg, out, ib_link_width_oper);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_link_width_oper);

int mlx5_query_port_proto_oper(struct mlx5_core_dev *dev,
			       u8 *proto_oper, int proto_mask,
			       u8 local_port)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), proto_mask, local_port);
	if (err)
		return err;

	if (proto_mask == MLX5_PTYS_EN)
		*proto_oper = MLX5_GET(ptys_reg, out, eth_proto_oper);
	else
		*proto_oper = MLX5_GET(ptys_reg, out, ib_proto_oper);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_proto_oper);

int mlx5_set_port_proto(struct mlx5_core_dev *dev, u32 proto_admin,
			int proto_mask)
{
	u32 in[MLX5_ST_SZ_DW(ptys_reg)];
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];

	memset(in, 0, sizeof(in));

	MLX5_SET(ptys_reg, in, local_port, 1);
	MLX5_SET(ptys_reg, in, proto_mask, proto_mask);
	if (proto_mask == MLX5_PTYS_EN)
		MLX5_SET(ptys_reg, in, eth_proto_admin, proto_admin);
	else
		MLX5_SET(ptys_reg, in, ib_proto_admin, proto_admin);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				    sizeof(out), MLX5_REG_PTYS, 0, 1);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_proto);

int mlx5_set_port_admin_status(struct mlx5_core_dev *dev,
			       enum mlx5_port_status status)
{
	u32 in[MLX5_ST_SZ_DW(paos_reg)];
	u32 out[MLX5_ST_SZ_DW(paos_reg)];

	memset(in, 0, sizeof(in));

	MLX5_SET(paos_reg, in, local_port, 1);
	MLX5_SET(paos_reg, in, admin_status, status);
	MLX5_SET(paos_reg, in, ase, 1);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				    sizeof(out), MLX5_REG_PAOS, 0, 1);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_admin_status);

int mlx5_query_port_admin_status(struct mlx5_core_dev *dev,
				 enum mlx5_port_status *status)
{
	u32 in[MLX5_ST_SZ_DW(paos_reg)];
	u32 out[MLX5_ST_SZ_DW(paos_reg)];
	int err;

	memset(in, 0, sizeof(in));

	MLX5_SET(paos_reg, in, local_port, 1);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PAOS, 0, 0);
	if (err)
		return err;

	*status = MLX5_GET(paos_reg, out, admin_status);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_admin_status);

static void mlx5_query_port_mtu(struct mlx5_core_dev *dev, u16 *admin_mtu,
				u16 *max_mtu, u16 *oper_mtu, u8 port)
{
	u32 in[MLX5_ST_SZ_DW(pmtu_reg)];
	u32 out[MLX5_ST_SZ_DW(pmtu_reg)];

	memset(in, 0, sizeof(in));

	MLX5_SET(pmtu_reg, in, local_port, port);

	mlx5_core_access_reg(dev, in, sizeof(in), out,
			     sizeof(out), MLX5_REG_PMTU, 0, 0);

	if (max_mtu)
		*max_mtu  = MLX5_GET(pmtu_reg, out, max_mtu);
	if (oper_mtu)
		*oper_mtu = MLX5_GET(pmtu_reg, out, oper_mtu);
	if (admin_mtu)
		*admin_mtu = MLX5_GET(pmtu_reg, out, admin_mtu);
}

int mlx5_set_port_mtu(struct mlx5_core_dev *dev, u16 mtu, u8 port)
{
	u32 in[MLX5_ST_SZ_DW(pmtu_reg)];
	u32 out[MLX5_ST_SZ_DW(pmtu_reg)];

	memset(in, 0, sizeof(in));

	MLX5_SET(pmtu_reg, in, admin_mtu, mtu);
	MLX5_SET(pmtu_reg, in, local_port, port);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PMTU, 0, 1);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_mtu);

void mlx5_query_port_max_mtu(struct mlx5_core_dev *dev, u16 *max_mtu,
			     u8 port)
{
	mlx5_query_port_mtu(dev, NULL, max_mtu, NULL, port);
}
EXPORT_SYMBOL_GPL(mlx5_query_port_max_mtu);

void mlx5_query_port_oper_mtu(struct mlx5_core_dev *dev, u16 *oper_mtu,
			      u8 port)
{
	mlx5_query_port_mtu(dev, NULL, NULL, oper_mtu, port);
}
EXPORT_SYMBOL_GPL(mlx5_query_port_oper_mtu);

static int mlx5_query_port_pvlc(struct mlx5_core_dev *dev, u32 *pvlc,
				int pvlc_size,  u8 local_port)
{
	u32 in[MLX5_ST_SZ_DW(pvlc_reg)];

	memset(in, 0, sizeof(in));
	MLX5_SET(pvlc_reg, in, local_port, local_port);

	return mlx5_core_access_reg(dev, in, sizeof(in), pvlc,
				    pvlc_size, MLX5_REG_PVLC, 0, 0);
}

int mlx5_query_port_vl_hw_cap(struct mlx5_core_dev *dev,
			      u8 *vl_hw_cap, u8 local_port)
{
	u32 out[MLX5_ST_SZ_DW(pvlc_reg)];
	int err;

	err = mlx5_query_port_pvlc(dev, out, sizeof(out), local_port);
	if (err)
		return err;

	*vl_hw_cap = MLX5_GET(pvlc_reg, out, vl_hw_cap);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_vl_hw_cap);

int mlx5_set_port_pause(struct mlx5_core_dev *dev, u32 rx_pause, u32 tx_pause)
{
	u32 in[MLX5_ST_SZ_DW(pfcc_reg)];
	u32 out[MLX5_ST_SZ_DW(pfcc_reg)];

	memset(in, 0, sizeof(in));
	MLX5_SET(pfcc_reg, in, local_port, 1);
	MLX5_SET(pfcc_reg, in, pptx, tx_pause);
	MLX5_SET(pfcc_reg, in, pprx, rx_pause);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				    sizeof(out), MLX5_REG_PFCC, 0, 1);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_pause);

int mlx5_query_port_pause(struct mlx5_core_dev *dev,
			  u32 *rx_pause, u32 *tx_pause)
{
	u32 in[MLX5_ST_SZ_DW(pfcc_reg)];
	u32 out[MLX5_ST_SZ_DW(pfcc_reg)];
	int err;

	memset(in, 0, sizeof(in));
	MLX5_SET(pfcc_reg, in, local_port, 1);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PFCC, 0, 0);
	if (err)
		return err;

	if (rx_pause)
		*rx_pause = MLX5_GET(pfcc_reg, out, pprx);

	if (tx_pause)
		*tx_pause = MLX5_GET(pfcc_reg, out, pptx);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_pause);
