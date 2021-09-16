// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#include <linux/mlx5/driver.h>
#include "eswitch.h"
#include "priv.h"
#include "sf/dev/dev.h"
#include "mlx5_ifc_vhca_event.h"
#include "vhca_event.h"
#include "ecpf.h"

struct mlx5_sf {
	struct devlink_port dl_port;
	unsigned int port_index;
	u32 controller;
	u16 id;
	u16 hw_fn_id;
	u16 hw_state;
};

struct mlx5_sf_table {
	struct mlx5_core_dev *dev; /* To refer from notifier context. */
	struct xarray port_indices; /* port index based lookup. */
	refcount_t refcount;
	struct completion disable_complete;
	struct mutex sf_state_lock; /* Serializes sf state among user cmds & vhca event handler. */
	struct notifier_block esw_nb;
	struct notifier_block vhca_nb;
	u8 ecpu: 1;
};

static struct mlx5_sf *
mlx5_sf_lookup_by_index(struct mlx5_sf_table *table, unsigned int port_index)
{
	return xa_load(&table->port_indices, port_index);
}

static struct mlx5_sf *
mlx5_sf_lookup_by_function_id(struct mlx5_sf_table *table, unsigned int fn_id)
{
	unsigned long index;
	struct mlx5_sf *sf;

	xa_for_each(&table->port_indices, index, sf) {
		if (sf->hw_fn_id == fn_id)
			return sf;
	}
	return NULL;
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
mlx5_sf_alloc(struct mlx5_sf_table *table, struct mlx5_eswitch *esw,
	      u32 controller, u32 sfnum, struct netlink_ext_ack *extack)
{
	unsigned int dl_port_index;
	struct mlx5_sf *sf;
	u16 hw_fn_id;
	int id_err;
	int err;

	if (!mlx5_esw_offloads_controller_valid(esw, controller)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid controller number");
		return ERR_PTR(-EINVAL);
	}

	id_err = mlx5_sf_hw_table_sf_alloc(table->dev, controller, sfnum);
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
	hw_fn_id = mlx5_sf_sw_to_hw_id(table->dev, controller, sf->id);
	dl_port_index = mlx5_esw_vport_to_devlink_port_index(table->dev, hw_fn_id);
	sf->port_index = dl_port_index;
	sf->hw_fn_id = hw_fn_id;
	sf->hw_state = MLX5_VHCA_STATE_ALLOCATED;
	sf->controller = controller;

	err = mlx5_sf_id_insert(table, sf);
	if (err)
		goto insert_err;

	return sf;

insert_err:
	kfree(sf);
alloc_err:
	mlx5_sf_hw_table_sf_free(table->dev, controller, id_err);
id_err:
	if (err == -EEXIST)
		NL_SET_ERR_MSG_MOD(extack, "SF already exist. Choose different sfnum");
	return ERR_PTR(err);
}

static void mlx5_sf_free(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	mlx5_sf_id_erase(table, sf);
	mlx5_sf_hw_table_sf_free(table->dev, sf->controller, sf->id);
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

static enum devlink_port_fn_state mlx5_sf_to_devlink_state(u8 hw_state)
{
	switch (hw_state) {
	case MLX5_VHCA_STATE_ACTIVE:
	case MLX5_VHCA_STATE_IN_USE:
		return DEVLINK_PORT_FN_STATE_ACTIVE;
	case MLX5_VHCA_STATE_INVALID:
	case MLX5_VHCA_STATE_ALLOCATED:
	case MLX5_VHCA_STATE_TEARDOWN_REQUEST:
	default:
		return DEVLINK_PORT_FN_STATE_INACTIVE;
	}
}

static enum devlink_port_fn_opstate mlx5_sf_to_devlink_opstate(u8 hw_state)
{
	switch (hw_state) {
	case MLX5_VHCA_STATE_IN_USE:
	case MLX5_VHCA_STATE_TEARDOWN_REQUEST:
		return DEVLINK_PORT_FN_OPSTATE_ATTACHED;
	case MLX5_VHCA_STATE_INVALID:
	case MLX5_VHCA_STATE_ALLOCATED:
	case MLX5_VHCA_STATE_ACTIVE:
	default:
		return DEVLINK_PORT_FN_OPSTATE_DETACHED;
	}
}

static bool mlx5_sf_is_active(const struct mlx5_sf *sf)
{
	return sf->hw_state == MLX5_VHCA_STATE_ACTIVE || sf->hw_state == MLX5_VHCA_STATE_IN_USE;
}

int mlx5_devlink_sf_port_fn_state_get(struct devlink_port *dl_port,
				      enum devlink_port_fn_state *state,
				      enum devlink_port_fn_opstate *opstate,
				      struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(dl_port->devlink);
	struct mlx5_sf_table *table;
	struct mlx5_sf *sf;
	int err = 0;

	table = mlx5_sf_table_try_get(dev);
	if (!table)
		return -EOPNOTSUPP;

	sf = mlx5_sf_lookup_by_index(table, dl_port->index);
	if (!sf) {
		err = -EOPNOTSUPP;
		goto sf_err;
	}
	mutex_lock(&table->sf_state_lock);
	*state = mlx5_sf_to_devlink_state(sf->hw_state);
	*opstate = mlx5_sf_to_devlink_opstate(sf->hw_state);
	mutex_unlock(&table->sf_state_lock);
sf_err:
	mlx5_sf_table_put(table);
	return err;
}

static int mlx5_sf_activate(struct mlx5_core_dev *dev, struct mlx5_sf *sf,
			    struct netlink_ext_ack *extack)
{
	int err;

	if (mlx5_sf_is_active(sf))
		return 0;
	if (sf->hw_state != MLX5_VHCA_STATE_ALLOCATED) {
		NL_SET_ERR_MSG_MOD(extack, "SF is inactivated but it is still attached");
		return -EBUSY;
	}

	err = mlx5_cmd_sf_enable_hca(dev, sf->hw_fn_id);
	if (err)
		return err;

	sf->hw_state = MLX5_VHCA_STATE_ACTIVE;
	return 0;
}

static int mlx5_sf_deactivate(struct mlx5_core_dev *dev, struct mlx5_sf *sf)
{
	int err;

	if (!mlx5_sf_is_active(sf))
		return 0;

	err = mlx5_cmd_sf_disable_hca(dev, sf->hw_fn_id);
	if (err)
		return err;

	sf->hw_state = MLX5_VHCA_STATE_TEARDOWN_REQUEST;
	return 0;
}

static int mlx5_sf_state_set(struct mlx5_core_dev *dev, struct mlx5_sf_table *table,
			     struct mlx5_sf *sf,
			     enum devlink_port_fn_state state,
			     struct netlink_ext_ack *extack)
{
	int err = 0;

	mutex_lock(&table->sf_state_lock);
	if (state == mlx5_sf_to_devlink_state(sf->hw_state))
		goto out;
	if (state == DEVLINK_PORT_FN_STATE_ACTIVE)
		err = mlx5_sf_activate(dev, sf, extack);
	else if (state == DEVLINK_PORT_FN_STATE_INACTIVE)
		err = mlx5_sf_deactivate(dev, sf);
	else
		err = -EINVAL;
out:
	mutex_unlock(&table->sf_state_lock);
	return err;
}

int mlx5_devlink_sf_port_fn_state_set(struct devlink_port *dl_port,
				      enum devlink_port_fn_state state,
				      struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(dl_port->devlink);
	struct mlx5_sf_table *table;
	struct mlx5_sf *sf;
	int err;

	table = mlx5_sf_table_try_get(dev);
	if (!table) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port state set is only supported in eswitch switchdev mode or SF ports are disabled.");
		return -EOPNOTSUPP;
	}
	sf = mlx5_sf_lookup_by_index(table, dl_port->index);
	if (!sf) {
		err = -ENODEV;
		goto out;
	}

	err = mlx5_sf_state_set(dev, table, sf, state, extack);
out:
	mlx5_sf_table_put(table);
	return err;
}

static int mlx5_sf_add(struct mlx5_core_dev *dev, struct mlx5_sf_table *table,
		       const struct devlink_port_new_attrs *new_attr,
		       struct netlink_ext_ack *extack,
		       unsigned int *new_port_index)
{
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_sf *sf;
	int err;

	sf = mlx5_sf_alloc(table, esw, new_attr->controller, new_attr->sfnum, extack);
	if (IS_ERR(sf))
		return PTR_ERR(sf);

	err = mlx5_esw_offloads_sf_vport_enable(esw, &sf->dl_port, sf->hw_fn_id,
						new_attr->controller, new_attr->sfnum);
	if (err)
		goto esw_err;
	*new_port_index = sf->port_index;
	return 0;

esw_err:
	mlx5_sf_free(table, sf);
	return err;
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
	if (new_attr->controller_valid && new_attr->controller &&
	    !mlx5_core_is_ecpf_esw_manager(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "External controller is unsupported");
		return -EOPNOTSUPP;
	}
	if (new_attr->pfnum != mlx5_get_dev_index(dev)) {
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

static void mlx5_sf_dealloc(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	if (sf->hw_state == MLX5_VHCA_STATE_ALLOCATED) {
		mlx5_sf_free(table, sf);
	} else if (mlx5_sf_is_active(sf)) {
		/* Even if its active, it is treated as in_use because by the time,
		 * it is disabled here, it may getting used. So it is safe to
		 * always look for the event to ensure that it is recycled only after
		 * firmware gives confirmation that it is detached by the driver.
		 */
		mlx5_cmd_sf_disable_hca(table->dev, sf->hw_fn_id);
		mlx5_sf_hw_table_sf_deferred_free(table->dev, sf->controller, sf->id);
		kfree(sf);
	} else {
		mlx5_sf_hw_table_sf_deferred_free(table->dev, sf->controller, sf->id);
		kfree(sf);
	}
}

int mlx5_devlink_sf_port_del(struct devlink *devlink, unsigned int port_index,
			     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
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

	mlx5_esw_offloads_sf_vport_disable(esw, sf->hw_fn_id);
	mlx5_sf_id_erase(table, sf);

	mutex_lock(&table->sf_state_lock);
	mlx5_sf_dealloc(table, sf);
	mutex_unlock(&table->sf_state_lock);
sf_err:
	mlx5_sf_table_put(table);
	return err;
}

static bool mlx5_sf_state_update_check(const struct mlx5_sf *sf, u8 new_state)
{
	if (sf->hw_state == MLX5_VHCA_STATE_ACTIVE && new_state == MLX5_VHCA_STATE_IN_USE)
		return true;

	if (sf->hw_state == MLX5_VHCA_STATE_IN_USE && new_state == MLX5_VHCA_STATE_ACTIVE)
		return true;

	if (sf->hw_state == MLX5_VHCA_STATE_TEARDOWN_REQUEST &&
	    new_state == MLX5_VHCA_STATE_ALLOCATED)
		return true;

	return false;
}

static int mlx5_sf_vhca_event(struct notifier_block *nb, unsigned long opcode, void *data)
{
	struct mlx5_sf_table *table = container_of(nb, struct mlx5_sf_table, vhca_nb);
	const struct mlx5_vhca_state_event *event = data;
	bool update = false;
	struct mlx5_sf *sf;

	table = mlx5_sf_table_try_get(table->dev);
	if (!table)
		return 0;

	mutex_lock(&table->sf_state_lock);
	sf = mlx5_sf_lookup_by_function_id(table, event->function_id);
	if (!sf)
		goto sf_err;

	/* When driver is attached or detached to a function, an event
	 * notifies such state change.
	 */
	update = mlx5_sf_state_update_check(sf, event->new_vhca_state);
	if (update)
		sf->hw_state = event->new_vhca_state;
sf_err:
	mutex_unlock(&table->sf_state_lock);
	mlx5_sf_table_put(table);
	return 0;
}

static void mlx5_sf_table_enable(struct mlx5_sf_table *table)
{
	init_completion(&table->disable_complete);
	refcount_set(&table->refcount, 1);
}

static void mlx5_sf_deactivate_all(struct mlx5_sf_table *table)
{
	struct mlx5_eswitch *esw = table->dev->priv.eswitch;
	unsigned long index;
	struct mlx5_sf *sf;

	/* At this point, no new user commands can start and no vhca event can
	 * arrive. It is safe to destroy all user created SFs.
	 */
	xa_for_each(&table->port_indices, index, sf) {
		mlx5_esw_offloads_sf_vport_disable(esw, sf->hw_fn_id);
		mlx5_sf_id_erase(table, sf);
		mlx5_sf_dealloc(table, sf);
	}
}

static void mlx5_sf_table_disable(struct mlx5_sf_table *table)
{
	if (!refcount_read(&table->refcount))
		return;

	/* Balances with refcount_set; drop the reference so that new user cmd cannot start
	 * and new vhca event handler cannot run.
	 */
	mlx5_sf_table_put(table);
	wait_for_completion(&table->disable_complete);

	mlx5_sf_deactivate_all(table);
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
	}

	return 0;
}

static bool mlx5_sf_table_supported(const struct mlx5_core_dev *dev)
{
	return dev->priv.eswitch && MLX5_ESWITCH_MANAGER(dev) &&
	       mlx5_sf_hw_table_supported(dev);
}

int mlx5_sf_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_table *table;
	int err;

	if (!mlx5_sf_table_supported(dev) || !mlx5_vhca_event_supported(dev))
		return 0;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	mutex_init(&table->sf_state_lock);
	table->dev = dev;
	xa_init(&table->port_indices);
	dev->priv.sf_table = table;
	refcount_set(&table->refcount, 0);
	table->esw_nb.notifier_call = mlx5_sf_esw_event;
	err = mlx5_esw_event_notifier_register(dev->priv.eswitch, &table->esw_nb);
	if (err)
		goto reg_err;

	table->vhca_nb.notifier_call = mlx5_sf_vhca_event;
	err = mlx5_vhca_event_notifier_register(table->dev, &table->vhca_nb);
	if (err)
		goto vhca_err;

	return 0;

vhca_err:
	mlx5_esw_event_notifier_unregister(dev->priv.eswitch, &table->esw_nb);
reg_err:
	mutex_destroy(&table->sf_state_lock);
	kfree(table);
	dev->priv.sf_table = NULL;
	return err;
}

void mlx5_sf_table_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_table *table = dev->priv.sf_table;

	if (!table)
		return;

	mlx5_vhca_event_notifier_unregister(table->dev, &table->vhca_nb);
	mlx5_esw_event_notifier_unregister(dev->priv.eswitch, &table->esw_nb);
	WARN_ON(refcount_read(&table->refcount));
	mutex_destroy(&table->sf_state_lock);
	WARN_ON(!xa_empty(&table->port_indices));
	kfree(table);
}
