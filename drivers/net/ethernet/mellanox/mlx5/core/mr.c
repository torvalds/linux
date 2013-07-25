/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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
#include "mlx5_core.h"

int mlx5_core_create_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			  struct mlx5_create_mkey_mbox_in *in, int inlen)
{
	struct mlx5_create_mkey_mbox_out out;
	int err;
	u8 key;

	memset(&out, 0, sizeof(out));
	spin_lock(&dev->priv.mkey_lock);
	key = dev->priv.mkey_key++;
	spin_unlock(&dev->priv.mkey_lock);
	in->seg.qpn_mkey7_0 |= cpu_to_be32(key);
	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_CREATE_MKEY);
	err = mlx5_cmd_exec(dev, in, inlen, &out, sizeof(out));
	if (err) {
		mlx5_core_dbg(dev, "cmd exec faile %d\n", err);
		return err;
	}

	if (out.hdr.status) {
		mlx5_core_dbg(dev, "status %d\n", out.hdr.status);
		return mlx5_cmd_status_to_err(&out.hdr);
	}

	mr->key = mlx5_idx_to_mkey(be32_to_cpu(out.mkey) & 0xffffff) | key;
	mlx5_core_dbg(dev, "out 0x%x, key 0x%x, mkey 0x%x\n", be32_to_cpu(out.mkey), key, mr->key);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_mkey);

int mlx5_core_destroy_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr)
{
	struct mlx5_destroy_mkey_mbox_in in;
	struct mlx5_destroy_mkey_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_MKEY);
	in.mkey = cpu_to_be32(mlx5_mkey_to_idx(mr->key));
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_destroy_mkey);

int mlx5_core_query_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			 struct mlx5_query_mkey_mbox_out *out, int outlen)
{
	struct mlx5_destroy_mkey_mbox_in in;
	int err;

	memset(&in, 0, sizeof(in));
	memset(out, 0, outlen);

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_MKEY);
	in.mkey = cpu_to_be32(mlx5_mkey_to_idx(mr->key));
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, outlen);
	if (err)
		return err;

	if (out->hdr.status)
		return mlx5_cmd_status_to_err(&out->hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_query_mkey);

int mlx5_core_dump_fill_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			     u32 *mkey)
{
	struct mlx5_query_special_ctxs_mbox_in in;
	struct mlx5_query_special_ctxs_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	*mkey = be32_to_cpu(out.dump_fill_mkey);

	return err;
}
EXPORT_SYMBOL(mlx5_core_dump_fill_mkey);
