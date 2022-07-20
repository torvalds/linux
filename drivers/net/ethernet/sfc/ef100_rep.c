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
#include "ef100_netdev.h"
#include "ef100_nic.h"
#include "mae.h"

#define EFX_EF100_REP_DRIVER	"efx_ef100_rep"

static int efx_ef100_rep_init_struct(struct efx_nic *efx, struct efx_rep *efv,
				     unsigned int i)
{
	efv->parent = efx;
	efv->idx = i;
	INIT_LIST_HEAD(&efv->list);
	efv->msg_enable = NETIF_MSG_DRV | NETIF_MSG_PROBE |
			  NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
			  NETIF_MSG_IFUP | NETIF_MSG_RX_ERR |
			  NETIF_MSG_TX_ERR | NETIF_MSG_HW;
	return 0;
}

static netdev_tx_t efx_ef100_rep_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct efx_rep *efv = netdev_priv(dev);
	struct efx_nic *efx = efv->parent;
	netdev_tx_t rc;

	/* __ef100_hard_start_xmit() will always return success even in the
	 * case of TX drops, where it will increment efx's tx_dropped.  The
	 * efv stats really only count attempted TX, not success/failure.
	 */
	atomic64_inc(&efv->stats.tx_packets);
	atomic64_add(skb->len, &efv->stats.tx_bytes);
	netif_tx_lock(efx->net_dev);
	rc = __ef100_hard_start_xmit(skb, efx, dev, efv);
	netif_tx_unlock(efx->net_dev);
	return rc;
}

static int efx_ef100_rep_get_port_parent_id(struct net_device *dev,
					    struct netdev_phys_item_id *ppid)
{
	struct efx_rep *efv = netdev_priv(dev);
	struct efx_nic *efx = efv->parent;
	struct ef100_nic_data *nic_data;

	nic_data = efx->nic_data;
	/* nic_data->port_id is a u8[] */
	ppid->id_len = sizeof(nic_data->port_id);
	memcpy(ppid->id, nic_data->port_id, sizeof(nic_data->port_id));
	return 0;
}

static int efx_ef100_rep_get_phys_port_name(struct net_device *dev,
					    char *buf, size_t len)
{
	struct efx_rep *efv = netdev_priv(dev);
	struct efx_nic *efx = efv->parent;
	struct ef100_nic_data *nic_data;
	int ret;

	nic_data = efx->nic_data;
	ret = snprintf(buf, len, "p%upf%uvf%u", efx->port_num,
		       nic_data->pf_index, efv->idx);
	if (ret >= len)
		return -EOPNOTSUPP;

	return 0;
}

static const struct net_device_ops efx_ef100_rep_netdev_ops = {
	.ndo_start_xmit		= efx_ef100_rep_xmit,
	.ndo_get_port_parent_id	= efx_ef100_rep_get_port_parent_id,
	.ndo_get_phys_port_name	= efx_ef100_rep_get_phys_port_name,
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
	rc = efx_ef100_rep_init_struct(efx, efv, i);
	if (rc)
		goto fail1;
	efv->net_dev = net_dev;
	rtnl_lock();
	spin_lock_bh(&efx->vf_reps_lock);
	list_add_tail(&efv->list, &efx->vf_reps);
	spin_unlock_bh(&efx->vf_reps_lock);
	if (netif_running(efx->net_dev) && efx->state == STATE_NET_UP) {
		netif_device_attach(net_dev);
		netif_carrier_on(net_dev);
	} else {
		netif_carrier_off(net_dev);
		netif_tx_stop_all_queues(net_dev);
	}
	rtnl_unlock();

	net_dev->netdev_ops = &efx_ef100_rep_netdev_ops;
	net_dev->ethtool_ops = &efx_ef100_rep_ethtool_ops;
	net_dev->min_mtu = EFX_MIN_MTU;
	net_dev->max_mtu = EFX_MAX_MTU;
	net_dev->features |= NETIF_F_LLTX;
	net_dev->hw_features |= NETIF_F_LLTX;
	return efv;
fail1:
	free_netdev(net_dev);
	return ERR_PTR(rc);
}

static int efx_ef100_configure_rep(struct efx_rep *efv)
{
	struct efx_nic *efx = efv->parent;
	u32 selector;
	int rc;

	/* Construct mport selector for corresponding VF */
	efx_mae_mport_vf(efx, efv->idx, &selector);
	/* Look up actual mport ID */
	rc = efx_mae_lookup_mport(efx, selector, &efv->mport);
	if (rc)
		return rc;
	pci_dbg(efx->pci_dev, "VF %u has mport ID %#x\n", efv->idx, efv->mport);
	/* mport label should fit in 16 bits */
	WARN_ON(efv->mport >> 16);

	return 0;
}

static void efx_ef100_rep_destroy_netdev(struct efx_rep *efv)
{
	struct efx_nic *efx = efv->parent;

	rtnl_lock();
	spin_lock_bh(&efx->vf_reps_lock);
	list_del(&efv->list);
	spin_unlock_bh(&efx->vf_reps_lock);
	rtnl_unlock();
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
	rc = efx_ef100_configure_rep(efv);
	if (rc) {
		pci_err(efx->pci_dev,
			"Failed to configure representor for VF %d, rc %d\n",
			i, rc);
		goto fail;
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
