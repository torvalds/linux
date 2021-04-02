// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/netdevice.h>
#include <net/netevent.h>
#include <net/switchdev.h>
#include "bridge.h"
#include "esw/bridge.h"
#include "en_rep.h"

static int mlx5_esw_bridge_port_changeupper(struct notifier_block *nb, void *ptr)
{
	struct mlx5_esw_bridge_offloads *br_offloads = container_of(nb,
								    struct mlx5_esw_bridge_offloads,
								    netdev_nb);
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5_eswitch *esw;
	struct mlx5_vport *vport;
	struct net_device *upper;
	struct mlx5e_priv *priv;
	u16 vport_num;

	if (!mlx5e_eswitch_rep(dev))
		return 0;

	upper = info->upper_dev;
	if (!netif_is_bridge_master(upper))
		return 0;

	esw = br_offloads->esw;
	priv = netdev_priv(dev);
	if (esw != priv->mdev->priv.eswitch)
		return 0;

	rpriv = priv->ppriv;
	vport_num = rpriv->rep->vport;
	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return PTR_ERR(vport);

	extack = netdev_notifier_info_to_extack(&info->info);

	return info->linking ?
		mlx5_esw_bridge_vport_link(upper->ifindex, br_offloads, vport, extack) :
		mlx5_esw_bridge_vport_unlink(upper->ifindex, br_offloads, vport, extack);
}

static int mlx5_esw_bridge_switchdev_port_event(struct notifier_block *nb,
						unsigned long event, void *ptr)
{
	int err = 0;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		break;

	case NETDEV_CHANGEUPPER:
		err = mlx5_esw_bridge_port_changeupper(nb, ptr);
		break;
	}

	return notifier_from_errno(err);
}

void mlx5e_rep_bridge_init(struct mlx5e_priv *priv)
{
	struct mlx5_esw_bridge_offloads *br_offloads;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw =
		mdev->priv.eswitch;
	int err;

	rtnl_lock();
	br_offloads = mlx5_esw_bridge_init(esw);
	rtnl_unlock();
	if (IS_ERR(br_offloads)) {
		esw_warn(mdev, "Failed to init esw bridge (err=%ld)\n", PTR_ERR(br_offloads));
		return;
	}

	br_offloads->netdev_nb.notifier_call = mlx5_esw_bridge_switchdev_port_event;
	err = register_netdevice_notifier(&br_offloads->netdev_nb);
	if (err) {
		esw_warn(mdev, "Failed to register bridge offloads netdevice notifier (err=%d)\n",
			 err);
		mlx5_esw_bridge_cleanup(esw);
	}
}

void mlx5e_rep_bridge_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5_esw_bridge_offloads *br_offloads;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw =
		mdev->priv.eswitch;

	br_offloads = esw->br_offloads;
	if (!br_offloads)
		return;

	unregister_netdevice_notifier(&br_offloads->netdev_nb);
	rtnl_lock();
	mlx5_esw_bridge_cleanup(esw);
	rtnl_unlock();
}
