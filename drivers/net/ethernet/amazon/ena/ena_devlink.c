// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#include "linux/pci.h"
#include "ena_devlink.h"
#include "ena_phc.h"

static int ena_devlink_enable_phc_validate(struct devlink *devlink, u32 id,
					   union devlink_param_value val,
					   struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);

	if (!val.vbool)
		return 0;

	if (!ena_com_phc_supported(adapter->ena_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Device doesn't support PHC");
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct devlink_param ena_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_PHC,
			      BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL,
			      NULL,
			      ena_devlink_enable_phc_validate),
};

void ena_devlink_params_get(struct devlink *devlink)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	union devlink_param_value val;
	int err;

	err = devl_param_driverinit_value_get(devlink,
					      DEVLINK_PARAM_GENERIC_ID_ENABLE_PHC,
					      &val);
	if (err) {
		netdev_err(adapter->netdev, "Failed to query PHC param\n");
		return;
	}

	ena_phc_enable(adapter, val.vbool);
}

void ena_devlink_disable_phc_param(struct devlink *devlink)
{
	union devlink_param_value value;

	value.vbool = false;
	devl_param_driverinit_value_set(devlink,
					DEVLINK_PARAM_GENERIC_ID_ENABLE_PHC,
					value);
}

static void ena_devlink_port_register(struct devlink *devlink)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	struct devlink_port_attrs attrs = {};

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	devlink_port_attrs_set(&adapter->devlink_port, &attrs);
	devl_port_register(devlink, &adapter->devlink_port, 0);
}

static void ena_devlink_port_unregister(struct devlink *devlink)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);

	devl_port_unregister(&adapter->devlink_port);
}

static int ena_devlink_reload_down(struct devlink *devlink,
				   bool netns_change,
				   enum devlink_reload_action action,
				   enum devlink_reload_limit limit,
				   struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);

	if (netns_change) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Namespace change is not supported");
		return -EOPNOTSUPP;
	}

	ena_devlink_port_unregister(devlink);

	rtnl_lock();
	ena_destroy_device(adapter, false);
	rtnl_unlock();

	return 0;
}

static int ena_devlink_reload_up(struct devlink *devlink,
				 enum devlink_reload_action action,
				 enum devlink_reload_limit limit,
				 u32 *actions_performed,
				 struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	int err = 0;

	rtnl_lock();
	/* Check that no other routine initialized the device (e.g.
	 * ena_fw_reset_device()). Also we're under devlink_mutex here,
	 * so devlink isn't freed under our feet.
	 */
	if (!test_bit(ENA_FLAG_DEVICE_RUNNING, &adapter->flags))
		err = ena_restore_device(adapter);

	rtnl_unlock();

	ena_devlink_port_register(devlink);

	if (!err)
		*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);

	return err;
}

static const struct devlink_ops ena_devlink_ops = {
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down	= ena_devlink_reload_down,
	.reload_up	= ena_devlink_reload_up,
};

static int ena_devlink_configure_params(struct devlink *devlink)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	union devlink_param_value value;
	int rc;

	rc = devlink_params_register(devlink, ena_devlink_params,
				     ARRAY_SIZE(ena_devlink_params));
	if (rc) {
		netdev_err(adapter->netdev, "Failed to register devlink params\n");
		return rc;
	}

	value.vbool = ena_phc_is_enabled(adapter);
	devl_param_driverinit_value_set(devlink,
					DEVLINK_PARAM_GENERIC_ID_ENABLE_PHC,
					value);

	return 0;
}

struct devlink *ena_devlink_alloc(struct ena_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	struct devlink *devlink;

	devlink = devlink_alloc(&ena_devlink_ops,
				sizeof(struct ena_adapter *),
				dev);
	if (!devlink) {
		netdev_err(adapter->netdev,
			   "Failed to allocate devlink struct\n");
		return NULL;
	}

	ENA_DEVLINK_PRIV(devlink) = adapter;
	adapter->devlink = devlink;

	if (ena_devlink_configure_params(devlink))
		goto free_devlink;

	return devlink;

free_devlink:
	devlink_free(devlink);
	return NULL;
}

static void ena_devlink_configure_params_clean(struct devlink *devlink)
{
	devlink_params_unregister(devlink, ena_devlink_params,
				  ARRAY_SIZE(ena_devlink_params));
}

void ena_devlink_free(struct devlink *devlink)
{
	ena_devlink_configure_params_clean(devlink);

	devlink_free(devlink);
}

void ena_devlink_register(struct devlink *devlink, struct device *dev)
{
	devl_lock(devlink);
	ena_devlink_port_register(devlink);
	devl_register(devlink);
	devl_unlock(devlink);
}

void ena_devlink_unregister(struct devlink *devlink)
{
	devl_lock(devlink);
	ena_devlink_port_unregister(devlink);
	devl_unregister(devlink);
	devl_unlock(devlink);
}
