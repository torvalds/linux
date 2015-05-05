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

int mlx5_cmd_query_adapter(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd_query_adapter_mbox_out *out;
	struct mlx5_cmd_query_adapter_mbox_in in;
	int err;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	memset(&in, 0, sizeof(in));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_ADAPTER);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, sizeof(*out));
	if (err)
		goto out_out;

	if (out->hdr.status) {
		err = mlx5_cmd_status_to_err(&out->hdr);
		goto out_out;
	}

	memcpy(dev->board_id, out->vsd_psid, sizeof(out->vsd_psid));

out_out:
	kfree(out);

	return err;
}

int mlx5_cmd_query_hca_cap(struct mlx5_core_dev *dev, struct mlx5_caps *caps)
{
	return mlx5_core_get_caps(dev, caps, HCA_CAP_OPMOD_GET_CUR);
}

int mlx5_query_odp_caps(struct mlx5_core_dev *dev, struct mlx5_odp_caps *caps)
{
	u8 in[MLX5_ST_SZ_BYTES(query_hca_cap_in)];
	int out_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *out;
	int err;

	if (!(dev->caps.gen.flags & MLX5_DEV_CAP_FLAG_ON_DMND_PG))
		return -ENOTSUPP;

	memset(in, 0, sizeof(in));
	out = kzalloc(out_sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;
	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, op_mod, HCA_CAP_OPMOD_GET_ODP_CUR);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, out_sz);
	if (err)
		goto out;

	err = mlx5_cmd_status_to_err_v2(out);
	if (err) {
		mlx5_core_warn(dev, "query cur hca ODP caps failed, %d\n", err);
		goto out;
	}

	memcpy(caps, MLX5_ADDR_OF(query_hca_cap_out, out, capability_struct),
	       sizeof(*caps));

	mlx5_core_dbg(dev, "on-demand paging capabilities:\nrc: %08x\nuc: %08x\nud: %08x\n",
		be32_to_cpu(caps->per_transport_caps.rc_odp_caps),
		be32_to_cpu(caps->per_transport_caps.uc_odp_caps),
		be32_to_cpu(caps->per_transport_caps.ud_odp_caps));

out:
	kfree(out);
	return err;
}
EXPORT_SYMBOL(mlx5_query_odp_caps);

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
