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

#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <linux/module.h>
#include "mlx5_core.h"

static int mlx5_cmd_query_adapter(struct mlx5_core_dev *dev, u32 *out,
				  int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_adapter_in)];

	memset(in, 0, sizeof(in));

	MLX5_SET(query_adapter_in, in, opcode, MLX5_CMD_OP_QUERY_ADAPTER);

	return mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, outlen);
}

int mlx5_query_board_id(struct mlx5_core_dev *dev)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_cmd_query_adapter(dev, out, outlen);
	if (err)
		goto out;

	memcpy(dev->board_id,
	       MLX5_ADDR_OF(query_adapter_out, out,
			    query_adapter_struct.vsd_contd_psid),
	       MLX5_FLD_SZ_BYTES(query_adapter_out,
				 query_adapter_struct.vsd_contd_psid));

out:
	kfree(out);
	return err;
}

int mlx5_core_query_vendor_id(struct mlx5_core_dev *mdev, u32 *vendor_id)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_cmd_query_adapter(mdev, out, outlen);
	if (err)
		goto out;

	*vendor_id = MLX5_GET(query_adapter_out, out,
			      query_adapter_struct.ieee_vendor_id);
out:
	kfree(out);
	return err;
}
EXPORT_SYMBOL(mlx5_core_query_vendor_id);

int mlx5_query_hca_caps(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_core_get_caps(dev, MLX5_CAP_GENERAL, HCA_CAP_OPMOD_GET_CUR);
	if (err)
		return err;

	err = mlx5_core_get_caps(dev, MLX5_CAP_GENERAL, HCA_CAP_OPMOD_GET_MAX);
	if (err)
		return err;

	if (MLX5_CAP_GEN(dev, eth_net_offloads)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ETHERNET_OFFLOADS,
					 HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
		err = mlx5_core_get_caps(dev, MLX5_CAP_ETHERNET_OFFLOADS,
					 HCA_CAP_OPMOD_GET_MAX);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, pg)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ODP,
					 HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
		err = mlx5_core_get_caps(dev, MLX5_CAP_ODP,
					 HCA_CAP_OPMOD_GET_MAX);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, atomic)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ATOMIC,
					 HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
		err = mlx5_core_get_caps(dev, MLX5_CAP_ATOMIC,
					 HCA_CAP_OPMOD_GET_MAX);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, roce)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ROCE,
					 HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
		err = mlx5_core_get_caps(dev, MLX5_CAP_ROCE,
					 HCA_CAP_OPMOD_GET_MAX);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, nic_flow_table)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_FLOW_TABLE,
					 HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
		err = mlx5_core_get_caps(dev, MLX5_CAP_FLOW_TABLE,
					 HCA_CAP_OPMOD_GET_MAX);
		if (err)
			return err;
	}
	return 0;
}

int mlx5_cmd_init_hca(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd_init_hca_mbox_in in;
	struct mlx5_cmd_init_hca_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_INIT_HCA);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		err = mlx5_cmd_status_to_err(&out.hdr);

	return err;
}

int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd_teardown_hca_mbox_in in;
	struct mlx5_cmd_teardown_hca_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_TEARDOWN_HCA);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		err = mlx5_cmd_status_to_err(&out.hdr);

	return err;
}
