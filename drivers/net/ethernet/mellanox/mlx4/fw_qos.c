/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies.
 * All rights reserved.
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

#include <linux/export.h>
#include "fw_qos.h"
#include "fw.h"

enum {
	/* allocate vpp opcode modifiers */
	MLX4_ALLOCATE_VPP_ALLOCATE	= 0x0,
	MLX4_ALLOCATE_VPP_QUERY		= 0x1
};

enum {
	/* set vport qos opcode modifiers */
	MLX4_SET_VPORT_QOS_SET		= 0x0,
	MLX4_SET_VPORT_QOS_QUERY	= 0x1
};

struct mlx4_set_port_prio2tc_context {
	u8 prio2tc[4];
};

struct mlx4_port_scheduler_tc_cfg_be {
	__be16 pg;
	__be16 bw_precentage;
	__be16 max_bw_units; /* 3-100Mbps, 4-1Gbps, other values - reserved */
	__be16 max_bw_value;
};

struct mlx4_set_port_scheduler_context {
	struct mlx4_port_scheduler_tc_cfg_be tc[MLX4_NUM_TC];
};

/* Granular Qos (per VF) section */
struct mlx4_alloc_vpp_param {
	__be32 availible_vpp;
	__be32 vpp_p_up[MLX4_NUM_UP];
};

struct mlx4_prio_qos_param {
	__be32 bw_share;
	__be32 max_avg_bw;
	__be32 reserved;
	__be32 enable;
	__be32 reserved1[4];
};

struct mlx4_set_vport_context {
	__be32 reserved[8];
	struct mlx4_prio_qos_param qos_p_up[MLX4_NUM_UP];
};

int mlx4_SET_PORT_PRIO2TC(struct mlx4_dev *dev, u8 port, u8 *prio2tc)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_prio2tc_context *context;
	int err;
	u32 in_mod;
	int i;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	context = mailbox->buf;

	for (i = 0; i < MLX4_NUM_UP; i += 2)
		context->prio2tc[i >> 1] = prio2tc[i] << 4 | prio2tc[i + 1];

	in_mod = MLX4_SET_PORT_PRIO2TC << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, 1, MLX4_CMD_SET_PORT,
		       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_PRIO2TC);

int mlx4_SET_PORT_SCHEDULER(struct mlx4_dev *dev, u8 port, u8 *tc_tx_bw,
			    u8 *pg, u16 *ratelimit)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_scheduler_context *context;
	int err;
	u32 in_mod;
	int i;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	context = mailbox->buf;

	for (i = 0; i < MLX4_NUM_TC; i++) {
		struct mlx4_port_scheduler_tc_cfg_be *tc = &context->tc[i];
		u16 r;

		if (ratelimit && ratelimit[i]) {
			if (ratelimit[i] <= MLX4_MAX_100M_UNITS_VAL) {
				r = ratelimit[i];
				tc->max_bw_units =
					htons(MLX4_RATELIMIT_100M_UNITS);
			} else {
				r = ratelimit[i] / 10;
				tc->max_bw_units =
					htons(MLX4_RATELIMIT_1G_UNITS);
			}
			tc->max_bw_value = htons(r);
		} else {
			tc->max_bw_value = htons(MLX4_RATELIMIT_DEFAULT);
			tc->max_bw_units = htons(MLX4_RATELIMIT_1G_UNITS);
		}

		tc->pg = htons(pg[i]);
		tc->bw_precentage = htons(tc_tx_bw[i]);
	}

	in_mod = MLX4_SET_PORT_SCHEDULER << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, 1, MLX4_CMD_SET_PORT,
		       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_SCHEDULER);

int mlx4_ALLOCATE_VPP_get(struct mlx4_dev *dev, u8 port,
			  u16 *availible_vpp, u8 *vpp_p_up)
{
	int i;
	int err;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_alloc_vpp_param *out_param;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	out_param = mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma, port,
			   MLX4_ALLOCATE_VPP_QUERY,
			   MLX4_CMD_ALLOCATE_VPP,
			   MLX4_CMD_TIME_CLASS_A,
			   MLX4_CMD_NATIVE);
	if (err)
		goto out;

	/* Total number of supported VPPs */
	*availible_vpp = (u16)be32_to_cpu(out_param->availible_vpp);

	for (i = 0; i < MLX4_NUM_UP; i++)
		vpp_p_up[i] = (u8)be32_to_cpu(out_param->vpp_p_up[i]);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}
EXPORT_SYMBOL(mlx4_ALLOCATE_VPP_get);

int mlx4_ALLOCATE_VPP_set(struct mlx4_dev *dev, u8 port, u8 *vpp_p_up)
{
	int i;
	int err;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_alloc_vpp_param *in_param;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	in_param = mailbox->buf;

	for (i = 0; i < MLX4_NUM_UP; i++)
		in_param->vpp_p_up[i] = cpu_to_be32(vpp_p_up[i]);

	err = mlx4_cmd(dev, mailbox->dma, port,
		       MLX4_ALLOCATE_VPP_ALLOCATE,
		       MLX4_CMD_ALLOCATE_VPP,
		       MLX4_CMD_TIME_CLASS_A,
		       MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_ALLOCATE_VPP_set);

int mlx4_SET_VPORT_QOS_get(struct mlx4_dev *dev, u8 port, u8 vport,
			   struct mlx4_vport_qos_param *out_param)
{
	int i;
	int err;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_vport_context *ctx;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ctx = mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma, (vport << 8) | port,
			   MLX4_SET_VPORT_QOS_QUERY,
			   MLX4_CMD_SET_VPORT_QOS,
			   MLX4_CMD_TIME_CLASS_A,
			   MLX4_CMD_NATIVE);
	if (err)
		goto out;

	for (i = 0; i < MLX4_NUM_UP; i++) {
		out_param[i].bw_share = be32_to_cpu(ctx->qos_p_up[i].bw_share);
		out_param[i].max_avg_bw =
			be32_to_cpu(ctx->qos_p_up[i].max_avg_bw);
		out_param[i].enable =
			!!(be32_to_cpu(ctx->qos_p_up[i].enable) & 31);
	}

out:
	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}
EXPORT_SYMBOL(mlx4_SET_VPORT_QOS_get);

int mlx4_SET_VPORT_QOS_set(struct mlx4_dev *dev, u8 port, u8 vport,
			   struct mlx4_vport_qos_param *in_param)
{
	int i;
	int err;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_vport_context *ctx;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ctx = mailbox->buf;

	for (i = 0; i < MLX4_NUM_UP; i++) {
		ctx->qos_p_up[i].bw_share = cpu_to_be32(in_param[i].bw_share);
		ctx->qos_p_up[i].max_avg_bw =
				cpu_to_be32(in_param[i].max_avg_bw);
		ctx->qos_p_up[i].enable =
				cpu_to_be32(in_param[i].enable << 31);
	}

	err = mlx4_cmd(dev, mailbox->dma, (vport << 8) | port,
		       MLX4_SET_VPORT_QOS_SET,
		       MLX4_CMD_SET_VPORT_QOS,
		       MLX4_CMD_TIME_CLASS_A,
		       MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_VPORT_QOS_set);
