// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Intel Corporation. */
#include "ice.h"
#include "ice_lib.h"
#include "ice_txrx.h"
#include "ice_fltr.h"
#include "ice_sf_eth.h"
#include "devlink/devlink.h"
#include "devlink/port.h"

static const struct net_device_ops ice_sf_netdev_ops = {
	.ndo_open = ice_open,
	.ndo_stop = ice_stop,
	.ndo_start_xmit = ice_start_xmit,
	.ndo_vlan_rx_add_vid = ice_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = ice_vlan_rx_kill_vid,
	.ndo_change_mtu = ice_change_mtu,
	.ndo_get_stats64 = ice_get_stats64,
	.ndo_tx_timeout = ice_tx_timeout,
	.ndo_bpf = ice_xdp,
	.ndo_xdp_xmit = ice_xdp_xmit,
	.ndo_xsk_wakeup = ice_xsk_wakeup,
};

/**
 * ice_sf_cfg_netdev - Allocate, configure and register a netdev
 * @dyn_port: subfunction associated with configured netdev
 * @devlink_port: subfunction devlink port to be linked with netdev
 *
 * Return: 0 on success, negative value on failure
 */
static int ice_sf_cfg_netdev(struct ice_dynamic_port *dyn_port,
			     struct devlink_port *devlink_port)
{
	struct ice_vsi *vsi = dyn_port->vsi;
	struct ice_netdev_priv *np;
	struct net_device *netdev;
	int err;

	netdev = alloc_etherdev_mqs(sizeof(*np), vsi->alloc_txq,
				    vsi->alloc_rxq);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &vsi->back->pdev->dev);
	set_bit(ICE_VSI_NETDEV_ALLOCD, vsi->state);
	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	ice_set_netdev_features(netdev);

	netdev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
			       NETDEV_XDP_ACT_XSK_ZEROCOPY |
			       NETDEV_XDP_ACT_RX_SG;
	netdev->xdp_zc_max_segs = ICE_MAX_BUF_TXD;

	eth_hw_addr_set(netdev, dyn_port->hw_addr);
	ether_addr_copy(netdev->perm_addr, dyn_port->hw_addr);
	netdev->netdev_ops = &ice_sf_netdev_ops;
	SET_NETDEV_DEVLINK_PORT(netdev, devlink_port);

	err = register_netdev(netdev);
	if (err) {
		free_netdev(netdev);
		vsi->netdev = NULL;
		return -ENOMEM;
	}
	set_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state);
	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	return 0;
}

static void ice_sf_decfg_netdev(struct ice_vsi *vsi)
{
	unregister_netdev(vsi->netdev);
	clear_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state);
	free_netdev(vsi->netdev);
	vsi->netdev = NULL;
	clear_bit(ICE_VSI_NETDEV_ALLOCD, vsi->state);
}

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
	if (IS_ERR(priv)) {
		dev_err(dev, "Subfunction devlink alloc failed");
		return PTR_ERR(priv);
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

	ice_eswitch_update_repr(&dyn_port->repr_id, vsi);

	err = ice_devlink_create_sf_dev_port(sf_dev);
	if (err) {
		dev_err(dev, "Cannot add ice virtual devlink port for subfunction");
		goto err_vsi_decfg;
	}

	err = ice_sf_cfg_netdev(dyn_port, &sf_dev->priv->devlink_port);
	if (err) {
		dev_err(dev, "Subfunction netdev config failed");
		goto err_devlink_destroy;
	}

	err = devl_port_fn_devlink_set(&dyn_port->devlink_port, devlink);
	if (err) {
		dev_err(dev, "Can't link devlink instance to SF devlink port");
		goto err_netdev_decfg;
	}

	ice_napi_add(vsi);

	devl_register(devlink);
	devl_unlock(devlink);

	dyn_port->attached = true;

	return 0;

err_netdev_decfg:
	ice_sf_decfg_netdev(vsi);
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

	ice_sf_decfg_netdev(vsi);
	ice_devlink_destroy_sf_dev_port(sf_dev);
	devl_unregister(devlink);
	devl_unlock(devlink);
	devlink_free(devlink);
	ice_vsi_decfg(vsi);

	dyn_port->attached = false;
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

static DEFINE_XARRAY_ALLOC1(ice_sf_aux_id);

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

/**
 * ice_sf_dev_release - Release device associated with auxiliary device
 * @device: pointer to the device
 *
 * Since most of the code for subfunction deactivation is handled in
 * the remove handler, here just free tracking resources.
 */
static void ice_sf_dev_release(struct device *device)
{
	struct auxiliary_device *adev = to_auxiliary_dev(device);
	struct ice_sf_dev *sf_dev = ice_adev_to_sf_dev(adev);

	xa_erase(&ice_sf_aux_id, adev->id);
	kfree(sf_dev);
}

/**
 * ice_sf_eth_activate - Activate Ethernet subfunction port
 * @dyn_port: the dynamic port instance for this subfunction
 * @extack: extack for reporting error messages
 *
 * Activate the dynamic port as an Ethernet subfunction. Setup the netdev
 * resources associated and initialize the auxiliary device.
 *
 * Return: zero on success or an error code on failure.
 */
int
ice_sf_eth_activate(struct ice_dynamic_port *dyn_port,
		    struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = dyn_port->pf;
	struct ice_sf_dev *sf_dev;
	struct pci_dev *pdev;
	int err;
	u32 id;

	err = xa_alloc(&ice_sf_aux_id, &id, NULL, xa_limit_32b,
		       GFP_KERNEL);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Could not allocate SF ID");
		return err;
	}

	sf_dev = kzalloc(sizeof(*sf_dev), GFP_KERNEL);
	if (!sf_dev) {
		err = -ENOMEM;
		NL_SET_ERR_MSG_MOD(extack, "Could not allocate SF memory");
		goto xa_erase;
	}
	pdev = pf->pdev;

	sf_dev->dyn_port = dyn_port;
	sf_dev->adev.id = id;
	sf_dev->adev.name = "sf";
	sf_dev->adev.dev.release = ice_sf_dev_release;
	sf_dev->adev.dev.parent = &pdev->dev;

	err = auxiliary_device_init(&sf_dev->adev);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to initialize SF device");
		goto sf_dev_free;
	}

	err = auxiliary_device_add(&sf_dev->adev);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to add SF device");
		goto aux_dev_uninit;
	}

	dyn_port->sf_dev = sf_dev;

	return 0;

aux_dev_uninit:
	auxiliary_device_uninit(&sf_dev->adev);
sf_dev_free:
	kfree(sf_dev);
xa_erase:
	xa_erase(&ice_sf_aux_id, id);

	return err;
}

/**
 * ice_sf_eth_deactivate - Deactivate Ethernet subfunction port
 * @dyn_port: the dynamic port instance for this subfunction
 *
 * Deactivate the Ethernet subfunction, removing its auxiliary device and the
 * associated resources.
 */
void ice_sf_eth_deactivate(struct ice_dynamic_port *dyn_port)
{
	struct ice_sf_dev *sf_dev = dyn_port->sf_dev;

	auxiliary_device_delete(&sf_dev->adev);
	auxiliary_device_uninit(&sf_dev->adev);
}
