/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_main.c: Main file for aQuantia Linux driver. */

#include "aq_main.h"
#include "aq_nic.h"
#include "aq_pci_func.h"
#include "aq_ethtool.h"
#include "hw_atl/hw_atl_a0.h"
#include "hw_atl/hw_atl_b0.h"

#include <linux/netdevice.h>
#include <linux/module.h>

static const struct pci_device_id aq_pci_tbl[] = {
	{ PCI_VDEVICE(AQUANTIA, HW_ATL_DEVICE_ID_0001), },
	{ PCI_VDEVICE(AQUANTIA, HW_ATL_DEVICE_ID_D100), },
	{ PCI_VDEVICE(AQUANTIA, HW_ATL_DEVICE_ID_D107), },
	{ PCI_VDEVICE(AQUANTIA, HW_ATL_DEVICE_ID_D108), },
	{ PCI_VDEVICE(AQUANTIA, HW_ATL_DEVICE_ID_D109), },
	{}
};

MODULE_DEVICE_TABLE(pci, aq_pci_tbl);

MODULE_LICENSE("GPL v2");
MODULE_VERSION(AQ_CFG_DRV_VERSION);
MODULE_AUTHOR(AQ_CFG_DRV_AUTHOR);
MODULE_DESCRIPTION(AQ_CFG_DRV_DESC);

static struct aq_hw_ops *aq_pci_probe_get_hw_ops_by_id(struct pci_dev *pdev)
{
	struct aq_hw_ops *ops = NULL;

	ops = hw_atl_a0_get_ops_by_id(pdev);
	if (!ops)
		ops = hw_atl_b0_get_ops_by_id(pdev);

	return ops;
}

static int aq_ndev_open(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = NULL;
	int err = 0;

	aq_nic = aq_nic_alloc_hot(ndev);
	if (!aq_nic) {
		err = -ENOMEM;
		goto err_exit;
	}
	err = aq_nic_init(aq_nic);
	if (err < 0)
		goto err_exit;
	err = aq_nic_start(aq_nic);
	if (err < 0)
		goto err_exit;

err_exit:
	if (err < 0)
		aq_nic_deinit(aq_nic);
	return err;
}

static int aq_ndev_close(struct net_device *ndev)
{
	int err = 0;
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	err = aq_nic_stop(aq_nic);
	if (err < 0)
		goto err_exit;
	aq_nic_deinit(aq_nic);
	aq_nic_free_hot_resources(aq_nic);

err_exit:
	return err;
}

static int aq_ndev_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	return aq_nic_xmit(aq_nic, skb);
}

static int aq_ndev_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = aq_nic_set_mtu(aq_nic, new_mtu + ETH_HLEN);

	if (err < 0)
		goto err_exit;
	ndev->mtu = new_mtu;

	if (netif_running(ndev)) {
		aq_ndev_close(ndev);
		aq_ndev_open(ndev);
	}

err_exit:
	return err;
}

static int aq_ndev_set_features(struct net_device *ndev,
				netdev_features_t features)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *aq_cfg = aq_nic_get_cfg(aq_nic);
	bool is_lro = false;

	if (aq_cfg->hw_features & NETIF_F_LRO) {
		is_lro = features & NETIF_F_LRO;

		if (aq_cfg->is_lro != is_lro) {
			aq_cfg->is_lro = is_lro;

			if (netif_running(ndev)) {
				aq_ndev_close(ndev);
				aq_ndev_open(ndev);
			}
		}
	}

	return 0;
}

static int aq_ndev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	err = eth_mac_addr(ndev, addr);
	if (err < 0)
		goto err_exit;
	err = aq_nic_set_mac(aq_nic, ndev);
	if (err < 0)
		goto err_exit;

err_exit:
	return err;
}

static void aq_ndev_set_multicast_settings(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	err = aq_nic_set_packet_filter(aq_nic, ndev->flags);
	if (err < 0)
		goto err_exit;

	if (netdev_mc_count(ndev)) {
		err = aq_nic_set_multicast_list(aq_nic, ndev);
		if (err < 0)
			goto err_exit;
	}

err_exit:;
}

static const struct net_device_ops aq_ndev_ops = {
	.ndo_open = aq_ndev_open,
	.ndo_stop = aq_ndev_close,
	.ndo_start_xmit = aq_ndev_start_xmit,
	.ndo_set_rx_mode = aq_ndev_set_multicast_settings,
	.ndo_change_mtu = aq_ndev_change_mtu,
	.ndo_set_mac_address = aq_ndev_set_mac_address,
	.ndo_set_features = aq_ndev_set_features
};

static int aq_pci_probe(struct pci_dev *pdev,
			const struct pci_device_id *pci_id)
{
	struct aq_hw_ops *aq_hw_ops = NULL;
	struct aq_pci_func_s *aq_pci_func = NULL;
	int err = 0;

	err = pci_enable_device(pdev);
	if (err < 0)
		goto err_exit;
	aq_hw_ops = aq_pci_probe_get_hw_ops_by_id(pdev);
	aq_pci_func = aq_pci_func_alloc(aq_hw_ops, pdev,
					&aq_ndev_ops, &aq_ethtool_ops);
	if (!aq_pci_func) {
		err = -ENOMEM;
		goto err_exit;
	}
	err = aq_pci_func_init(aq_pci_func);
	if (err < 0)
		goto err_exit;

err_exit:
	if (err < 0) {
		if (aq_pci_func)
			aq_pci_func_free(aq_pci_func);
	}
	return err;
}

static void aq_pci_remove(struct pci_dev *pdev)
{
	struct aq_pci_func_s *aq_pci_func = pci_get_drvdata(pdev);

	aq_pci_func_deinit(aq_pci_func);
	aq_pci_func_free(aq_pci_func);
}

static int aq_pci_suspend(struct pci_dev *pdev, pm_message_t pm_msg)
{
	struct aq_pci_func_s *aq_pci_func = pci_get_drvdata(pdev);

	return aq_pci_func_change_pm_state(aq_pci_func, &pm_msg);
}

static int aq_pci_resume(struct pci_dev *pdev)
{
	struct aq_pci_func_s *aq_pci_func = pci_get_drvdata(pdev);
	pm_message_t pm_msg = PMSG_RESTORE;

	return aq_pci_func_change_pm_state(aq_pci_func, &pm_msg);
}

static struct pci_driver aq_pci_ops = {
	.name = AQ_CFG_DRV_NAME,
	.id_table = aq_pci_tbl,
	.probe = aq_pci_probe,
	.remove = aq_pci_remove,
	.suspend = aq_pci_suspend,
	.resume = aq_pci_resume,
};

module_pci_driver(aq_pci_ops);
