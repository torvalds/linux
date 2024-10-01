/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#ifndef __MLX5_VHCA_EVENT_H__
#define __MLX5_VHCA_EVENT_H__

#ifdef CONFIG_MLX5_SF

struct mlx5_vhca_state_event {
	u16 function_id;
	u16 sw_function_id;
	u8 new_vhca_state;
};

static inline bool mlx5_vhca_event_supported(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN_MAX(dev, vhca_state);
}

void mlx5_vhca_state_cap_handle(struct mlx5_core_dev *dev, void *set_hca_cap);
int mlx5_vhca_event_init(struct mlx5_core_dev *dev);
void mlx5_vhca_event_cleanup(struct mlx5_core_dev *dev);
void mlx5_vhca_event_start(struct mlx5_core_dev *dev);
void mlx5_vhca_event_stop(struct mlx5_core_dev *dev);
int mlx5_vhca_event_notifier_register(struct mlx5_core_dev *dev, struct notifier_block *nb);
void mlx5_vhca_event_notifier_unregister(struct mlx5_core_dev *dev, struct notifier_block *nb);
int mlx5_modify_vhca_sw_id(struct mlx5_core_dev *dev, u16 function_id, u32 sw_fn_id);
int mlx5_vhca_event_arm(struct mlx5_core_dev *dev, u16 function_id);
int mlx5_cmd_query_vhca_state(struct mlx5_core_dev *dev, u16 function_id,
			      u32 *out, u32 outlen);
void mlx5_vhca_events_work_enqueue(struct mlx5_core_dev *dev, int idx, struct work_struct *work);
void mlx5_vhca_event_work_queues_flush(struct mlx5_core_dev *dev);

#else

static inline void mlx5_vhca_state_cap_handle(struct mlx5_core_dev *dev, void *set_hca_cap)
{
}

static inline int mlx5_vhca_event_init(struct mlx5_core_dev *dev)
{
	return 0;
}

static inline void mlx5_vhca_event_cleanup(struct mlx5_core_dev *dev)
{
}

static inline void mlx5_vhca_event_start(struct mlx5_core_dev *dev)
{
}

static inline void mlx5_vhca_event_stop(struct mlx5_core_dev *dev)
{
}

#endif

#endif
