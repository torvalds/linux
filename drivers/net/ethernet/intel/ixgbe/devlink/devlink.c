// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ixgbe.h"
#include "devlink.h"

static const struct devlink_ops ixgbe_devlink_ops = {
};

/**
 * ixgbe_allocate_devlink - Allocate devlink instance
 * @dev: device to allocate devlink for
 *
 * Allocate a devlink instance for this physical function.
 *
 * Return: pointer to the device adapter structure on success,
 * ERR_PTR(-ENOMEM) when allocation failed.
 */
struct ixgbe_adapter *ixgbe_allocate_devlink(struct device *dev)
{
	struct ixgbe_adapter *adapter;
	struct devlink *devlink;

	devlink = devlink_alloc(&ixgbe_devlink_ops, sizeof(*adapter), dev);
	if (!devlink)
		return ERR_PTR(-ENOMEM);

	adapter = devlink_priv(devlink);
	adapter->devlink = devlink;

	return adapter;
}

/**
 * ixgbe_devlink_set_switch_id - Set unique switch ID based on PCI DSN
 * @adapter: pointer to the device adapter structure
 * @ppid: struct with switch id information
 */
static void ixgbe_devlink_set_switch_id(struct ixgbe_adapter *adapter,
					struct netdev_phys_item_id *ppid)
{
	u64 id = pci_get_dsn(adapter->pdev);

	ppid->id_len = sizeof(id);
	put_unaligned_be64(id, &ppid->id);
}

/**
 * ixgbe_devlink_register_port - Register devlink port
 * @adapter: pointer to the device adapter structure
 *
 * Create and register a devlink_port for this physical function.
 *
 * Return: 0 on success, error code on failure.
 */
int ixgbe_devlink_register_port(struct ixgbe_adapter *adapter)
{
	struct devlink_port *devlink_port = &adapter->devlink_port;
	struct devlink *devlink = adapter->devlink;
	struct device *dev = &adapter->pdev->dev;
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = adapter->hw.bus.func;
	ixgbe_devlink_set_switch_id(adapter, &attrs.switch_id);

	devlink_port_attrs_set(devlink_port, &attrs);

	err = devl_port_register(devlink, devlink_port, 0);
	if (err) {
		dev_err(dev,
			"devlink port registration failed, err %d\n", err);
	}

	return err;
}
