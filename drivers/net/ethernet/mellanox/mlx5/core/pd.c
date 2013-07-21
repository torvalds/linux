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

struct mlx5_alloc_pd_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_alloc_pd_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be32			pdn;
	u8			rsvd[4];
};

struct mlx5_dealloc_pd_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			pdn;
	u8			rsvd[4];
};

struct mlx5_dealloc_pd_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

int mlx5_core_alloc_pd(struct mlx5_core_dev *dev, u32 *pdn)
{
	struct mlx5_alloc_pd_mbox_in	in;
	struct mlx5_alloc_pd_mbox_out	out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_ALLOC_PD);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	*pdn = be32_to_cpu(out.pdn) & 0xffffff;
	return err;
}
EXPORT_SYMBOL(mlx5_core_alloc_pd);

int mlx5_core_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn)
{
	struct mlx5_dealloc_pd_mbox_in	in;
	struct mlx5_dealloc_pd_mbox_out	out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DEALLOC_PD);
	in.pdn = cpu_to_be32(pdn);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_dealloc_pd);
