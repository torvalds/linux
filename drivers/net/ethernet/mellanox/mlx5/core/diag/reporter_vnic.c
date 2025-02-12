// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. */

#include "reporter_vnic.h"
#include "en_stats.h"
#include "devlink.h"

#define VNIC_ENV_GET64(vnic_env_stats, c) \
	MLX5_GET64(query_vnic_env_out, (vnic_env_stats)->query_vnic_env_out, \
		 vport_env.c)

struct mlx5_vnic_diag_stats {
	__be64 query_vnic_env_out[MLX5_ST_SZ_QW(query_vnic_env_out)];
};

static void mlx5_reporter_vnic_diagnose_counter_icm(struct mlx5_core_dev *dev,
						    struct devlink_fmsg *fmsg,
						    u16 vport_num, bool other_vport)
{
	u32 out_icm_reg[MLX5_ST_SZ_DW(vhca_icm_ctrl_reg)] = {};
	u32 in_icm_reg[MLX5_ST_SZ_DW(vhca_icm_ctrl_reg)] = {};
	u32 out_reg[MLX5_ST_SZ_DW(nic_cap_reg)] = {};
	u32 in_reg[MLX5_ST_SZ_DW(nic_cap_reg)] = {};
	u32 cur_alloc_icm;
	int vhca_icm_ctrl;
	u16 vhca_id;
	int err;

	err = mlx5_core_access_reg(dev, in_reg, sizeof(in_reg), out_reg,
				   sizeof(out_reg), MLX5_REG_NIC_CAP, 0, 0);
	if (err) {
		mlx5_core_warn(dev, "Reading nic_cap_reg failed. err = %d\n", err);
		return;
	}
	vhca_icm_ctrl = MLX5_GET(nic_cap_reg, out_reg, vhca_icm_ctrl);
	if (!vhca_icm_ctrl)
		return;

	MLX5_SET(vhca_icm_ctrl_reg, in_icm_reg, vhca_id_valid, other_vport);
	if (other_vport) {
		err = mlx5_vport_get_vhca_id(dev, vport_num, &vhca_id);
		if (err) {
			mlx5_core_warn(dev, "vport to vhca_id failed. vport_num = %d, err = %d\n",
				       vport_num, err);
			return;
		}
		MLX5_SET(vhca_icm_ctrl_reg, in_icm_reg, vhca_id, vhca_id);
	}
	err = mlx5_core_access_reg(dev, in_icm_reg, sizeof(in_icm_reg),
				   out_icm_reg, sizeof(out_icm_reg),
				   MLX5_REG_VHCA_ICM_CTRL, 0, 0);
	if (err) {
		mlx5_core_warn(dev, "Reading vhca_icm_ctrl failed. err = %d\n", err);
		return;
	}
	cur_alloc_icm = MLX5_GET(vhca_icm_ctrl_reg, out_icm_reg, cur_alloc_icm);
	devlink_fmsg_u32_pair_put(fmsg, "icm_consumption", cur_alloc_icm);
}

void mlx5_reporter_vnic_diagnose_counters(struct mlx5_core_dev *dev,
					  struct devlink_fmsg *fmsg,
					  u16 vport_num, bool other_vport)
{
	u32 in[MLX5_ST_SZ_DW(query_vnic_env_in)] = {};
	struct mlx5_vnic_diag_stats vnic;

	MLX5_SET(query_vnic_env_in, in, opcode, MLX5_CMD_OP_QUERY_VNIC_ENV);
	MLX5_SET(query_vnic_env_in, in, vport_number, vport_num);
	MLX5_SET(query_vnic_env_in, in, other_vport, !!other_vport);

	mlx5_cmd_exec_inout(dev, query_vnic_env, in, &vnic.query_vnic_env_out);

	devlink_fmsg_pair_nest_start(fmsg, "vNIC env counters");
	devlink_fmsg_obj_nest_start(fmsg);

	if (MLX5_CAP_GEN(dev, vnic_env_queue_counters)) {
		devlink_fmsg_u32_pair_put(fmsg, "total_error_queues",
					  VNIC_ENV_GET(&vnic, total_error_queues));
		devlink_fmsg_u32_pair_put(fmsg, "send_queue_priority_update_flow",
					  VNIC_ENV_GET(&vnic, send_queue_priority_update_flow));
	}
	if (MLX5_CAP_GEN(dev, eq_overrun_count)) {
		devlink_fmsg_u32_pair_put(fmsg, "comp_eq_overrun",
					  VNIC_ENV_GET(&vnic, comp_eq_overrun));
		devlink_fmsg_u32_pair_put(fmsg, "async_eq_overrun",
					  VNIC_ENV_GET(&vnic, async_eq_overrun));
	}
	if (MLX5_CAP_GEN(dev, vnic_env_cq_overrun))
		devlink_fmsg_u32_pair_put(fmsg, "cq_overrun",
					  VNIC_ENV_GET(&vnic, cq_overrun));
	if (MLX5_CAP_GEN(dev, invalid_command_count))
		devlink_fmsg_u32_pair_put(fmsg, "invalid_command",
					  VNIC_ENV_GET(&vnic, invalid_command));
	if (MLX5_CAP_GEN(dev, quota_exceeded_count))
		devlink_fmsg_u32_pair_put(fmsg, "quota_exceeded_command",
					  VNIC_ENV_GET(&vnic, quota_exceeded_command));
	if (MLX5_CAP_GEN(dev, nic_receive_steering_discard))
		devlink_fmsg_u64_pair_put(fmsg, "nic_receive_steering_discard",
					  VNIC_ENV_GET64(&vnic, nic_receive_steering_discard));
	if (MLX5_CAP_GEN(dev, vnic_env_cnt_steering_fail)) {
		devlink_fmsg_u64_pair_put(fmsg, "generated_pkt_steering_fail",
					  VNIC_ENV_GET64(&vnic, generated_pkt_steering_fail));
		devlink_fmsg_u64_pair_put(fmsg, "handled_pkt_steering_fail",
					  VNIC_ENV_GET64(&vnic, handled_pkt_steering_fail));
	}
	if (MLX5_CAP_GEN(dev, nic_cap_reg))
		mlx5_reporter_vnic_diagnose_counter_icm(dev, fmsg, vport_num, other_vport);

	devlink_fmsg_obj_nest_end(fmsg);
	devlink_fmsg_pair_nest_end(fmsg);
}

static int mlx5_reporter_vnic_diagnose(struct devlink_health_reporter *reporter,
				       struct devlink_fmsg *fmsg,
				       struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);

	mlx5_reporter_vnic_diagnose_counters(dev, fmsg, 0, false);
	return 0;
}

static const struct devlink_health_reporter_ops mlx5_reporter_vnic_ops = {
	.name = "vnic",
	.diagnose = mlx5_reporter_vnic_diagnose,
};

void mlx5_reporter_vnic_create(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct devlink *devlink = priv_to_devlink(dev);

	health->vnic_reporter =
		devlink_health_reporter_create(devlink,
					       &mlx5_reporter_vnic_ops,
					       0, dev);
	if (IS_ERR(health->vnic_reporter))
		mlx5_core_warn(dev,
			       "Failed to create vnic reporter, err = %ld\n",
			       PTR_ERR(health->vnic_reporter));
}

void mlx5_reporter_vnic_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	if (!IS_ERR_OR_NULL(health->vnic_reporter))
		devlink_health_reporter_destroy(health->vnic_reporter);
}
