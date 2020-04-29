/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies */

#ifndef __LIB_MLX5_DEVCOM_H__
#define __LIB_MLX5_DEVCOM_H__

#include <linux/mlx5/driver.h>

enum mlx5_devcom_components {
	MLX5_DEVCOM_ESW_OFFLOADS,

	MLX5_DEVCOM_NUM_COMPONENTS,
};

typedef int (*mlx5_devcom_event_handler_t)(int event,
					   void *my_data,
					   void *event_data);

struct mlx5_devcom *mlx5_devcom_register_device(struct mlx5_core_dev *dev);
void mlx5_devcom_unregister_device(struct mlx5_devcom *devcom);

void mlx5_devcom_register_component(struct mlx5_devcom *devcom,
				    enum mlx5_devcom_components id,
				    mlx5_devcom_event_handler_t handler,
				    void *data);
void mlx5_devcom_unregister_component(struct mlx5_devcom *devcom,
				      enum mlx5_devcom_components id);

int mlx5_devcom_send_event(struct mlx5_devcom *devcom,
			   enum mlx5_devcom_components id,
			   int event,
			   void *event_data);

void mlx5_devcom_set_paired(struct mlx5_devcom *devcom,
			    enum mlx5_devcom_components id,
			    bool paired);
bool mlx5_devcom_is_paired(struct mlx5_devcom *devcom,
			   enum mlx5_devcom_components id);

void *mlx5_devcom_get_peer_data(struct mlx5_devcom *devcom,
				enum mlx5_devcom_components id);
void mlx5_devcom_release_peer_data(struct mlx5_devcom *devcom,
				   enum mlx5_devcom_components id);

#endif

