/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#include "ib_rep.h"

static int
mlx5_ib_nic_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	return 0;
}

static void
mlx5_ib_nic_rep_unload(struct mlx5_eswitch_rep *rep)
{
}

static int
mlx5_ib_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	return 0;
}

static void
mlx5_ib_vport_rep_unload(struct mlx5_eswitch_rep *rep)
{
}

static void *mlx5_ib_vport_get_proto_dev(struct mlx5_eswitch_rep *rep)
{
	return mlx5_ib_rep_to_dev(rep);
}

static void mlx5_ib_rep_register_vf_vports(struct mlx5_ib_dev *dev)
{
	struct mlx5_eswitch *esw   = dev->mdev->priv.eswitch;
	int total_vfs = MLX5_TOTAL_VPORTS(dev->mdev);
	int vport;

	for (vport = 1; vport < total_vfs; vport++) {
		struct mlx5_eswitch_rep_if rep_if = {};

		rep_if.load = mlx5_ib_vport_rep_load;
		rep_if.unload = mlx5_ib_vport_rep_unload;
		rep_if.get_proto_dev = mlx5_ib_vport_get_proto_dev;
		mlx5_eswitch_register_vport_rep(esw, vport, &rep_if, REP_IB);
	}
}

static void mlx5_ib_rep_unregister_vf_vports(struct mlx5_ib_dev *dev)
{
	struct mlx5_eswitch *esw   = dev->mdev->priv.eswitch;
	int total_vfs = MLX5_TOTAL_VPORTS(dev->mdev);
	int vport;

	for (vport = 1; vport < total_vfs; vport++)
		mlx5_eswitch_unregister_vport_rep(esw, vport, REP_IB);
}

void mlx5_ib_register_vport_reps(struct mlx5_ib_dev *dev)
{
	struct mlx5_eswitch *esw = dev->mdev->priv.eswitch;
	struct mlx5_eswitch_rep_if rep_if = {};

	rep_if.load = mlx5_ib_nic_rep_load;
	rep_if.unload = mlx5_ib_nic_rep_unload;
	rep_if.get_proto_dev = mlx5_ib_vport_get_proto_dev;
	rep_if.priv = dev;

	mlx5_eswitch_register_vport_rep(esw, 0, &rep_if, REP_IB);

	mlx5_ib_rep_register_vf_vports(dev);
}

void mlx5_ib_unregister_vport_reps(struct mlx5_ib_dev *dev)
{
	struct mlx5_eswitch *esw   = dev->mdev->priv.eswitch;

	mlx5_ib_rep_unregister_vf_vports(dev); /* VFs vports */
	mlx5_eswitch_unregister_vport_rep(esw, 0, REP_IB); /* UPLINK PF*/
}

u8 mlx5_ib_eswitch_mode(struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_mode(esw);
}

struct mlx5_ib_dev *mlx5_ib_get_rep_ibdev(struct mlx5_eswitch *esw,
					  int vport_index)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_index, REP_IB);
}

struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  int vport_index)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_index, REP_ETH);
}

struct mlx5_eswitch_rep *mlx5_ib_vport_rep(struct mlx5_eswitch *esw, int vport)
{
	return mlx5_eswitch_vport_rep(esw, vport);
}

int create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
			      struct mlx5_ib_sq *sq)
{
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_eswitch *esw = dev->mdev->priv.eswitch;

	if (!dev->rep)
		return 0;

	flow_rule =
		mlx5_eswitch_add_send_to_vport_rule(esw,
						    dev->rep->vport,
						    sq->base.mqp.qpn);
	if (IS_ERR(flow_rule))
		return PTR_ERR(flow_rule);
	sq->flow_rule = flow_rule;

	return 0;
}
