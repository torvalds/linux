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
#include "mlx5_core.h"

int mlx5_core_mad_ifc(struct mlx5_core_dev *dev, const void *inb, void *outb,
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

	err = mlx5_cmd_exec(dev, in, inlen, out, outlen);
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
EXPORT_SYMBOL_GPL(mlx5_core_mad_ifc);
