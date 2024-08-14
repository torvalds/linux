/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_VNET_H__
#define __MLX5_VNET_H__

#include "mlx5_vdpa.h"

#define to_mlx5_vdpa_ndev(__mvdev)                                             \
	container_of(__mvdev, struct mlx5_vdpa_net, mvdev)
#define to_mvdev(__vdev) container_of((__vdev), struct mlx5_vdpa_dev, vdev)

struct mlx5_vdpa_net_resources {
	u32 tisn;
	u32 tdn;
	u32 tirn;
	u32 rqtn;
	bool valid;
	struct dentry *tirn_dent;
};

#define MLX5V_MACVLAN_SIZE 256

static inline u16 key2vid(u64 key)
{
	return (u16)(key >> 48) & 0xfff;
}

#define MLX5_VDPA_IRQ_NAME_LEN 32

struct mlx5_vdpa_irq_pool_entry {
	struct msi_map map;
	bool used;
	char name[MLX5_VDPA_IRQ_NAME_LEN];
	void *dev_id;
};

struct mlx5_vdpa_irq_pool {
	int num_ent;
	struct mlx5_vdpa_irq_pool_entry *entries;
};

struct mlx5_vdpa_net {
	struct mlx5_vdpa_dev mvdev;
	struct mlx5_vdpa_net_resources res;
	struct virtio_net_config config;
	struct mlx5_vdpa_virtqueue *vqs;
	struct vdpa_callback *event_cbs;

	/* Serialize vq resources creation and destruction. This is required
	 * since memory map might change and we need to destroy and create
	 * resources while driver in operational.
	 */
	struct rw_semaphore reslock;
	struct mlx5_flow_table *rxft;
	struct dentry *rx_dent;
	struct dentry *rx_table_dent;
	bool setup;
	bool needs_teardown;
	u32 cur_num_vqs;
	u32 rqt_size;
	bool nb_registered;
	struct notifier_block nb;
	struct vdpa_callback config_cb;
	struct mlx5_vdpa_wq_ent cvq_ent;
	struct hlist_head macvlan_hash[MLX5V_MACVLAN_SIZE];
	struct mlx5_vdpa_irq_pool irqp;
	struct dentry *debugfs;

	u32 umem_1_buffer_param_a;
	u32 umem_1_buffer_param_b;

	u32 umem_2_buffer_param_a;
	u32 umem_2_buffer_param_b;

	u32 umem_3_buffer_param_a;
	u32 umem_3_buffer_param_b;
};

struct mlx5_vdpa_counter {
	struct mlx5_fc *counter;
	struct dentry *dent;
	struct mlx5_core_dev *mdev;
};

struct macvlan_node {
	struct hlist_node hlist;
	struct mlx5_flow_handle *ucast_rule;
	struct mlx5_flow_handle *mcast_rule;
	u64 macvlan;
	struct mlx5_vdpa_net *ndev;
	bool tagged;
#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
	struct dentry *dent;
	struct mlx5_vdpa_counter ucast_counter;
	struct mlx5_vdpa_counter mcast_counter;
#endif
};

void mlx5_vdpa_add_debugfs(struct mlx5_vdpa_net *ndev);
void mlx5_vdpa_remove_debugfs(struct mlx5_vdpa_net *ndev);
void mlx5_vdpa_add_rx_flow_table(struct mlx5_vdpa_net *ndev);
void mlx5_vdpa_remove_rx_flow_table(struct mlx5_vdpa_net *ndev);
void mlx5_vdpa_add_tirn(struct mlx5_vdpa_net *ndev);
void mlx5_vdpa_remove_tirn(struct mlx5_vdpa_net *ndev);
#if defined(CONFIG_MLX5_VDPA_STEERING_DEBUG)
void mlx5_vdpa_add_rx_counters(struct mlx5_vdpa_net *ndev,
			       struct macvlan_node *node);
void mlx5_vdpa_remove_rx_counters(struct mlx5_vdpa_net *ndev,
				  struct macvlan_node *node);
#else
static inline void mlx5_vdpa_add_rx_counters(struct mlx5_vdpa_net *ndev,
					     struct macvlan_node *node) {}
static inline void mlx5_vdpa_remove_rx_counters(struct mlx5_vdpa_net *ndev,
						struct macvlan_node *node) {}
#endif


#endif /* __MLX5_VNET_H__ */
