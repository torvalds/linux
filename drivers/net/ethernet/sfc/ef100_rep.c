// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "ef100_rep.h"
#include "ef100_nic.h"

#define EFX_EF100_REP_DRIVER	"efx_ef100_rep"

static int efx_ef100_rep_init_struct(struct efx_nic *efx, struct efx_rep *efv)
{
	efv->parent = efx;
	INIT_LIST_HEAD(&efv->list);
	efv->msg_enable = NETIF_MSG_DRV | NETIF_MSG_PROBE |
			  NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
			  NETIF_MSG_IFUP | NETIF_MSG_RX_ERR |
			  NETIF_MSG_TX_ERR | NETIF_MSG_HW;
	return 0;
}

static const struct net_device_ops efx_ef100_rep_netdev_ops = {
};

static void efx_ef100_rep_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	strscpy(drvinfo->driver, EFX_EF100_REP_DRIVER, sizeof(drvinfo->driver));
}

static u32 efx_ef100_rep_ethtool_get_msglevel(struct net_device *net_dev)
{
	struct efx_rep *efv = netdev_priv(net_dev);

	return efv->msg_enable;
}

static void efx_ef100_rep_ethtool_set_msglevel(struct net_device *net_dev,
					       u32 msg_enable)
{
	struct efx_rep *efv = netdev_priv(net_dev);

	efv->msg_enable = msg_enable;
}

static const struct ethtool_ops efx_ef100_rep_ethtool_ops = {
	.get_drvinfo		= efx_ef100_rep_get_drvinfo,
	.get_msglevel		= efx_ef100_rep_ethtool_get_msglevel,
	.set_msglevel		= efx_ef100_rep_ethtool_set_msglevel,
};

static struct efx_rep *efx_ef100_rep_create_netdev(struct efx_nic *efx,
						   unsigned int i)
{
	struct net_device *net_dev;
	struct efx_rep *efv;
	int rc;

	net_dev = alloc_etherdev_mq(sizeof(*efv), 1);
	if (!net_dev)
		return ERR_PTR(-ENOMEM);

	efv = netdev_priv(net_dev);
	rc = efx_ef100_rep_init_struct(efx, efv);
	if (rc)
		goto fail1;
	efv->net_dev = net_dev;
	rtnl_lock();
	spin_lock_bh(&efx->vf_reps_lock);
	list_add_tail(&efv->list, &efx->vf_reps);
	spin_unlock_bh(&efx->vf_reps_lock);
	netif_carrier_off(net_dev);
	netif_tx_stop_all_queues(net_dev);
	rtnl_unlock();

	net_dev->netdev_ops = &efx_ef100_rep_netdev_ops;
	net_dev->ethtool_ops = &efx_ef100_rep_ethtool_ops;
	net_dev->min_mtu = EFX_MIN_MTU;
	net_dev->max_mtu = EFX_MAX_MTU;
	return efv;
fail1:
	free_netdev(net_dev);
	return ERR_PTR(rc);
}

static void efx_ef100_rep_destroy_netdev(struct efx_rep *efv)
{
	struct efx_nic *efx = efv->parent;

	spin_lock_bh(&efx->vf_reps_lock);
	list_del(&efv->list);
	spin_unlock_bh(&efx->vf_reps_lock);
	free_netdev(efv->net_dev);
}

int efx_ef100_vfrep_create(struct efx_nic *efx, unsigned int i)
{
	struct efx_rep *efv;
	int rc;

	efv = efx_ef100_rep_create_netdev(efx, i);
	if (IS_ERR(efv)) {
		rc = PTR_ERR(efv);
		pci_err(efx->pci_dev,
			"Failed to create representor for VF %d, rc %d\n", i,
			rc);
		return rc;
	}
	rc = register_netdev(efv->net_dev);
	if (rc) {
		pci_err(efx->pci_dev,
			"Failed to register representor for VF %d, rc %d\n",
			i, rc);
		goto fail;
	}
	pci_dbg(efx->pci_dev, "Representor for VF %d is %s\n", i,
		efv->net_dev->name);
	return 0;
fail:
	efx_ef100_rep_destroy_netdev(efv);
	return rc;
}

void efx_ef100_vfrep_destroy(struct efx_nic *efx, struct efx_rep *efv)
{
	struct net_device *rep_dev;

	rep_dev = efv->net_dev;
	if (!rep_dev)
		return;
	netif_dbg(efx, drv, rep_dev, "Removing VF representor\n");
	unregister_netdev(rep_dev);
	efx_ef100_rep_destroy_netdev(efv);
}

void efx_ef100_fini_vfreps(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	struct efx_rep *efv, *next;

	if (!nic_data->grp_mae)
		return;

	list_for_each_entry_safe(efv, next, &efx->vf_reps, list)
		efx_ef100_vfrep_destroy(efx, efv);
}
