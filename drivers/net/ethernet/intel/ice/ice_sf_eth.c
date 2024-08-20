// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Intel Corporation. */
#include "ice.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice_sf_eth.h"
#include "devlink/devlink_port.h"
#include "devlink/devlink.h"

/**
 * ice_sf_dev_probe - subfunction driver probe function
 * @adev: pointer to the auxiliary device
 * @id: pointer to the auxiliary_device id
 *
 * Configure VSI and netdev resources for the subfunction device.
 *
 * Return: zero on success or an error code on failure.
 */
static int ice_sf_dev_probe(struct auxiliary_device *adev,
			    const struct auxiliary_device_id *id)
{
	struct ice_sf_dev *sf_dev = ice_adev_to_sf_dev(adev);
	struct ice_dynamic_port *dyn_port = sf_dev->dyn_port;
	struct ice_vsi *vsi = dyn_port->vsi;
	struct ice_pf *pf = dyn_port->pf;
	struct device *dev = &adev->dev;
	struct ice_sf_priv *priv;
	struct devlink *devlink;
	int err;

	vsi->type = ICE_VSI_SF;
	vsi->port_info = pf->hw.port_info;
	vsi->flags = ICE_VSI_FLAG_INIT;

	priv = ice_allocate_sf(&adev->dev, pf);
	if (!priv) {
		dev_err(dev, "Subfunction devlink alloc failed");
		return -ENOMEM;
	}

	priv->dev = sf_dev;
	sf_dev->priv = priv;
	devlink = priv_to_devlink(priv);

	devl_lock(devlink);

	err = ice_vsi_cfg(vsi);
	if (err) {
		dev_err(dev, "Subfunction vsi config failed");
		goto err_free_devlink;
	}
	vsi->sf = dyn_port;

	err = ice_devlink_create_sf_dev_port(sf_dev);
	if (err) {
		dev_err(dev, "Cannot add ice virtual devlink port for subfunction");
		goto err_vsi_decfg;
	}

	err = devl_port_fn_devlink_set(&dyn_port->devlink_port, devlink);
	if (err) {
		dev_err(dev, "Can't link devlink instance to SF devlink port");
		goto err_devlink_destroy;
	}

	ice_napi_add(vsi);

	devl_register(devlink);
	devl_unlock(devlink);

	return 0;

err_devlink_destroy:
	ice_devlink_destroy_sf_dev_port(sf_dev);
err_vsi_decfg:
	ice_vsi_decfg(vsi);
err_free_devlink:
	devl_unlock(devlink);
	devlink_free(devlink);
	return err;
}

/**
 * ice_sf_dev_remove - subfunction driver remove function
 * @adev: pointer to the auxiliary device
 *
 * Deinitalize VSI and netdev resources for the subfunction device.
 */
static void ice_sf_dev_remove(struct auxiliary_device *adev)
{
	struct ice_sf_dev *sf_dev = ice_adev_to_sf_dev(adev);
	struct ice_dynamic_port *dyn_port = sf_dev->dyn_port;
	struct ice_vsi *vsi = dyn_port->vsi;
	struct devlink *devlink;

	devlink = priv_to_devlink(sf_dev->priv);
	devl_lock(devlink);

	ice_vsi_close(vsi);

	ice_devlink_destroy_sf_dev_port(sf_dev);
	devl_unregister(devlink);
	devl_unlock(devlink);
	devlink_free(devlink);
	ice_vsi_decfg(vsi);
}

static const struct auxiliary_device_id ice_sf_dev_id_table[] = {
	{ .name = "ice.sf", },
	{ },
};

MODULE_DEVICE_TABLE(auxiliary, ice_sf_dev_id_table);

static struct auxiliary_driver ice_sf_driver = {
	.name = "sf",
	.probe = ice_sf_dev_probe,
	.remove = ice_sf_dev_remove,
	.id_table = ice_sf_dev_id_table
};

/**
 * ice_sf_driver_register - Register new auxiliary subfunction driver
 *
 * Return: zero on success or an error code on failure.
 */
int ice_sf_driver_register(void)
{
	return auxiliary_driver_register(&ice_sf_driver);
}

/**
 * ice_sf_driver_unregister - Unregister new auxiliary subfunction driver
 *
 */
void ice_sf_driver_unregister(void)
{
	auxiliary_driver_unregister(&ice_sf_driver);
}
