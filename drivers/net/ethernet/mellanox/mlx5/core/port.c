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
	int err;

	memset(&in, 0, sizeof(in));
	in.caps_127_96 = cpu_to_be32(caps);
	in.port_num = port_num;

	err = mlx5_core_access_reg(dev, &in, sizeof(in), &out,
				   sizeof(out), MLX5_REG_PCAP, 0, 1);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_set_port_caps);
