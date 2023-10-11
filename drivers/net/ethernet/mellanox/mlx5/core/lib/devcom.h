/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies */

#ifndef __LIB_MLX5_DEVCOM_H__
#define __LIB_MLX5_DEVCOM_H__

#include <linux/mlx5/driver.h>

enum mlx5_devcom_component {
	MLX5_DEVCOM_ESW_OFFLOADS,
	MLX5_DEVCOM_NUM_COMPONENTS,
};

typedef int (*mlx5_devcom_event_handler_t)(int event,
					   void *my_data,
					   void *event_data);

struct mlx5_devcom_dev *mlx5_devcom_register_device(struct mlx5_core_dev *dev);
void mlx5_devcom_unregister_device(struct mlx5_devcom_dev *devc);

struct mlx5_devcom_comp_dev *
mlx5_devcom_register_component(struct mlx5_devcom_dev *devc,
			       enum mlx5_devcom_component id,
			       u64 key,
			       mlx5_devcom_event_handler_t handler,
			       void *data);
void mlx5_devcom_unregister_component(struct mlx5_devcom_comp_dev *devcom);

int mlx5_devcom_send_event(struct mlx5_devcom_comp_dev *devcom,
			   int event, int rollback_event,
			   void *event_data);

void mlx5_devcom_comp_set_ready(struct mlx5_devcom_comp_dev *devcom, bool ready);
bool mlx5_devcom_comp_is_ready(struct mlx5_devcom_comp_dev *devcom);

bool mlx5_devcom_for_each_peer_begin(struct mlx5_devcom_comp_dev *devcom);
void mlx5_devcom_for_each_peer_end(struct mlx5_devcom_comp_dev *devcom);
void *mlx5_devcom_get_next_peer_data(struct mlx5_devcom_comp_dev *devcom,
				     struct mlx5_devcom_comp_dev **pos);

#define mlx5_devcom_for_each_peer_entry(devcom, data, pos)                    \
	for (pos = NULL, data = mlx5_devcom_get_next_peer_data(devcom, &pos); \
	     data;                                                            \
	     data = mlx5_devcom_get_next_peer_data(devcom, &pos))

void *mlx5_devcom_get_next_peer_data_rcu(struct mlx5_devcom_comp_dev *devcom,
					 struct mlx5_devcom_comp_dev **pos);

#define mlx5_devcom_for_each_peer_entry_rcu(devcom, data, pos)                    \
	for (pos = NULL, data = mlx5_devcom_get_next_peer_data_rcu(devcom, &pos); \
	     data;								  \
	     data = mlx5_devcom_get_next_peer_data_rcu(devcom, &pos))

#endif /* __LIB_MLX5_DEVCOM_H__ */
