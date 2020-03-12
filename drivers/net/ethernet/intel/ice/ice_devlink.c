// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include "ice.h"
#include "ice_devlink.h"

static const struct devlink_ops ice_devlink_ops = {
};

static void ice_devlink_free(void *devlink_ptr)
{
	devlink_free((struct devlink *)devlink_ptr);
}

/**
 * ice_allocate_pf - Allocate devlink and return PF structure pointer
 * @dev: the device to allocate for
 *
 * Allocate a devlink instance for this device and return the private area as
 * the PF structure. The devlink memory is kept track of through devres by
 * adding an action to remove it when unwinding.
 */
struct ice_pf *ice_allocate_pf(struct device *dev)
{
	struct devlink *devlink;

	devlink = devlink_alloc(&ice_devlink_ops, sizeof(struct ice_pf));
	if (!devlink)
		return NULL;

	/* Add an action to teardown the devlink when unwinding the driver */
	if (devm_add_action(dev, ice_devlink_free, devlink)) {
		devlink_free(devlink);
		return NULL;
	}

	return devlink_priv(devlink);
}

/**
 * ice_devlink_register - Register devlink interface for this PF
 * @pf: the PF to register the devlink for.
 *
 * Register the devlink instance associated with this physical function.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_register(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	err = devlink_register(devlink, dev);
	if (err) {
		dev_err(dev, "devlink registration failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_unregister - Unregister devlink resources for this PF.
 * @pf: the PF structure to cleanup
 *
 * Releases resources used by devlink and cleans up associated memory.
 */
void ice_devlink_unregister(struct ice_pf *pf)
{
	devlink_unregister(priv_to_devlink(pf));
}

/**
 * ice_devlink_create_port - Create a devlink port for this PF
 * @pf: the PF to create a port for
 *
 * Create and register a devlink_port for this PF. Note that although each
 * physical function is connected to a separate devlink instance, the port
 * will still be numbered according to the physical function id.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_port(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	if (!vsi) {
		dev_err(dev, "%s: unable to find main VSI\n", __func__);
		return -EIO;
	}

	devlink_port_attrs_set(&pf->devlink_port, DEVLINK_PORT_FLAVOUR_PHYSICAL,
			       pf->hw.pf_id, false, 0, NULL, 0);
	err = devlink_port_register(devlink, &pf->devlink_port, pf->hw.pf_id);
	if (err) {
		dev_err(dev, "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 */
void ice_devlink_destroy_port(struct ice_pf *pf)
{
	devlink_port_type_clear(&pf->devlink_port);
	devlink_port_unregister(&pf->devlink_port);
}
