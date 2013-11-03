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

int mlx5_cmd_query_hca_cap(struct mlx5_core_dev *dev,
			   struct mlx5_caps *caps)
{
	struct mlx5_cmd_query_hca_cap_mbox_out *out;
	struct mlx5_cmd_query_hca_cap_mbox_in in;
	struct mlx5_query_special_ctxs_mbox_out ctx_out;
	struct mlx5_query_special_ctxs_mbox_in ctx_in;
	int err;
	u16 t16;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	memset(&in, 0, sizeof(in));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_HCA_CAP);
	in.hdr.opmod  = cpu_to_be16(0x1);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, sizeof(*out));
	if (err)
		goto out_out;

	if (out->hdr.status) {
		err = mlx5_cmd_status_to_err(&out->hdr);
		goto out_out;
	}


	caps->log_max_eq = out->hca_cap.log_max_eq & 0xf;
	caps->max_cqes = 1 << out->hca_cap.log_max_cq_sz;
	caps->max_wqes = 1 << out->hca_cap.log_max_qp_sz;
	caps->max_sq_desc_sz = be16_to_cpu(out->hca_cap.max_desc_sz_sq);
	caps->max_rq_desc_sz = be16_to_cpu(out->hca_cap.max_desc_sz_rq);
	caps->flags = be64_to_cpu(out->hca_cap.flags);
	caps->stat_rate_support = be16_to_cpu(out->hca_cap.stat_rate_support);
	caps->log_max_msg = out->hca_cap.log_max_msg & 0x1f;
	caps->num_ports = out->hca_cap.num_ports & 0xf;
	caps->log_max_cq = out->hca_cap.log_max_cq & 0x1f;
	if (caps->num_ports > MLX5_MAX_PORTS) {
		mlx5_core_err(dev, "device has %d ports while the driver supports max %d ports\n",
			      caps->num_ports, MLX5_MAX_PORTS);
		err = -EINVAL;
		goto out_out;
	}
	caps->log_max_qp = out->hca_cap.log_max_qp & 0x1f;
	caps->log_max_mkey = out->hca_cap.log_max_mkey & 0x3f;
	caps->log_max_pd = out->hca_cap.log_max_pd & 0x1f;
	caps->log_max_srq = out->hca_cap.log_max_srqs & 0x1f;
	caps->local_ca_ack_delay = out->hca_cap.local_ca_ack_delay & 0x1f;
	caps->log_max_mcg = out->hca_cap.log_max_mcg;
	caps->max_qp_mcg = be32_to_cpu(out->hca_cap.max_qp_mcg) & 0xffffff;
	caps->max_ra_res_qp = 1 << (out->hca_cap.log_max_ra_res_qp & 0x3f);
	caps->max_ra_req_qp = 1 << (out->hca_cap.log_max_ra_req_qp & 0x3f);
	caps->max_srq_wqes = 1 << out->hca_cap.log_max_srq_sz;
	t16 = be16_to_cpu(out->hca_cap.bf_log_bf_reg_size);
	if (t16 & 0x8000) {
		caps->bf_reg_size = 1 << (t16 & 0x1f);
		caps->bf_regs_per_page = MLX5_BF_REGS_PER_PAGE;
	} else {
		caps->bf_reg_size = 0;
		caps->bf_regs_per_page = 0;
	}
	caps->min_page_sz = ~(u32)((1 << out->hca_cap.log_pg_sz) - 1);

	memset(&ctx_in, 0, sizeof(ctx_in));
	memset(&ctx_out, 0, sizeof(ctx_out));
	ctx_in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, &ctx_in, sizeof(ctx_in),
				 &ctx_out, sizeof(ctx_out));
	if (err)
		goto out_out;

	if (ctx_out.hdr.status)
		err = mlx5_cmd_status_to_err(&ctx_out.hdr);

	caps->reserved_lkey = be32_to_cpu(ctx_out.reserved_lkey);

out_out:
	kfree(out);

	return err;
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
