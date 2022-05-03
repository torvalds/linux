// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#include "macsec.h"

struct mlx5_reserved_gids {
	int macsec_index;
	const struct ib_gid_attr *physical_gid;
};

int mlx5r_macsec_alloc_gids(struct mlx5_ib_dev *dev)
{
	int i, j, max_gids;

	if (!mlx5_is_macsec_roce_supported(dev->mdev)) {
		mlx5_ib_dbg(dev, "RoCE MACsec not supported due to capabilities\n");
		return 0;
	}

	max_gids = MLX5_CAP_ROCE(dev->mdev, roce_address_table_size);
	for (i = 0; i < dev->num_ports; i++) {
		dev->port[i].reserved_gids = kcalloc(max_gids,
						     sizeof(*dev->port[i].reserved_gids),
						     GFP_KERNEL);
		if (!dev->port[i].reserved_gids)
			goto err;

		for (j = 0; j < max_gids; j++)
			dev->port[i].reserved_gids[j].macsec_index = -1;
	}

	return 0;
err:
	while (i >= 0) {
		kfree(dev->port[i].reserved_gids);
		i--;
	}
	return -ENOMEM;
}

void mlx5r_macsec_dealloc_gids(struct mlx5_ib_dev *dev)
{
	int i;

	if (!mlx5_is_macsec_roce_supported(dev->mdev))
		mlx5_ib_dbg(dev, "RoCE MACsec not supported due to capabilities\n");

	for (i = 0; i < dev->num_ports; i++)
		kfree(dev->port[i].reserved_gids);
}

int mlx5r_add_gid_macsec_operations(const struct ib_gid_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(attr->device);
	const struct ib_gid_attr *physical_gid;
	struct mlx5_reserved_gids *mgids;
	struct net_device *ndev;
	int ret = 0;

	if (attr->gid_type != IB_GID_TYPE_ROCE_UDP_ENCAP)
		return 0;

	if (!mlx5_is_macsec_roce_supported(dev->mdev)) {
		mlx5_ib_dbg(dev, "RoCE MACsec not supported due to capabilities\n");
		return 0;
	}

	rcu_read_lock();
	ndev = rcu_dereference(attr->ndev);
	if (!ndev) {
		rcu_read_unlock();
		return -ENODEV;
	}

	if (!netif_is_macsec(ndev) || !macsec_netdev_is_offloaded(ndev)) {
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	physical_gid = rdma_find_gid(attr->device, &attr->gid,
				     attr->gid_type, NULL);
	if (IS_ERR(physical_gid))
		return 0;

	ret = set_roce_addr(to_mdev(physical_gid->device),
			    physical_gid->port_num,
			    physical_gid->index, NULL,
			    physical_gid);
	if (ret)
		goto gid_err;

	mgids = &dev->port[attr->port_num - 1].reserved_gids[physical_gid->index];
	mgids->macsec_index = attr->index;
	mgids->physical_gid = physical_gid;

	return 0;

gid_err:
	rdma_put_gid_attr(physical_gid);
	return ret;
}

void mlx5r_del_gid_macsec_operations(const struct ib_gid_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(attr->device);
	struct mlx5_reserved_gids *mgids;
	struct net_device *ndev;
	int i, max_gids;

	if (attr->gid_type != IB_GID_TYPE_ROCE_UDP_ENCAP)
		return;

	if (!mlx5_is_macsec_roce_supported(dev->mdev)) {
		mlx5_ib_dbg(dev, "RoCE MACsec not supported due to capabilities\n");
		return;
	}

	mgids = &dev->port[attr->port_num - 1].reserved_gids[attr->index];
	if (mgids->macsec_index != -1) { /* Checking if physical gid has ambiguous IP */
		rdma_put_gid_attr(mgids->physical_gid);
		mgids->macsec_index = -1;
		return;
	}

	rcu_read_lock();
	ndev = rcu_dereference(attr->ndev);
	if (!ndev) {
		rcu_read_unlock();
		return;
	}

	if (!netif_is_macsec(ndev) || !macsec_netdev_is_offloaded(ndev)) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	max_gids = MLX5_CAP_ROCE(dev->mdev, roce_address_table_size);
	for (i = 0; i < max_gids; i++) { /* Checking if macsec gid has ambiguous IP */
		mgids = &dev->port[attr->port_num - 1].reserved_gids[i];
		if (mgids->macsec_index == attr->index) {
			const struct ib_gid_attr *physical_gid = mgids->physical_gid;

			set_roce_addr(to_mdev(physical_gid->device),
				      physical_gid->port_num,
				      physical_gid->index,
				      &physical_gid->gid, physical_gid);

			rdma_put_gid_attr(physical_gid);
			mgids->macsec_index = -1;
			break;
		}
	}
}
