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

	ibdev = mlx5_eswitch_uplink_get_proto_dev(dev->priv.eswitch, REP_IB);
	vport_index = rep->vport_index;

	ibdev->port[vport_index].rep = rep;
	rep->rep_data[REP_IB].priv = ibdev;
	write_lock(&ibdev->port[vport_index].roce.netdev_lock);
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(rep->esw, rep->vport);
	write_unlock(&ibdev->port[vport_index].roce.netdev_lock);

	return 0;
}

static int
mlx5_ib_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	int num_ports = mlx5_eswitch_get_total_vports(dev);
	const struct mlx5_ib_profile *profile;
	struct mlx5_ib_dev *ibdev;
	int vport_index;
	int ret;

	if (rep->vport == MLX5_VPORT_UPLINK)
		profile = &raw_eth_profile;
	else
		return mlx5_ib_set_vport_rep(dev, rep);

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
		mlx5_ib_get_rep_netdev(dev->priv.eswitch, rep->vport);
	ibdev->mdev = dev;
	ibdev->num_ports = num_ports;

	ret = __mlx5_ib_add(ibdev, profile);
	if (ret)
		goto fail_add;

	rep->rep_data[REP_IB].priv = ibdev;

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
	struct mlx5_ib_dev *dev = mlx5_ib_rep_to_dev(rep);
	struct mlx5_ib_port *port;

	port = &dev->port[rep->vport_index];
	write_lock(&port->roce.netdev_lock);
	port->roce.netdev = NULL;
	write_unlock(&port->roce.netdev_lock);
	rep->rep_data[REP_IB].priv = NULL;
	port->rep = NULL;

	if (rep->vport == MLX5_VPORT_UPLINK)
		__mlx5_ib_remove(dev, dev->profile, MLX5_IB_STAGE_MAX);
}

static const struct mlx5_eswitch_rep_ops rep_ops = {
	.load = mlx5_ib_vport_rep_load,
	.unload = mlx5_ib_vport_rep_unload,
	.get_proto_dev = mlx5_ib_rep_to_dev,
};

struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_num, REP_ETH);
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

	return mlx5_eswitch_add_send_to_vport_rule(esw, rep, sq->base.mqp.qpn);
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
