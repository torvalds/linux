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

int mlx5_core_mad_ifc(struct mlx5_core_dev *dev, void *inb, void *outb,
		      u16 opmod, int port)
{
	struct mlx5_mad_ifc_mbox_in *in = NULL;
	struct mlx5_mad_ifc_mbox_out *out = NULL;
	int err;

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out) {
		err = -ENOMEM;
		goto out;
	}

	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_MAD_IFC);
	in->hdr.opmod = cpu_to_be16(opmod);
	in->port = port;

	memcpy(in->data, inb, sizeof(in->data));

	err = mlx5_cmd_exec(dev, in, sizeof(*in), out, sizeof(*out));
	if (err)
		goto out;

	if (out->hdr.status) {
		err = mlx5_cmd_status_to_err(&out->hdr);
		goto out;
	}

	memcpy(outb, out->data, sizeof(out->data));

out:
	kfree(out);
	kfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_mad_ifc);
