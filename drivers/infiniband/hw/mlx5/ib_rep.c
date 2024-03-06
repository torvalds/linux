// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#include <linux/mlx5/vport.h>
#include "ib_rep.h"
#include "srq.h"

static int
mlx5_ib_set_vport_rep(struct mlx5_core_dev *dev,
		      struct mlx5_eswitch_rep *rep,
		      int vport_index)
{
	struct mlx5_ib_dev *ibdev;

	ibdev = mlx5_eswitch_uplink_get_proto_dev(dev->priv.eswitch, REP_IB);
	if (!ibdev)
		return -EINVAL;

	ibdev->port[vport_index].rep = rep;
	rep->rep_data[REP_IB].priv = ibdev;
	write_lock(&ibdev->port[vport_index].roce.netdev_lock);
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(rep->esw, rep->vport);
	write_unlock(&ibdev->port[vport_index].roce.netdev_lock);

	return 0;
}

static void mlx5_ib_register_peer_vport_reps(struct mlx5_core_dev *mdev);

static void mlx5_ib_num_ports_update(struct mlx5_core_dev *dev, u32 *num_ports)
{
	struct mlx5_core_dev *peer_dev;
	int i;

	mlx5_lag_for_each_peer_mdev(dev, peer_dev, i) {
		u32 peer_num_ports = mlx5_eswitch_get_total_vports(peer_dev);

		if (mlx5_lag_is_mpesw(peer_dev))
			*num_ports += peer_num_ports;
		else
			/* Only 1 ib port is the representor for all uplinks */
			*num_ports += peer_num_ports - 1;
	}
}

static int
mlx5_ib_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	u32 num_ports = mlx5_eswitch_get_total_vports(dev);
	struct mlx5_core_dev *lag_master = dev;
	const struct mlx5_ib_profile *profile;
	struct mlx5_core_dev *peer_dev;
	struct mlx5_ib_dev *ibdev;
	int new_uplink = false;
	int vport_index;
	int ret;
	int i;

	vport_index = rep->vport_index;

	if (mlx5_lag_is_shared_fdb(dev)) {
		if (mlx5_lag_is_master(dev)) {
			mlx5_ib_num_ports_update(dev, &num_ports);
		} else {
			if (rep->vport == MLX5_VPORT_UPLINK) {
				if (!mlx5_lag_is_mpesw(dev))
					return 0;
				new_uplink = true;
			}
			mlx5_lag_for_each_peer_mdev(dev, peer_dev, i) {
				u32 peer_n_ports = mlx5_eswitch_get_total_vports(peer_dev);

				if (mlx5_lag_is_master(peer_dev))
					lag_master = peer_dev;
				else if (!mlx5_lag_is_mpesw(dev))
				/* Only 1 ib port is the representor for all uplinks */
					peer_n_ports--;

				if (mlx5_get_dev_index(peer_dev) < mlx5_get_dev_index(dev))
					vport_index += peer_n_ports;
			}
		}
	}

	if (rep->vport == MLX5_VPORT_UPLINK && !new_uplink)
		profile = &raw_eth_profile;
	else
		return mlx5_ib_set_vport_rep(lag_master, rep, vport_index);

	ibdev = ib_alloc_device(mlx5_ib_dev, ib_dev);
	if (!ibdev)
		return -ENOMEM;

	ibdev->port = kcalloc(num_ports, sizeof(*ibdev->port),
			      GFP_KERNEL);
	if (!ibdev->port) {
		ret = -ENOMEM;
		goto fail_port;
	}

	ibdev->is_rep = true;
	vport_index = rep->vport_index;
	ibdev->port[vport_index].rep = rep;
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(lag_master->priv.eswitch, rep->vport);
	ibdev->mdev = lag_master;
	ibdev->num_ports = num_ports;

	ret = __mlx5_ib_add(ibdev, profile);
	if (ret)
		goto fail_add;

	rep->rep_data[REP_IB].priv = ibdev;
	if (mlx5_lag_is_shared_fdb(lag_master))
		mlx5_ib_register_peer_vport_reps(lag_master);

	return 0;

fail_add:
	kfree(ibdev->port);
fail_port:
	ib_dealloc_device(&ibdev->ib_dev);
	return ret;
}

static void *mlx5_ib_rep_to_dev(struct mlx5_eswitch_rep *rep)
{
	return rep->rep_data[REP_IB].priv;
}

static void
mlx5_ib_vport_rep_unload(struct mlx5_eswitch_rep *rep)
{
	struct mlx5_core_dev *mdev = mlx5_eswitch_get_core_dev(rep->esw);
	struct mlx5_ib_dev *dev = mlx5_ib_rep_to_dev(rep);
	int vport_index = rep->vport_index;
	struct mlx5_ib_port *port;
	int i;

	if (WARN_ON(!mdev))
		return;

	if (!dev)
		return;

	if (mlx5_lag_is_shared_fdb(mdev) &&
	    !mlx5_lag_is_master(mdev)) {
		if (rep->vport == MLX5_VPORT_UPLINK && !mlx5_lag_is_mpesw(mdev))
			return;
		for (i = 0; i < dev->num_ports; i++) {
			if (dev->port[i].rep == rep)
				break;
		}
		if (WARN_ON(i == dev->num_ports))
			return;
		vport_index = i;
	}

	port = &dev->port[vport_index];
	write_lock(&port->roce.netdev_lock);
	port->roce.netdev = NULL;
	write_unlock(&port->roce.netdev_lock);
	rep->rep_data[REP_IB].priv = NULL;
	port->rep = NULL;

	if (rep->vport == MLX5_VPORT_UPLINK) {

		if (mlx5_lag_is_shared_fdb(mdev) && !mlx5_lag_is_master(mdev))
			return;

		if (mlx5_lag_is_shared_fdb(mdev)) {
			struct mlx5_core_dev *peer_mdev;
			struct mlx5_eswitch *esw;

			mlx5_lag_for_each_peer_mdev(mdev, peer_mdev, i) {
				esw = peer_mdev->priv.eswitch;
				mlx5_eswitch_unregister_vport_reps(esw, REP_IB);
			}
		}
		__mlx5_ib_remove(dev, dev->profile, MLX5_IB_STAGE_MAX);
	}
}

static const struct mlx5_eswitch_rep_ops rep_ops = {
	.load = mlx5_ib_vport_rep_load,
	.unload = mlx5_ib_vport_rep_unload,
	.get_proto_dev = mlx5_ib_rep_to_dev,
};

static void mlx5_ib_register_peer_vport_reps(struct mlx5_core_dev *mdev)
{
	struct mlx5_core_dev *peer_mdev;
	struct mlx5_eswitch *esw;
	int i;

	mlx5_lag_for_each_peer_mdev(mdev, peer_mdev, i) {
		esw = peer_mdev->priv.eswitch;
		mlx5_eswitch_register_vport_reps(esw, &rep_ops, REP_IB);
	}
}

struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_num, REP_ETH);
}

struct mlx5_flow_handle *create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
						   struct mlx5_ib_sq *sq,
						   u32 port)
{
	struct mlx5_eswitch *esw = dev->mdev->priv.eswitch;
	struct mlx5_eswitch_rep *rep;

	if (!dev->is_rep || !port)
		return NULL;

	if (!dev->port[port - 1].rep)
		return ERR_PTR(-EINVAL);

	rep = dev->port[port - 1].rep;

	return mlx5_eswitch_add_send_to_vport_rule(esw, esw, rep, sq->base.mqp.qpn);
}

static int mlx5r_rep_probe(struct auxiliary_device *adev,
			   const struct auxiliary_device_id *id)
{
	struct mlx5_adev *idev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = idev->mdev;
	struct mlx5_eswitch *esw;

	esw = mdev->priv.eswitch;
	mlx5_eswitch_register_vport_reps(esw, &rep_ops, REP_IB);
	return 0;
}

static void mlx5r_rep_remove(struct auxiliary_device *adev)
{
	struct mlx5_adev *idev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = idev->mdev;
	struct mlx5_eswitch *esw;

	esw = mdev->priv.eswitch;
	mlx5_eswitch_unregister_vport_reps(esw, REP_IB);
}

static const struct auxiliary_device_id mlx5r_rep_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".rdma-rep", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx5r_rep_id_table);

static struct auxiliary_driver mlx5r_rep_driver = {
	.name = "rep",
	.probe = mlx5r_rep_probe,
	.remove = mlx5r_rep_remove,
	.id_table = mlx5r_rep_id_table,
};

int mlx5r_rep_init(void)
{
	return auxiliary_driver_register(&mlx5r_rep_driver);
}

void mlx5r_rep_cleanup(void)
{
	auxiliary_driver_unregister(&mlx5r_rep_driver);
}
