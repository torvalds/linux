/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. */

#include "en.h"
#include "monitor_stats.h"
#include "lib/eq.h"

/* Driver will set the following watch counters list:
 * Ppcnt.802_3:
 * a_in_range_length_errors      Type: 0x0, Counter:  0x0, group_id = N/A
 * a_out_of_range_length_field   Type: 0x0, Counter:  0x1, group_id = N/A
 * a_frame_too_long_errors       Type: 0x0, Counter:  0x2, group_id = N/A
 * a_frame_check_sequence_errors Type: 0x0, Counter:  0x3, group_id = N/A
 * a_alignment_errors            Type: 0x0, Counter:  0x4, group_id = N/A
 * if_out_discards               Type: 0x0, Counter:  0x5, group_id = N/A
 * Q_Counters:
 * Q[index].rx_out_of_buffer   Type: 0x1, Counter:  0x4, group_id = counter_ix
 */

#define NUM_REQ_PPCNT_COUNTER_S1 MLX5_CMD_SET_MONITOR_NUM_PPCNT_COUNTER_SET1
#define NUM_REQ_Q_COUNTERS_S1    MLX5_CMD_SET_MONITOR_NUM_Q_COUNTERS_SET1

int mlx5e_monitor_counter_supported(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!MLX5_CAP_GEN(mdev, max_num_of_monitor_counters))
		return false;
	if (MLX5_CAP_PCAM_REG(mdev, ppcnt) &&
	    MLX5_CAP_GEN(mdev, num_ppcnt_monitor_counters) <
	    NUM_REQ_PPCNT_COUNTER_S1)
		return false;
	if (MLX5_CAP_GEN(mdev, num_q_monitor_counters) <
	    NUM_REQ_Q_COUNTERS_S1)
		return false;
	return true;
}

void mlx5e_monitor_counter_arm(struct mlx5e_priv *priv)
{
	u32  in[MLX5_ST_SZ_DW(arm_monitor_counter_in)]  = {};
	u32 out[MLX5_ST_SZ_DW(arm_monitor_counter_out)] = {};

	MLX5_SET(arm_monitor_counter_in, in, opcode,
		 MLX5_CMD_OP_ARM_MONITOR_COUNTER);
	mlx5_cmd_exec(priv->mdev, in, sizeof(in), out, sizeof(out));
}

static void mlx5e_monitor_counters_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       monitor_counters_work);

	mutex_lock(&priv->state_lock);
	mlx5e_update_ndo_stats(priv);
	mutex_unlock(&priv->state_lock);
	mlx5e_monitor_counter_arm(priv);
}

static int mlx5e_monitor_event_handler(struct notifier_block *nb,
				       unsigned long event, void *eqe)
{
	struct mlx5e_priv *priv = mlx5_nb_cof(nb, struct mlx5e_priv,
					      monitor_counters_nb);
	queue_work(priv->wq, &priv->monitor_counters_work);
	return NOTIFY_OK;
}

void mlx5e_monitor_counter_start(struct mlx5e_priv *priv)
{
	MLX5_NB_INIT(&priv->monitor_counters_nb, mlx5e_monitor_event_handler,
		     MONITOR_COUNTER);
	mlx5_eq_notifier_register(priv->mdev, &priv->monitor_counters_nb);
}

static void mlx5e_monitor_counter_stop(struct mlx5e_priv *priv)
{
	mlx5_eq_notifier_unregister(priv->mdev, &priv->monitor_counters_nb);
	cancel_work_sync(&priv->monitor_counters_work);
}

static int fill_monitor_counter_ppcnt_set1(int cnt, u32 *in)
{
	enum mlx5_monitor_counter_ppcnt ppcnt_cnt;

	for (ppcnt_cnt = 0;
	     ppcnt_cnt < NUM_REQ_PPCNT_COUNTER_S1;
	     ppcnt_cnt++, cnt++) {
		MLX5_SET(set_monitor_counter_in, in,
			 monitor_counter[cnt].type,
			 MLX5_QUERY_MONITOR_CNT_TYPE_PPCNT);
		MLX5_SET(set_monitor_counter_in, in,
			 monitor_counter[cnt].counter,
			 ppcnt_cnt);
	}
	return ppcnt_cnt;
}

static int fill_monitor_counter_q_counter_set1(int cnt, int q_counter, u32 *in)
{
	MLX5_SET(set_monitor_counter_in, in,
		 monitor_counter[cnt].type,
		 MLX5_QUERY_MONITOR_CNT_TYPE_Q_COUNTER);
	MLX5_SET(set_monitor_counter_in, in,
		 monitor_counter[cnt].counter,
		 MLX5_QUERY_MONITOR_Q_COUNTER_RX_OUT_OF_BUFFER);
	MLX5_SET(set_monitor_counter_in, in,
		 monitor_counter[cnt].counter_group_id,
		 q_counter);
	return 1;
}

/* check if mlx5e_monitor_counter_supported before calling this function*/
static void mlx5e_set_monitor_counter(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int max_num_of_counters = MLX5_CAP_GEN(mdev, max_num_of_monitor_counters);
	int num_q_counters      = MLX5_CAP_GEN(mdev, num_q_monitor_counters);
	int num_ppcnt_counters  = !MLX5_CAP_PCAM_REG(mdev, ppcnt) ? 0 :
				  MLX5_CAP_GEN(mdev, num_ppcnt_monitor_counters);
	u32  in[MLX5_ST_SZ_DW(set_monitor_counter_in)]  = {};
	u32 out[MLX5_ST_SZ_DW(set_monitor_counter_out)] = {};
	int q_counter = priv->q_counter;
	int cnt	= 0;

	if (num_ppcnt_counters  >=  NUM_REQ_PPCNT_COUNTER_S1 &&
	    max_num_of_counters >= (NUM_REQ_PPCNT_COUNTER_S1 + cnt))
		cnt += fill_monitor_counter_ppcnt_set1(cnt, in);

	if (num_q_counters      >=  NUM_REQ_Q_COUNTERS_S1 &&
	    max_num_of_counters >= (NUM_REQ_Q_COUNTERS_S1 + cnt) &&
	    q_counter)
		cnt += fill_monitor_counter_q_counter_set1(cnt, q_counter, in);

	MLX5_SET(set_monitor_counter_in, in, num_of_counters, cnt);
	MLX5_SET(set_monitor_counter_in, in, opcode,
		 MLX5_CMD_OP_SET_MONITOR_COUNTER);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

/* check if mlx5e_monitor_counter_supported before calling this function*/
void mlx5e_monitor_counter_init(struct mlx5e_priv *priv)
{
	INIT_WORK(&priv->monitor_counters_work, mlx5e_monitor_counters_work);
	mlx5e_monitor_counter_start(priv);
	mlx5e_set_monitor_counter(priv);
	mlx5e_monitor_counter_arm(priv);
	queue_work(priv->wq, &priv->update_stats_work);
}

static void mlx5e_monitor_counter_disable(struct mlx5e_priv *priv)
{
	u32  in[MLX5_ST_SZ_DW(set_monitor_counter_in)]  = {};
	u32 out[MLX5_ST_SZ_DW(set_monitor_counter_out)] = {};

	MLX5_SET(set_monitor_counter_in, in, num_of_counters, 0);
	MLX5_SET(set_monitor_counter_in, in, opcode,
		 MLX5_CMD_OP_SET_MONITOR_COUNTER);

	mlx5_cmd_exec(priv->mdev, in, sizeof(in), out, sizeof(out));
}

/* check if mlx5e_monitor_counter_supported before calling this function*/
void mlx5e_monitor_counter_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_monitor_counter_disable(priv);
	mlx5e_monitor_counter_stop(priv);
}
