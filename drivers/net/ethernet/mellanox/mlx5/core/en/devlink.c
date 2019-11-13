// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved. */

#include "en/devlink.h"

int mlx5e_devlink_phy_port_register(struct net_device *dev)
{
	struct mlx5e_priv *priv;
	struct devlink *devlink;
	int err;

	priv = netdev_priv(dev);
	devlink = priv_to_devlink(priv->mdev);

	devlink_port_attrs_set(&priv->dl_phy_port,
			       DEVLINK_PORT_FLAVOUR_PHYSICAL,
			       PCI_FUNC(priv->mdev->pdev->devfn),
			       false, 0,
			       NULL, 0);
	err = devlink_port_register(devlink, &priv->dl_phy_port, 1);
	if (err)
		return err;
	devlink_port_type_eth_set(&priv->dl_phy_port, dev);
	return 0;
}

void mlx5e_devlink_phy_port_unregister(struct mlx5e_priv *priv)
{
	devlink_port_unregister(&priv->dl_phy_port);
}

struct devlink_port *mlx5e_get_devlink_phy_port(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return &priv->dl_phy_port;
}

