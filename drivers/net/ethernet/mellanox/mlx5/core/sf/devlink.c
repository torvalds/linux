// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#include <linux/mlx5/driver.h>
#include "eswitch.h"
#include "priv.h"
#include "sf/dev/dev.h"
#include "mlx5_ifc_vhca_event.h"
#include "vhca_event.h"
#include "ecpf.h"
#define CREATE_TRACE_POINTS
#include "diag/sf_tracepoint.h"

struct mlx5_sf {
	struct mlx5_devlink_port dl_port;
	unsigned int port_index;
	u32 controller;
	u16 id;
	u16 hw_fn_id;
	u16 hw_state;
};

static void *mlx5_sf_by_dl_port(struct devlink_port *dl_port)
{
	struct mlx5_devlink_port *mlx5_dl_port = mlx5_devlink_port_get(dl_port);

	return container_of(mlx5_dl_port, struct mlx5_sf, dl_port);
}

struct mlx5_sf_table {
	struct mlx5_core_dev *dev; /* To refer from notifier context. */
	struct xarray function_ids; /* function id based lookup. */
	struct mutex sf_state_lock; /* Serializes sf state among user cmds & vhca event handler. */
	struct notifier_block esw_nb;
	struct notifier_block vhca_nb;
	struct notifier_block mdev_nb;
};

static struct mlx5_sf *
mlx5_sf_lookup_by_function_id(struct mlx5_sf_table *table, unsigned int fn_id)
{
	return xa_load(&table->function_ids, fn_id);
}

static int mlx5_sf_function_id_insert(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	return xa_insert(&table->function_ids, sf->hw_fn_id, sf, GFP_KERNEL);
}

static void mlx5_sf_function_id_erase(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	xa_erase(&table->function_ids, sf->hw_fn_id);
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

	err = mlx5_sf_function_id_insert(table, sf);
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
	mlx5_sf_hw_table_sf_free(table->dev, sf->controller, sf->id);
	trace_mlx5_sf_free(table->dev, sf->port_index, sf->controller, sf->hw_fn_id);
	kfree(sf);
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
	struct mlx5_sf_table *table = dev->priv.sf_table;
	struct mlx5_sf *sf = mlx5_sf_by_dl_port(dl_port);

	mutex_lock(&table->sf_state_lock);
	*state = mlx5_sf_to_devlink_state(sf->hw_state);
	*opstate = mlx5_sf_to_devlink_opstate(sf->hw_state);
	mutex_unlock(&table->sf_state_lock);
	return 0;
}

static int mlx5_sf_activate(struct mlx5_core_dev *dev, struct mlx5_sf *sf,
			    struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport;
	int err;

	if (mlx5_sf_is_active(sf))
		return 0;
	if (sf->hw_state != MLX5_VHCA_STATE_ALLOCATED) {
		NL_SET_ERR_MSG_MOD(extack, "SF is inactivated but it is still attached");
		return -EBUSY;
	}

	vport = mlx5_devlink_port_vport_get(&sf->dl_port.dl_port);
	if (!vport->max_eqs_set && MLX5_CAP_GEN_2(dev, max_num_eqs_24b)) {
		err = mlx5_devlink_port_fn_max_io_eqs_set_sf_default(&sf->dl_port.dl_port,
								     extack);
		if (err)
			return err;
	}
	err = mlx5_cmd_sf_enable_hca(dev, sf->hw_fn_id);
	if (err)
		return err;

	sf->hw_state = MLX5_VHCA_STATE_ACTIVE;
	trace_mlx5_sf_activate(dev, sf->port_index, sf->controller, sf->hw_fn_id);
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
	trace_mlx5_sf_deactivate(dev, sf->port_index, sf->controller, sf->hw_fn_id);
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
	struct mlx5_sf_table *table = dev->priv.sf_table;
	struct mlx5_sf *sf = mlx5_sf_by_dl_port(dl_port);

	return mlx5_sf_state_set(dev, table, sf, state, extack);
}

static int mlx5_sf_add(struct mlx5_core_dev *dev, struct mlx5_sf_table *table,
		       const struct devlink_port_new_attrs *new_attr,
		       struct netlink_ext_ack *extack,
		       struct devlink_port **dl_port)
{
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_sf *sf;
	int err;

	sf = mlx5_sf_alloc(table, esw, new_attr->controller, new_attr->sfnum, extack);
	if (IS_ERR(sf))
		return PTR_ERR(sf);

	err = mlx5_eswitch_load_sf_vport(esw, sf->hw_fn_id, MLX5_VPORT_UC_ADDR_CHANGE,
					 &sf->dl_port, new_attr->controller, new_attr->sfnum);
	if (err)
		goto esw_err;
	*dl_port = &sf->dl_port.dl_port;
	trace_mlx5_sf_add(dev, sf->port_index, sf->controller, sf->hw_fn_id, new_attr->sfnum);
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

static bool mlx5_sf_table_supported(const struct mlx5_core_dev *dev)
{
	return dev->priv.eswitch && MLX5_ESWITCH_MANAGER(dev) &&
	       mlx5_sf_hw_table_supported(dev);
}

int mlx5_devlink_sf_port_new(struct devlink *devlink,
			     const struct devlink_port_new_attrs *new_attr,
			     struct netlink_ext_ack *extack,
			     struct devlink_port **dl_port)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_sf_table *table = dev->priv.sf_table;
	int err;

	err = mlx5_sf_new_check_attr(dev, new_attr, extack);
	if (err)
		return err;

	if (!mlx5_sf_table_supported(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "SF ports are not supported.");
		return -EOPNOTSUPP;
	}

	if (!is_mdev_switchdev_mode(dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "SF ports are only supported in eswitch switchdev mode.");
		return -EOPNOTSUPP;
	}

	return mlx5_sf_add(dev, table, new_attr, extack, dl_port);
}

static void mlx5_sf_dealloc(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	struct mlx5_vport *vport;

	mutex_lock(&table->sf_state_lock);
	vport = mlx5_devlink_port_vport_get(&sf->dl_port.dl_port);
	vport->max_eqs_set = false;

	mlx5_sf_function_id_erase(table, sf);

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

	mutex_unlock(&table->sf_state_lock);
}

static void mlx5_sf_del(struct mlx5_sf_table *table, struct mlx5_sf *sf)
{
	struct mlx5_eswitch *esw = table->dev->priv.eswitch;

	mlx5_eswitch_unload_sf_vport(esw, sf->hw_fn_id);
	mlx5_sf_dealloc(table, sf);
}

int mlx5_devlink_sf_port_del(struct devlink *devlink,
			     struct devlink_port *dl_port,
			     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_sf_table *table = dev->priv.sf_table;
	struct mlx5_sf *sf = mlx5_sf_by_dl_port(dl_port);

	mlx5_sf_del(table, sf);
	return 0;
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

	mutex_lock(&table->sf_state_lock);
	sf = mlx5_sf_lookup_by_function_id(table, event->function_id);
	if (!sf)
		goto unlock;

	/* When driver is attached or detached to a function, an event
	 * notifies such state change.
	 */
	update = mlx5_sf_state_update_check(sf, event->new_vhca_state);
	if (update)
		sf->hw_state = event->new_vhca_state;
	trace_mlx5_sf_update_state(table->dev, sf->port_index, sf->controller,
				   sf->hw_fn_id, sf->hw_state);
unlock:
	mutex_unlock(&table->sf_state_lock);
	return 0;
}

static void mlx5_sf_del_all(struct mlx5_sf_table *table)
{
	unsigned long index;
	struct mlx5_sf *sf;

	xa_for_each(&table->function_ids, index, sf)
		mlx5_sf_del(table, sf);
}

static int mlx5_sf_esw_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5_sf_table *table = container_of(nb, struct mlx5_sf_table, esw_nb);
	const struct mlx5_esw_event_info *mode = data;

	switch (mode->new_mode) {
	case MLX5_ESWITCH_LEGACY:
		mlx5_sf_del_all(table);
		break;
	default:
		break;
	}

	return 0;
}

static int mlx5_sf_mdev_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5_sf_table *table = container_of(nb, struct mlx5_sf_table, mdev_nb);
	struct mlx5_sf_peer_devlink_event_ctx *event_ctx = data;
	int ret = NOTIFY_DONE;
	struct mlx5_sf *sf;

	if (event != MLX5_DRIVER_EVENT_SF_PEER_DEVLINK)
		return NOTIFY_DONE;


	mutex_lock(&table->sf_state_lock);
	sf = mlx5_sf_lookup_by_function_id(table, event_ctx->fn_id);
	if (!sf)
		goto out;

	event_ctx->err = devl_port_fn_devlink_set(&sf->dl_port.dl_port,
						  event_ctx->devlink);

	ret = NOTIFY_OK;
out:
	mutex_unlock(&table->sf_state_lock);
	return ret;
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
	xa_init(&table->function_ids);
	dev->priv.sf_table = table;
	table->esw_nb.notifier_call = mlx5_sf_esw_event;
	err = mlx5_esw_event_notifier_register(dev->priv.eswitch, &table->esw_nb);
	if (err)
		goto reg_err;

	table->vhca_nb.notifier_call = mlx5_sf_vhca_event;
	err = mlx5_vhca_event_notifier_register(table->dev, &table->vhca_nb);
	if (err)
		goto vhca_err;

	table->mdev_nb.notifier_call = mlx5_sf_mdev_event;
	mlx5_blocking_notifier_register(dev, &table->mdev_nb);

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

	mlx5_blocking_notifier_unregister(dev, &table->mdev_nb);
	mlx5_vhca_event_notifier_unregister(table->dev, &table->vhca_nb);
	mlx5_esw_event_notifier_unregister(dev->priv.eswitch, &table->esw_nb);
	mutex_destroy(&table->sf_state_lock);
	WARN_ON(!xa_empty(&table->function_ids));
	kfree(table);
}
