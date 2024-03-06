// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#include "macsec.h"
#include <linux/mlx5/macsec.h>

struct mlx5_reserved_gids {
	int macsec_index;
	const struct ib_gid_attr *physical_gid;
};

struct mlx5_roce_gids {
	struct list_head roce_gid_list_entry;
	u16 gid_idx;
	union {
		struct sockaddr_in  sockaddr_in;
		struct sockaddr_in6 sockaddr_in6;
	} addr;
};

struct mlx5_macsec_device {
	struct list_head macsec_devices_list_entry;
	void *macdev;
	struct list_head macsec_roce_gids;
	struct list_head tx_rules_list;
	struct list_head rx_rules_list;
};

static void cleanup_macsec_device(struct mlx5_macsec_device *macsec_device)
{
	if (!list_empty(&macsec_device->tx_rules_list) ||
	    !list_empty(&macsec_device->rx_rules_list) ||
	    !list_empty(&macsec_device->macsec_roce_gids))
		return;

	list_del(&macsec_device->macsec_devices_list_entry);
	kfree(macsec_device);
}

static struct mlx5_macsec_device *get_macsec_device(void *macdev,
						    struct list_head *macsec_devices_list)
{
	struct mlx5_macsec_device *iter, *macsec_device = NULL;

	list_for_each_entry(iter, macsec_devices_list, macsec_devices_list_entry) {
		if (iter->macdev == macdev) {
			macsec_device = iter;
			break;
		}
	}

	if (macsec_device)
		return macsec_device;

	macsec_device = kzalloc(sizeof(*macsec_device), GFP_KERNEL);
	if (!macsec_device)
		return NULL;

	macsec_device->macdev = macdev;
	INIT_LIST_HEAD(&macsec_device->tx_rules_list);
	INIT_LIST_HEAD(&macsec_device->rx_rules_list);
	INIT_LIST_HEAD(&macsec_device->macsec_roce_gids);
	list_add(&macsec_device->macsec_devices_list_entry, macsec_devices_list);

	return macsec_device;
}

static void mlx5_macsec_del_roce_gid(struct mlx5_macsec_device *macsec_device, u16 gid_idx)
{
	struct mlx5_roce_gids *current_gid, *next_gid;

	list_for_each_entry_safe(current_gid, next_gid, &macsec_device->macsec_roce_gids,
				 roce_gid_list_entry)
		if (current_gid->gid_idx == gid_idx) {
			list_del(&current_gid->roce_gid_list_entry);
			kfree(current_gid);
		}
}

static void mlx5_macsec_save_roce_gid(struct mlx5_macsec_device *macsec_device,
				      const struct sockaddr *addr, u16 gid_idx)
{
	struct mlx5_roce_gids *roce_gids;

	roce_gids = kzalloc(sizeof(*roce_gids), GFP_KERNEL);
	if (!roce_gids)
		return;

	roce_gids->gid_idx = gid_idx;
	if (addr->sa_family == AF_INET)
		memcpy(&roce_gids->addr.sockaddr_in, addr, sizeof(roce_gids->addr.sockaddr_in));
	else
		memcpy(&roce_gids->addr.sockaddr_in6, addr, sizeof(roce_gids->addr.sockaddr_in6));

	list_add_tail(&roce_gids->roce_gid_list_entry, &macsec_device->macsec_roce_gids);
}

static void handle_macsec_gids(struct list_head *macsec_devices_list,
			       struct mlx5_macsec_event_data *data)
{
	struct mlx5_macsec_device *macsec_device;
	struct mlx5_roce_gids *gid;

	macsec_device = get_macsec_device(data->macdev, macsec_devices_list);
	if (!macsec_device)
		return;

	list_for_each_entry(gid, &macsec_device->macsec_roce_gids, roce_gid_list_entry) {
		mlx5_macsec_add_roce_sa_rules(data->fs_id, (struct sockaddr *)&gid->addr,
					      gid->gid_idx, &macsec_device->tx_rules_list,
					      &macsec_device->rx_rules_list, data->macsec_fs,
					      data->is_tx);
	}
}

static void del_sa_roce_rule(struct list_head *macsec_devices_list,
			     struct mlx5_macsec_event_data *data)
{
	struct mlx5_macsec_device *macsec_device;

	macsec_device = get_macsec_device(data->macdev, macsec_devices_list);
	WARN_ON(!macsec_device);

	mlx5_macsec_del_roce_sa_rules(data->fs_id, data->macsec_fs,
				      &macsec_device->tx_rules_list,
				      &macsec_device->rx_rules_list, data->is_tx);
}

static int macsec_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5_macsec *macsec = container_of(nb, struct mlx5_macsec, blocking_events_nb);

	mutex_lock(&macsec->lock);
	switch (event) {
	case MLX5_DRIVER_EVENT_MACSEC_SA_ADDED:
		handle_macsec_gids(&macsec->macsec_devices_list, data);
		break;
	case MLX5_DRIVER_EVENT_MACSEC_SA_DELETED:
		del_sa_roce_rule(&macsec->macsec_devices_list, data);
		break;
	default:
		mutex_unlock(&macsec->lock);
		return NOTIFY_DONE;
	}
	mutex_unlock(&macsec->lock);
	return NOTIFY_OK;
}

void mlx5r_macsec_event_register(struct mlx5_ib_dev *dev)
{
	if (!mlx5_is_macsec_roce_supported(dev->mdev)) {
		mlx5_ib_dbg(dev, "RoCE MACsec not supported due to capabilities\n");
		return;
	}

	dev->macsec.blocking_events_nb.notifier_call = macsec_event;
	blocking_notifier_chain_register(&dev->mdev->macsec_nh,
					 &dev->macsec.blocking_events_nb);
}

void mlx5r_macsec_event_unregister(struct mlx5_ib_dev *dev)
{
	if (!mlx5_is_macsec_roce_supported(dev->mdev)) {
		mlx5_ib_dbg(dev, "RoCE MACsec not supported due to capabilities\n");
		return;
	}

	blocking_notifier_chain_unregister(&dev->mdev->macsec_nh,
					   &dev->macsec.blocking_events_nb);
}

int mlx5r_macsec_init_gids_and_devlist(struct mlx5_ib_dev *dev)
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

	INIT_LIST_HEAD(&dev->macsec.macsec_devices_list);
	mutex_init(&dev->macsec.lock);

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

	mutex_destroy(&dev->macsec.lock);
}

int mlx5r_add_gid_macsec_operations(const struct ib_gid_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(attr->device);
	struct mlx5_macsec_device *macsec_device;
	const struct ib_gid_attr *physical_gid;
	struct mlx5_reserved_gids *mgids;
	struct net_device *ndev;
	int ret = 0;
	union {
		struct sockaddr_in  sockaddr_in;
		struct sockaddr_in6 sockaddr_in6;
	} addr;

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
	dev_hold(ndev);
	rcu_read_unlock();

	mutex_lock(&dev->macsec.lock);
	macsec_device = get_macsec_device(ndev, &dev->macsec.macsec_devices_list);
	if (!macsec_device) {
		ret = -ENOMEM;
		goto dev_err;
	}

	physical_gid = rdma_find_gid(attr->device, &attr->gid,
				     attr->gid_type, NULL);
	if (!IS_ERR(physical_gid)) {
		ret = set_roce_addr(to_mdev(physical_gid->device),
				    physical_gid->port_num,
				    physical_gid->index, NULL,
				    physical_gid);
		if (ret)
			goto gid_err;

		mgids = &dev->port[attr->port_num - 1].reserved_gids[physical_gid->index];
		mgids->macsec_index = attr->index;
		mgids->physical_gid = physical_gid;
	}

	/* Proceed with adding steering rules, regardless if there was gid ambiguity or not.*/
	rdma_gid2ip((struct sockaddr *)&addr, &attr->gid);
	ret = mlx5_macsec_add_roce_rule(ndev, (struct sockaddr *)&addr, attr->index,
					&macsec_device->tx_rules_list,
					&macsec_device->rx_rules_list, dev->mdev->macsec_fs);
	if (ret && !IS_ERR(physical_gid))
		goto rule_err;

	mlx5_macsec_save_roce_gid(macsec_device, (struct sockaddr *)&addr, attr->index);

	dev_put(ndev);
	mutex_unlock(&dev->macsec.lock);
	return ret;

rule_err:
	set_roce_addr(to_mdev(physical_gid->device), physical_gid->port_num,
		      physical_gid->index, &physical_gid->gid, physical_gid);
	mgids->macsec_index = -1;
gid_err:
	rdma_put_gid_attr(physical_gid);
	cleanup_macsec_device(macsec_device);
dev_err:
	dev_put(ndev);
	mutex_unlock(&dev->macsec.lock);
	return ret;
}

void mlx5r_del_gid_macsec_operations(const struct ib_gid_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(attr->device);
	struct mlx5_macsec_device *macsec_device;
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
	dev_hold(ndev);
	rcu_read_unlock();

	mutex_lock(&dev->macsec.lock);
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
	macsec_device = get_macsec_device(ndev, &dev->macsec.macsec_devices_list);
	mlx5_macsec_del_roce_rule(attr->index, dev->mdev->macsec_fs,
				  &macsec_device->tx_rules_list, &macsec_device->rx_rules_list);
	mlx5_macsec_del_roce_gid(macsec_device, attr->index);
	cleanup_macsec_device(macsec_device);

	dev_put(ndev);
	mutex_unlock(&dev->macsec.lock);
}
