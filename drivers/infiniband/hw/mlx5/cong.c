/*
 * Copyright (c) 2013-2017, Mellanox Technologies. All rights reserved.
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

#include <linux/debugfs.h>

#include "mlx5_ib.h"
#include "cmd.h"

enum mlx5_ib_cong_node_type {
	MLX5_IB_RROCE_ECN_RP = 1,
	MLX5_IB_RROCE_ECN_NP = 2,
};

static const char * const mlx5_ib_dbg_cc_name[] = {
	"rp_clamp_tgt_rate",
	"rp_clamp_tgt_rate_ati",
	"rp_time_reset",
	"rp_byte_reset",
	"rp_threshold",
	"rp_ai_rate",
	"rp_hai_rate",
	"rp_min_dec_fac",
	"rp_min_rate",
	"rp_rate_to_set_on_first_cnp",
	"rp_dce_tcp_g",
	"rp_dce_tcp_rtt",
	"rp_rate_reduce_monitor_period",
	"rp_initial_alpha_value",
	"rp_gd",
	"np_cnp_dscp",
	"np_cnp_prio_mode",
	"np_cnp_prio",
};

#define MLX5_IB_RP_CLAMP_TGT_RATE_ATTR			BIT(1)
#define MLX5_IB_RP_CLAMP_TGT_RATE_ATI_ATTR		BIT(2)
#define MLX5_IB_RP_TIME_RESET_ATTR			BIT(3)
#define MLX5_IB_RP_BYTE_RESET_ATTR			BIT(4)
#define MLX5_IB_RP_THRESHOLD_ATTR			BIT(5)
#define MLX5_IB_RP_AI_RATE_ATTR				BIT(7)
#define MLX5_IB_RP_HAI_RATE_ATTR			BIT(8)
#define MLX5_IB_RP_MIN_DEC_FAC_ATTR			BIT(9)
#define MLX5_IB_RP_MIN_RATE_ATTR			BIT(10)
#define MLX5_IB_RP_RATE_TO_SET_ON_FIRST_CNP_ATTR	BIT(11)
#define MLX5_IB_RP_DCE_TCP_G_ATTR			BIT(12)
#define MLX5_IB_RP_DCE_TCP_RTT_ATTR			BIT(13)
#define MLX5_IB_RP_RATE_REDUCE_MONITOR_PERIOD_ATTR	BIT(14)
#define MLX5_IB_RP_INITIAL_ALPHA_VALUE_ATTR		BIT(15)
#define MLX5_IB_RP_GD_ATTR				BIT(16)

#define MLX5_IB_NP_CNP_DSCP_ATTR			BIT(3)
#define MLX5_IB_NP_CNP_PRIO_MODE_ATTR			BIT(4)

static enum mlx5_ib_cong_node_type
mlx5_ib_param_to_node(enum mlx5_ib_dbg_cc_types param_offset)
{
	if (param_offset >= MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE &&
	    param_offset <= MLX5_IB_DBG_CC_RP_GD)
		return MLX5_IB_RROCE_ECN_RP;
	else
		return MLX5_IB_RROCE_ECN_NP;
}

static u32 mlx5_get_cc_param_val(void *field, int offset)
{
	switch (offset) {
	case MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				clamp_tgt_rate);
	case MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE_ATI:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				clamp_tgt_rate_after_time_inc);
	case MLX5_IB_DBG_CC_RP_TIME_RESET:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_time_reset);
	case MLX5_IB_DBG_CC_RP_BYTE_RESET:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_byte_reset);
	case MLX5_IB_DBG_CC_RP_THRESHOLD:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_threshold);
	case MLX5_IB_DBG_CC_RP_AI_RATE:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_ai_rate);
	case MLX5_IB_DBG_CC_RP_HAI_RATE:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_hai_rate);
	case MLX5_IB_DBG_CC_RP_MIN_DEC_FAC:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_min_dec_fac);
	case MLX5_IB_DBG_CC_RP_MIN_RATE:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_min_rate);
	case MLX5_IB_DBG_CC_RP_RATE_TO_SET_ON_FIRST_CNP:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rate_to_set_on_first_cnp);
	case MLX5_IB_DBG_CC_RP_DCE_TCP_G:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				dce_tcp_g);
	case MLX5_IB_DBG_CC_RP_DCE_TCP_RTT:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				dce_tcp_rtt);
	case MLX5_IB_DBG_CC_RP_RATE_REDUCE_MONITOR_PERIOD:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rate_reduce_monitor_period);
	case MLX5_IB_DBG_CC_RP_INITIAL_ALPHA_VALUE:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				initial_alpha_value);
	case MLX5_IB_DBG_CC_RP_GD:
		return MLX5_GET(cong_control_r_roce_ecn_rp, field,
				rpg_gd);
	case MLX5_IB_DBG_CC_NP_CNP_DSCP:
		return MLX5_GET(cong_control_r_roce_ecn_np, field,
				cnp_dscp);
	case MLX5_IB_DBG_CC_NP_CNP_PRIO_MODE:
		return MLX5_GET(cong_control_r_roce_ecn_np, field,
				cnp_prio_mode);
	case MLX5_IB_DBG_CC_NP_CNP_PRIO:
		return MLX5_GET(cong_control_r_roce_ecn_np, field,
				cnp_802p_prio);
	default:
		return 0;
	}
}

static void mlx5_ib_set_cc_param_mask_val(void *field, int offset,
					  u32 var, u32 *attr_mask)
{
	switch (offset) {
	case MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE:
		*attr_mask |= MLX5_IB_RP_CLAMP_TGT_RATE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 clamp_tgt_rate, var);
		break;
	case MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE_ATI:
		*attr_mask |= MLX5_IB_RP_CLAMP_TGT_RATE_ATI_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 clamp_tgt_rate_after_time_inc, var);
		break;
	case MLX5_IB_DBG_CC_RP_TIME_RESET:
		*attr_mask |= MLX5_IB_RP_TIME_RESET_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_time_reset, var);
		break;
	case MLX5_IB_DBG_CC_RP_BYTE_RESET:
		*attr_mask |= MLX5_IB_RP_BYTE_RESET_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_byte_reset, var);
		break;
	case MLX5_IB_DBG_CC_RP_THRESHOLD:
		*attr_mask |= MLX5_IB_RP_THRESHOLD_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_threshold, var);
		break;
	case MLX5_IB_DBG_CC_RP_AI_RATE:
		*attr_mask |= MLX5_IB_RP_AI_RATE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_ai_rate, var);
		break;
	case MLX5_IB_DBG_CC_RP_HAI_RATE:
		*attr_mask |= MLX5_IB_RP_HAI_RATE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_hai_rate, var);
		break;
	case MLX5_IB_DBG_CC_RP_MIN_DEC_FAC:
		*attr_mask |= MLX5_IB_RP_MIN_DEC_FAC_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_min_dec_fac, var);
		break;
	case MLX5_IB_DBG_CC_RP_MIN_RATE:
		*attr_mask |= MLX5_IB_RP_MIN_RATE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_min_rate, var);
		break;
	case MLX5_IB_DBG_CC_RP_RATE_TO_SET_ON_FIRST_CNP:
		*attr_mask |= MLX5_IB_RP_RATE_TO_SET_ON_FIRST_CNP_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rate_to_set_on_first_cnp, var);
		break;
	case MLX5_IB_DBG_CC_RP_DCE_TCP_G:
		*attr_mask |= MLX5_IB_RP_DCE_TCP_G_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 dce_tcp_g, var);
		break;
	case MLX5_IB_DBG_CC_RP_DCE_TCP_RTT:
		*attr_mask |= MLX5_IB_RP_DCE_TCP_RTT_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 dce_tcp_rtt, var);
		break;
	case MLX5_IB_DBG_CC_RP_RATE_REDUCE_MONITOR_PERIOD:
		*attr_mask |= MLX5_IB_RP_RATE_REDUCE_MONITOR_PERIOD_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rate_reduce_monitor_period, var);
		break;
	case MLX5_IB_DBG_CC_RP_INITIAL_ALPHA_VALUE:
		*attr_mask |= MLX5_IB_RP_INITIAL_ALPHA_VALUE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 initial_alpha_value, var);
		break;
	case MLX5_IB_DBG_CC_RP_GD:
		*attr_mask |= MLX5_IB_RP_GD_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_rp, field,
			 rpg_gd, var);
		break;
	case MLX5_IB_DBG_CC_NP_CNP_DSCP:
		*attr_mask |= MLX5_IB_NP_CNP_DSCP_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_np, field, cnp_dscp, var);
		break;
	case MLX5_IB_DBG_CC_NP_CNP_PRIO_MODE:
		*attr_mask |= MLX5_IB_NP_CNP_PRIO_MODE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_np, field, cnp_prio_mode, var);
		break;
	case MLX5_IB_DBG_CC_NP_CNP_PRIO:
		*attr_mask |= MLX5_IB_NP_CNP_PRIO_MODE_ATTR;
		MLX5_SET(cong_control_r_roce_ecn_np, field, cnp_prio_mode, 0);
		MLX5_SET(cong_control_r_roce_ecn_np, field, cnp_802p_prio, var);
		break;
	}
}

static int mlx5_ib_get_cc_params(struct mlx5_ib_dev *dev, u8 port_num,
				 int offset, u32 *var)
{
	int outlen = MLX5_ST_SZ_BYTES(query_cong_params_out);
	void *out;
	void *field;
	int err;
	enum mlx5_ib_cong_node_type node;
	struct mlx5_core_dev *mdev;

	/* Takes a 1-based port number */
	mdev = mlx5_ib_get_native_port_mdev(dev, port_num + 1, NULL);
	if (!mdev)
		return -ENODEV;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out) {
		err = -ENOMEM;
		goto alloc_err;
	}

	node = mlx5_ib_param_to_node(offset);

	err = mlx5_cmd_query_cong_params(mdev, node, out, outlen);
	if (err)
		goto free;

	field = MLX5_ADDR_OF(query_cong_params_out, out, congestion_parameters);
	*var = mlx5_get_cc_param_val(field, offset);

free:
	kvfree(out);
alloc_err:
	mlx5_ib_put_native_port_mdev(dev, port_num + 1);
	return err;
}

static int mlx5_ib_set_cc_params(struct mlx5_ib_dev *dev, u8 port_num,
				 int offset, u32 var)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_cong_params_in);
	void *in;
	void *field;
	enum mlx5_ib_cong_node_type node;
	struct mlx5_core_dev *mdev;
	u32 attr_mask = 0;
	int err;

	/* Takes a 1-based port number */
	mdev = mlx5_ib_get_native_port_mdev(dev, port_num + 1, NULL);
	if (!mdev)
		return -ENODEV;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto alloc_err;
	}

	MLX5_SET(modify_cong_params_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_CONG_PARAMS);

	node = mlx5_ib_param_to_node(offset);
	MLX5_SET(modify_cong_params_in, in, cong_protocol, node);

	field = MLX5_ADDR_OF(modify_cong_params_in, in, congestion_parameters);
	mlx5_ib_set_cc_param_mask_val(field, offset, var, &attr_mask);

	field = MLX5_ADDR_OF(modify_cong_params_in, in, field_select);
	MLX5_SET(field_select_r_roce_rp, field, field_select_r_roce_rp,
		 attr_mask);

	err = mlx5_cmd_modify_cong_params(mdev, in, inlen);
	kvfree(in);
alloc_err:
	mlx5_ib_put_native_port_mdev(dev, port_num + 1);
	return err;
}

static ssize_t set_param(struct file *filp, const char __user *buf,
			 size_t count, loff_t *pos)
{
	struct mlx5_ib_dbg_param *param = filp->private_data;
	int offset = param->offset;
	char lbuf[11] = { };
	u32 var;
	int ret;

	if (count > sizeof(lbuf))
		return -EINVAL;

	if (copy_from_user(lbuf, buf, count))
		return -EFAULT;

	lbuf[sizeof(lbuf) - 1] = '\0';

	if (kstrtou32(lbuf, 0, &var))
		return -EINVAL;

	ret = mlx5_ib_set_cc_params(param->dev, param->port_num, offset, var);
	return ret ? ret : count;
}

static ssize_t get_param(struct file *filp, char __user *buf, size_t count,
			 loff_t *pos)
{
	struct mlx5_ib_dbg_param *param = filp->private_data;
	int offset = param->offset;
	u32 var = 0;
	int ret;
	char lbuf[11];

	ret = mlx5_ib_get_cc_params(param->dev, param->port_num, offset, &var);
	if (ret)
		return ret;

	ret = snprintf(lbuf, sizeof(lbuf), "%d\n", var);
	if (ret < 0)
		return ret;

	return simple_read_from_buffer(buf, count, pos, lbuf, ret);
}

static const struct file_operations dbg_cc_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= set_param,
	.read	= get_param,
};

void mlx5_ib_cleanup_cong_debugfs(struct mlx5_ib_dev *dev, u8 port_num)
{
	if (!mlx5_debugfs_root ||
	    !dev->port[port_num].dbg_cc_params ||
	    !dev->port[port_num].dbg_cc_params->root)
		return;

	debugfs_remove_recursive(dev->port[port_num].dbg_cc_params->root);
	kfree(dev->port[port_num].dbg_cc_params);
	dev->port[port_num].dbg_cc_params = NULL;
}

int mlx5_ib_init_cong_debugfs(struct mlx5_ib_dev *dev, u8 port_num)
{
	struct mlx5_ib_dbg_cc_params *dbg_cc_params;
	struct mlx5_core_dev *mdev;
	int i;

	if (!mlx5_debugfs_root)
		goto out;

	/* Takes a 1-based port number */
	mdev = mlx5_ib_get_native_port_mdev(dev, port_num + 1, NULL);
	if (!mdev)
		goto out;

	if (!MLX5_CAP_GEN(mdev, cc_query_allowed) ||
	    !MLX5_CAP_GEN(mdev, cc_modify_allowed))
		goto put_mdev;

	dbg_cc_params = kzalloc(sizeof(*dbg_cc_params), GFP_KERNEL);
	if (!dbg_cc_params)
		goto err;

	dev->port[port_num].dbg_cc_params = dbg_cc_params;

	dbg_cc_params->root = debugfs_create_dir("cc_params",
						 mdev->priv.dbg_root);
	if (!dbg_cc_params->root)
		goto err;

	for (i = 0; i < MLX5_IB_DBG_CC_MAX; i++) {
		dbg_cc_params->params[i].offset = i;
		dbg_cc_params->params[i].dev = dev;
		dbg_cc_params->params[i].port_num = port_num;
		dbg_cc_params->params[i].dentry =
			debugfs_create_file(mlx5_ib_dbg_cc_name[i],
					    0600, dbg_cc_params->root,
					    &dbg_cc_params->params[i],
					    &dbg_cc_fops);
		if (!dbg_cc_params->params[i].dentry)
			goto err;
	}

put_mdev:
	mlx5_ib_put_native_port_mdev(dev, port_num + 1);
out:
	return 0;

err:
	mlx5_ib_warn(dev, "cong debugfs failure\n");
	mlx5_ib_cleanup_cong_debugfs(dev, port_num);
	mlx5_ib_put_native_port_mdev(dev, port_num + 1);

	/*
	 * We don't want to fail driver if debugfs failed to initialize,
	 * so we are not forwarding error to the user.
	 */
	return 0;
}
