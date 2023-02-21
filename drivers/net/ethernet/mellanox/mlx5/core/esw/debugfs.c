// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/debugfs.h>
#include "eswitch.h"

enum vnic_diag_counter {
	MLX5_VNIC_DIAG_TOTAL_Q_UNDER_PROCESSOR_HANDLE,
	MLX5_VNIC_DIAG_SEND_QUEUE_PRIORITY_UPDATE_FLOW,
	MLX5_VNIC_DIAG_COMP_EQ_OVERRUN,
	MLX5_VNIC_DIAG_ASYNC_EQ_OVERRUN,
	MLX5_VNIC_DIAG_CQ_OVERRUN,
	MLX5_VNIC_DIAG_INVALID_COMMAND,
	MLX5_VNIC_DIAG_QOUTA_EXCEEDED_COMMAND,
	MLX5_VNIC_DIAG_RX_STEERING_DISCARD,
};

static int mlx5_esw_query_vnic_diag(struct mlx5_vport *vport, enum vnic_diag_counter counter,
				    u64 *val)
{
	u32 out[MLX5_ST_SZ_DW(query_vnic_env_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_vnic_env_in)] = {};
	struct mlx5_core_dev *dev = vport->dev;
	u16 vport_num = vport->vport;
	void *vnic_diag_out;
	int err;

	MLX5_SET(query_vnic_env_in, in, opcode, MLX5_CMD_OP_QUERY_VNIC_ENV);
	MLX5_SET(query_vnic_env_in, in, vport_number, vport_num);
	if (!mlx5_esw_is_manager_vport(dev->priv.eswitch, vport_num))
		MLX5_SET(query_vnic_env_in, in, other_vport, 1);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	vnic_diag_out = MLX5_ADDR_OF(query_vnic_env_out, out, vport_env);
	switch (counter) {
	case MLX5_VNIC_DIAG_TOTAL_Q_UNDER_PROCESSOR_HANDLE:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out, total_error_queues);
		break;
	case MLX5_VNIC_DIAG_SEND_QUEUE_PRIORITY_UPDATE_FLOW:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out,
				send_queue_priority_update_flow);
		break;
	case MLX5_VNIC_DIAG_COMP_EQ_OVERRUN:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out, comp_eq_overrun);
		break;
	case MLX5_VNIC_DIAG_ASYNC_EQ_OVERRUN:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out, async_eq_overrun);
		break;
	case MLX5_VNIC_DIAG_CQ_OVERRUN:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out, cq_overrun);
		break;
	case MLX5_VNIC_DIAG_INVALID_COMMAND:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out, invalid_command);
		break;
	case MLX5_VNIC_DIAG_QOUTA_EXCEEDED_COMMAND:
		*val = MLX5_GET(vnic_diagnostic_statistics, vnic_diag_out, quota_exceeded_command);
		break;
	case MLX5_VNIC_DIAG_RX_STEERING_DISCARD:
		*val = MLX5_GET64(vnic_diagnostic_statistics, vnic_diag_out,
				  nic_receive_steering_discard);
		break;
	}

	return 0;
}

static int __show_vnic_diag(struct seq_file *file, struct mlx5_vport *vport,
			    enum vnic_diag_counter type)
{
	u64 val = 0;
	int ret;

	ret = mlx5_esw_query_vnic_diag(vport, type, &val);
	if (ret)
		return ret;

	seq_printf(file, "%llu\n", val);
	return 0;
}

static int total_q_under_processor_handle_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_TOTAL_Q_UNDER_PROCESSOR_HANDLE);
}

static int send_queue_priority_update_flow_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private,
				MLX5_VNIC_DIAG_SEND_QUEUE_PRIORITY_UPDATE_FLOW);
}

static int comp_eq_overrun_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_COMP_EQ_OVERRUN);
}

static int async_eq_overrun_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_ASYNC_EQ_OVERRUN);
}

static int cq_overrun_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_CQ_OVERRUN);
}

static int invalid_command_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_INVALID_COMMAND);
}

static int quota_exceeded_command_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_QOUTA_EXCEEDED_COMMAND);
}

static int rx_steering_discard_show(struct seq_file *file, void *priv)
{
	return __show_vnic_diag(file, file->private, MLX5_VNIC_DIAG_RX_STEERING_DISCARD);
}

DEFINE_SHOW_ATTRIBUTE(total_q_under_processor_handle);
DEFINE_SHOW_ATTRIBUTE(send_queue_priority_update_flow);
DEFINE_SHOW_ATTRIBUTE(comp_eq_overrun);
DEFINE_SHOW_ATTRIBUTE(async_eq_overrun);
DEFINE_SHOW_ATTRIBUTE(cq_overrun);
DEFINE_SHOW_ATTRIBUTE(invalid_command);
DEFINE_SHOW_ATTRIBUTE(quota_exceeded_command);
DEFINE_SHOW_ATTRIBUTE(rx_steering_discard);

void mlx5_esw_vport_debugfs_destroy(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);

	debugfs_remove_recursive(vport->dbgfs);
	vport->dbgfs = NULL;
}

/* vnic diag dir name is "pf", "ecpf" or "{vf/sf}_xxxx" */
#define VNIC_DIAG_DIR_NAME_MAX_LEN 8

void mlx5_esw_vport_debugfs_create(struct mlx5_eswitch *esw, u16 vport_num, bool is_sf, u16 sf_num)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);
	struct dentry *vnic_diag;
	char dir_name[VNIC_DIAG_DIR_NAME_MAX_LEN];
	int err;

	if (!MLX5_CAP_GEN(esw->dev, vport_group_manager))
		return;

	if (vport_num == MLX5_VPORT_PF) {
		strcpy(dir_name, "pf");
	} else if (vport_num == MLX5_VPORT_ECPF) {
		strcpy(dir_name, "ecpf");
	} else {
		err = snprintf(dir_name, VNIC_DIAG_DIR_NAME_MAX_LEN, "%s_%d", is_sf ? "sf" : "vf",
			       is_sf ? sf_num : vport_num - MLX5_VPORT_FIRST_VF);
		if (WARN_ON(err < 0))
			return;
	}

	vport->dbgfs = debugfs_create_dir(dir_name, esw->dbgfs);
	vnic_diag = debugfs_create_dir("vnic_diag", vport->dbgfs);

	if (MLX5_CAP_GEN(esw->dev, vnic_env_queue_counters)) {
		debugfs_create_file("total_q_under_processor_handle", 0444, vnic_diag, vport,
				    &total_q_under_processor_handle_fops);
		debugfs_create_file("send_queue_priority_update_flow", 0444, vnic_diag, vport,
				    &send_queue_priority_update_flow_fops);
	}

	if (MLX5_CAP_GEN(esw->dev, eq_overrun_count)) {
		debugfs_create_file("comp_eq_overrun", 0444, vnic_diag, vport,
				    &comp_eq_overrun_fops);
		debugfs_create_file("async_eq_overrun", 0444, vnic_diag, vport,
				    &async_eq_overrun_fops);
	}

	if (MLX5_CAP_GEN(esw->dev, vnic_env_cq_overrun))
		debugfs_create_file("cq_overrun", 0444, vnic_diag, vport, &cq_overrun_fops);

	if (MLX5_CAP_GEN(esw->dev, invalid_command_count))
		debugfs_create_file("invalid_command", 0444, vnic_diag, vport,
				    &invalid_command_fops);

	if (MLX5_CAP_GEN(esw->dev, quota_exceeded_count))
		debugfs_create_file("quota_exceeded_command", 0444, vnic_diag, vport,
				    &quota_exceeded_command_fops);

	if (MLX5_CAP_GEN(esw->dev, nic_receive_steering_discard))
		debugfs_create_file("rx_steering_discard", 0444, vnic_diag, vport,
				    &rx_steering_discard_fops);

}
