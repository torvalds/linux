// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#include <linux/mlx5/driver.h>
#include "eswitch.h"
#include "priv.h"

struct mlx5_sf {
	struct devlink_port dl_port;
	unsigned int port_index;
	u16 id;
};

struct mlx5_sf_table {
	struct mlx5_core_dev *dev; /* To refer from notifier context. */
	struct xarray port_indices; /* port index based lookup. */
	refcount_t refcount;
	struct completion disable_complete;
	struct notifier_block esw_nb;
};

static struct mlx5_sf *
mlx5_sf_lookup_by_index(struct mlx5_sf_table *table, unsigned int port_index)
{
	return xa_load(&table->port_indices, port_index);
}

static int mlx5_sf_id_insert(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	return xa_insert(&table->port_indices, sf->port_index, sf, GFP_KERNEL);
}

static void mlx5_sf_id_erase(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	xa_erase(&table->port_indices, sf->port_index);
}

static struct mlx5_sf *
mlx5_sf_alloc(struct mlx5_sf_table *table, u32 sfnum, struct netlink_ext_ack *extack)
{
	unsigned int dl_port_index;
	struct mlx5_sf *sf;
	u16 hw_fn_id;
	int id_err;
	int err;

	id_err = mlx5_sf_hw_table_sf_alloc(table->dev, sfnum);
	if (id_err < 0) {
		err = id_err;
		goto id_err;
	}

	sf = kzalloc(sizeof(*sf), GFP_KERNEL);
	if (!sf) {
		err = -ENOMEM;
		goto alloc_err;
	}
	sf->id = id_err;
	hw_fn_id = mlx5_sf_sw_to_hw_id(table->dev, sf->id);
	dl_port_index = mlx5_esw_vport_to_devlink_port_index(table->dev, hw_fn_id);
	sf->port_index = dl_port_index;

	err = mlx5_sf_id_insert(table, sf);
	if (err)
		goto insert_err;

	return sf;

insert_err:
	kfree(sf);
alloc_err:
	mlx5_sf_hw_table_sf_free(table->dev, id_err);
id_err:
	if (err == -EEXIST)
		NL_SET_ERR_MSG_MOD(extack, "SF already exist. Choose different sfnum");
	return ERR_PTR(err);
}

static void mlx5_sf_free(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	mlx5_sf_id_erase(table, sf);
	mlx5_sf_hw_table_sf_free(table->dev, sf->id);
	kfree(sf);
}

static struct mlx5_sf_table *mlx5_sf_table_try_get(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_table *table = dev->priv.sf_table;

	if (!table)
		return NULL;

	return refcount_inc_not_zero(&table->refcount) ? table : NULL;
}

static void mlx5_sf_table_put(struct mlx5_sf_table *table)
{
	if (refcount_dec_and_test(&table->refcount))
		complete(&table->disable_complete);
}

static int mlx5_sf_add(struct mlx5_core_dev *dev, struct mlx5_sf_table *table,
		       const struct devlink_port_new_attrs *new_attr,
		       struct netlink_ext_ack *extack,
		       unsigned int *new_port_index)
{
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_sf *sf;
	u16 hw_fn_id;
	int err;

	sf = mlx5_sf_alloc(table, new_attr->sfnum, extack);
	if (IS_ERR(sf))
		return PTR_ERR(sf);

	hw_fn_id = mlx5_sf_sw_to_hw_id(dev, sf->id);
	err = mlx5_esw_offloads_sf_vport_enable(esw, &sf->dl_port, hw_fn_id, new_attr->sfnum);
	if (err)
		goto esw_err;
	*new_port_index = sf->port_index;
	return 0;

esw_err:
	mlx5_sf_free(table, sf);
	return err;
}

static void mlx5_sf_del(struct mlx5_core_dev *dev, struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	u16 hw_fn_id;

	hw_fn_id = mlx5_sf_sw_to_hw_id(dev, sf->id);
	mlx5_esw_offloads_sf_vport_disable(esw, hw_fn_id);
	mlx5_sf_free(table, sf);
}

static int
mlx5_sf_new_check_attr(struct mlx5_core_dev *dev, const struct devlink_port_new_attrs *new_attr,
		       struct netlink_ext_ack *extack)
{
	if (new_attr->flavour != DEVLINK_PORT_FLAVOUR_PCI_SF) {
		NL_SET_ERR_MSG_MOD(extack, "Driver supports only SF port addition");
		return -EOPNOTSUPP;
	}
	if (new_attr->port_index_valid) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Driver does not support user defined port index assignment");
		return -EOPNOTSUPP;
	}
	if (!new_attr->sfnum_valid) {
		NL_SET_ERR_MSG_MOD(extack,
				   "User must provide unique sfnum. Driver does not support auto assignment");
		return -EOPNOTSUPP;
	}
	if (new_attr->controller_valid && new_attr->controller) {
		NL_SET_ERR_MSG_MOD(extack, "External controller is unsupported");
		return -EOPNOTSUPP;
	}
	if (new_attr->pfnum != PCI_FUNC(dev->pdev->devfn)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid pfnum supplied");
		return -EOPNOTSUPP;
	}
	return 0;
}

int mlx5_devlink_sf_port_new(struct devlink *devlink,
			     const struct devlink_port_new_attrs *new_attr,
			     struct netlink_ext_ack *extack,
			     unsigned int *new_port_index)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_sf_table *table;
	int err;

	err = mlx5_sf_new_check_attr(dev, new_attr, extack);
	if (err)
		return err;

	table = mlx5_sf_table_try_get(dev);
	if (!table) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port add is only supported in eswitch switchdev mode or SF ports are disabled.");
		return -EOPNOTSUPP;
	}
	err = mlx5_sf_add(dev, table, new_attr, extack, new_port_index);
	mlx5_sf_table_put(table);
	return err;
}

int mlx5_devlink_sf_port_del(struct devlink *devlink, unsigned int port_index,
			     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_sf_table *table;
	struct mlx5_sf *sf;
	int err = 0;

	table = mlx5_sf_table_try_get(dev);
	if (!table) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port del is only supported in eswitch switchdev mode or SF ports are disabled.");
		return -EOPNOTSUPP;
	}
	sf = mlx5_sf_lookup_by_index(table, port_index);
	if (!sf) {
		err = -ENODEV;
		goto sf_err;
	}

	mlx5_sf_del(dev, table, sf);
sf_err:
	mlx5_sf_table_put(table);
	return err;
}

static void mlx5_sf_destroy_all(struct mlx5_sf_table *table)
{
	struct mlx5_core_dev *dev = table->dev;
	unsigned long index;
	struct mlx5_sf *sf;

	xa_for_each(&table->port_indices, index, sf)
		mlx5_sf_del(dev, table, sf);
}

static void mlx5_sf_table_enable(struct mlx5_sf_table *table)
{
	if (!mlx5_sf_max_functions(table->dev))
		return;

	init_completion(&table->disable_complete);
	refcount_set(&table->refcount, 1);
}

static void mlx5_sf_table_disable(struct mlx5_sf_table *table)
{
	if (!mlx5_sf_max_functions(table->dev))
		return;

	if (!refcount_read(&table->refcount))
		return;

	/* Balances with refcount_set; drop the reference so that new user cmd cannot start. */
	mlx5_sf_table_put(table);
	wait_for_completion(&table->disable_complete);

	/* At this point, no new user commands can start.
	 * It is safe to destroy all user created SFs.
	 */
	mlx5_sf_destroy_all(table);
}

static int mlx5_sf_esw_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5_sf_table *table = container_of(nb, struct mlx5_sf_table, esw_nb);
	const struct mlx5_esw_event_info *mode = data;

	switch (mode->new_mode) {
	case MLX5_ESWITCH_OFFLOADS:
		mlx5_sf_table_enable(table);
		break;
	case MLX5_ESWITCH_NONE:
		mlx5_sf_table_disable(table);
		break;
	default:
		break;
	};

	return 0;
}

static bool mlx5_sf_table_supported(const struct mlx5_core_dev *dev)
{
	return dev->priv.eswitch && MLX5_ESWITCH_MANAGER(dev) && mlx5_sf_supported(dev);
}

int mlx5_sf_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_table *table;
	int err;

	if (!mlx5_sf_table_supported(dev))
		return 0;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->dev = dev;
	xa_init(&table->port_indices);
	dev->priv.sf_table = table;
	table->esw_nb.notifier_call = mlx5_sf_esw_event;
	err = mlx5_esw_event_notifier_register(dev->priv.eswitch, &table->esw_nb);
	if (err)
		goto reg_err;
	return 0;

reg_err:
	kfree(table);
	dev->priv.sf_table = NULL;
	return err;
}

void mlx5_sf_table_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_table *table = dev->priv.sf_table;

	if (!table)
		return;

	mlx5_esw_event_notifier_unregister(dev->priv.eswitch, &table->esw_nb);
	WARN_ON(refcount_read(&table->refcount));
	WARN_ON(!xa_empty(&table->port_indices));
	kfree(table);
}
