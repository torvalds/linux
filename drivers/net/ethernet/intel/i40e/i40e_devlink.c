// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Intel Corporation. */

#include <net/devlink.h>
#include "i40e.h"
#include "i40e_devlink.h"

static const struct devlink_ops i40e_devlink_ops = {
};

/**
 * i40e_alloc_pf - Allocate devlink and return i40e_pf structure pointer
 * @dev: the device to allocate for
 *
 * Allocate a devlink instance for this device and return the private
 * area as the i40e_pf structure.
 **/
struct i40e_pf *i40e_alloc_pf(struct device *dev)
{
	struct devlink *devlink;

	devlink = devlink_alloc(&i40e_devlink_ops, sizeof(struct i40e_pf), dev);
	if (!devlink)
		return NULL;

	return devlink_priv(devlink);
}

/**
 * i40e_free_pf - Free i40e_pf structure and associated devlink
 * @pf: the PF structure
 *
 * Free i40e_pf structure and devlink allocated by devlink_alloc.
 **/
void i40e_free_pf(struct i40e_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);

	devlink_free(devlink);
}

/**
 * i40e_devlink_register - Register devlink interface for this PF
 * @pf: the PF to register the devlink for.
 *
 * Register the devlink instance associated with this physical function.
 **/
void i40e_devlink_register(struct i40e_pf *pf)
{
	devlink_register(priv_to_devlink(pf));
}

/**
 * i40e_devlink_unregister - Unregister devlink resources for this PF.
 * @pf: the PF structure to cleanup
 *
 * Releases resources used by devlink and cleans up associated memory.
 **/
void i40e_devlink_unregister(struct i40e_pf *pf)
{
	devlink_unregister(priv_to_devlink(pf));
}

/**
 * i40e_devlink_set_switch_id - Set unique switch id based on pci dsn
 * @pf: the PF to create a devlink port for
 * @ppid: struct with switch id information
 */
static void i40e_devlink_set_switch_id(struct i40e_pf *pf,
				       struct netdev_phys_item_id *ppid)
{
	u64 id = pci_get_dsn(pf->pdev);

	ppid->id_len = sizeof(id);
	put_unaligned_be64(id, &ppid->id);
}

/**
 * i40e_devlink_create_port - Create a devlink port for this PF
 * @pf: the PF to create a port for
 *
 * Create and register a devlink_port for this PF. Note that although each
 * physical function is connected to a separate devlink instance, the port
 * will still be numbered according to the physical function id.
 *
 * Return: zero on success or an error code on failure.
 **/
int i40e_devlink_create_port(struct i40e_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct devlink_port_attrs attrs = {};
	struct device *dev = &pf->pdev->dev;
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = pf->hw.pf_id;
	i40e_devlink_set_switch_id(pf, &attrs.switch_id);
	devlink_port_attrs_set(&pf->devlink_port, &attrs);
	err = devlink_port_register(devlink, &pf->devlink_port, pf->hw.pf_id);
	if (err) {
		dev_err(dev, "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * i40e_devlink_destroy_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 **/
void i40e_devlink_destroy_port(struct i40e_pf *pf)
{
	devlink_port_type_clear(&pf->devlink_port);
	devlink_port_unregister(&pf->devlink_port);
}
