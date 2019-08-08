// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#include <linux/mlx5/vport.h>
#include "ib_rep.h"
#include "srq.h"

static int
mlx5_ib_set_vport_rep(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	struct mlx5_ib_dev *ibdev;
	int vport_index;

	ibdev = mlx5_ib_get_uplink_ibdev(dev->priv.eswitch);
	vport_index = ibdev->free_port++;

	ibdev->port[vport_index].rep = rep;
	write_lock(&ibdev->port[vport_index].roce.netdev_lock);
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(dev->priv.eswitch, rep->vport);
	write_unlock(&ibdev->port[vport_index].roce.netdev_lock);

	return 0;
}

static int
mlx5_ib_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	int num_ports = MLX5_TOTAL_VPORTS(dev);
	const struct mlx5_ib_profile *profile;
	struct mlx5_ib_dev *ibdev;
	int vport_index;

	if (rep->vport == MLX5_VPORT_UPLINK)
		profile = &uplink_rep_profile;
	else
		return mlx5_ib_set_vport_rep(dev, rep);

	ibdev = ib_alloc_device(mlx5_ib_dev, ib_dev);
	if (!ibdev)
		return -ENOMEM;

	ibdev->port = kcalloc(num_ports, sizeof(*ibdev->port),
			      GFP_KERNEL);
	if (!ibdev->port) {
		ib_dealloc_device(&ibdev->ib_dev);
		return -ENOMEM;
	}

	ibdev->is_rep = true;
	vport_index = ibdev->free_port++;
	ibdev->port[vport_index].rep = rep;
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(dev->priv.eswitch, rep->vport);
	ibdev->mdev = dev;
	ibdev->num_ports = num_ports;

	if (!__mlx5_ib_add(ibdev, profile))
		return -EINVAL;

	rep->rep_if[REP_IB].priv = ibdev;

	return 0;
}

static void
mlx5_ib_vport_rep_unload(struct mlx5_eswitch_rep *rep)
{
	struct mlx5_ib_dev *dev;

	if (!rep->rep_if[REP_IB].priv ||
	    rep->vport != MLX5_VPORT_UPLINK)
		return;

	dev = mlx5_ib_rep_to_dev(rep);
	__mlx5_ib_remove(dev, dev->profile, MLX5_IB_STAGE_MAX);
	rep->rep_if[REP_IB].priv = NULL;
}

static void *mlx5_ib_vport_get_proto_dev(struct mlx5_eswitch_rep *rep)
{
	return mlx5_ib_rep_to_dev(rep);
}

void mlx5_ib_register_vport_reps(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	struct mlx5_eswitch_rep_if rep_if = {};

	rep_if.load = mlx5_ib_vport_rep_load;
	rep_if.unload = mlx5_ib_vport_rep_unload;
	rep_if.get_proto_dev = mlx5_ib_vport_get_proto_dev;

	mlx5_eswitch_register_vport_reps(esw, &rep_if, REP_IB);
}

void mlx5_ib_unregister_vport_reps(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	mlx5_eswitch_unregister_vport_reps(esw, REP_IB);
}

u8 mlx5_ib_eswitch_mode(struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_mode(esw);
}

struct mlx5_ib_dev *mlx5_ib_get_rep_ibdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_num, REP_IB);
}

struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_num, REP_ETH);
}

struct mlx5_ib_dev *mlx5_ib_get_uplink_ibdev(struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_uplink_get_proto_dev(esw, REP_IB);
}

struct mlx5_eswitch_rep *mlx5_ib_vport_rep(struct mlx5_eswitch *esw,
					   u16 vport_num)
{
	return mlx5_eswitch_vport_rep(esw, vport_num);
}

struct mlx5_flow_handle *create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
						   struct mlx5_ib_sq *sq,
						   u16 port)
{
	struct mlx5_eswitch *esw = dev->mdev->priv.eswitch;
	struct mlx5_eswitch_rep *rep;

	if (!dev->is_rep || !port)
		return NULL;

	if (!dev->port[port - 1].rep)
		return ERR_PTR(-EINVAL);

	rep = dev->port[port - 1].rep;

	return mlx5_eswitch_add_send_to_vport_rule(esw, rep->vport,
						   sq->base.mqp.qpn);
}
