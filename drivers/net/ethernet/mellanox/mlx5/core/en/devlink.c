// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved. */

#include "en/devlink.h"
#include "eswitch.h"

static const struct devlink_ops mlx5e_devlink_ops = {
};

struct mlx5e_dev *mlx5e_create_devlink(struct device *dev,
				       struct mlx5_core_dev *mdev)
{
	struct mlx5e_dev *mlx5e_dev;
	struct devlink *devlink;

	devlink = devlink_alloc_ns(&mlx5e_devlink_ops, sizeof(*mlx5e_dev),
				   devlink_net(priv_to_devlink(mdev)), dev);
	if (!devlink)
		return ERR_PTR(-ENOMEM);
	devlink_register(devlink);
	return devlink_priv(devlink);
}

void mlx5e_destroy_devlink(struct mlx5e_dev *mlx5e_dev)
{
	struct devlink *devlink = priv_to_devlink(mlx5e_dev);

	devlink_unregister(devlink);
	devlink_free(devlink);
}

static void
mlx5e_devlink_get_port_parent_id(struct mlx5_core_dev *dev, struct netdev_phys_item_id *ppid)
{
	u64 parent_id;

	parent_id = mlx5_query_nic_system_image_guid(dev);
	ppid->id_len = sizeof(parent_id);
	memcpy(ppid->id, &parent_id, sizeof(parent_id));
}

int mlx5e_devlink_port_register(struct mlx5e_dev *mlx5e_dev,
				struct mlx5_core_dev *mdev)
{
	struct devlink *devlink = priv_to_devlink(mlx5e_dev);
	struct devlink_port_attrs attrs = {};
	struct netdev_phys_item_id ppid = {};
	unsigned int dl_port_index;

	if (mlx5_core_is_pf(mdev)) {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
		attrs.phys.port_number = mlx5_get_dev_index(mdev);
		if (MLX5_ESWITCH_MANAGER(mdev)) {
			mlx5e_devlink_get_port_parent_id(mdev, &ppid);
			memcpy(attrs.switch_id.id, ppid.id, ppid.id_len);
			attrs.switch_id.id_len = ppid.id_len;
		}
		dl_port_index = mlx5_esw_vport_to_devlink_port_index(mdev,
								     MLX5_VPORT_UPLINK);
	} else {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_VIRTUAL;
		dl_port_index = mlx5_esw_vport_to_devlink_port_index(mdev, 0);
	}

	devlink_port_attrs_set(&mlx5e_dev->dl_port, &attrs);

	return devlink_port_register(devlink, &mlx5e_dev->dl_port,
				     dl_port_index);
}

void mlx5e_devlink_port_unregister(struct mlx5e_dev *mlx5e_dev)
{
	devlink_port_unregister(&mlx5e_dev->dl_port);
}
