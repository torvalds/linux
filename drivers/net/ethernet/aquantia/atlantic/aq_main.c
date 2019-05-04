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
#include "aq_filters.h"

#include <linux/netdevice.h>
#include <linux/module.h>

MODULE_LICENSE("GPL v2");
MODULE_VERSION(AQ_CFG_DRV_VERSION);
MODULE_AUTHOR(AQ_CFG_DRV_AUTHOR);
MODULE_DESCRIPTION(AQ_CFG_DRV_DESC);

static const char aq_ndev_driver_name[] = AQ_CFG_DRV_NAME;

static const struct net_device_ops aq_ndev_ops;

static struct workqueue_struct *aq_ndev_wq;

void aq_ndev_schedule_work(struct work_struct *work)
{
	queue_work(aq_ndev_wq, work);
}

struct net_device *aq_ndev_alloc(void)
{
	struct net_device *ndev = NULL;
	struct aq_nic_s *aq_nic = NULL;

	ndev = alloc_etherdev_mq(sizeof(struct aq_nic_s), AQ_CFG_VECS_MAX);
	if (!ndev)
		return NULL;

	aq_nic = netdev_priv(ndev);
	aq_nic->ndev = ndev;
	ndev->netdev_ops = &aq_ndev_ops;
	ndev->ethtool_ops = &aq_ethtool_ops;

	return ndev;
}

static int aq_ndev_open(struct net_device *ndev)
{
	int err = 0;
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	err = aq_nic_init(aq_nic);
	if (err < 0)
		goto err_exit;

	err = aq_reapply_rxnfc_all_rules(aq_nic);
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

err_exit:
	return err;
}

static int aq_ndev_set_features(struct net_device *ndev,
				netdev_features_t features)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *aq_cfg = aq_nic_get_cfg(aq_nic);
	bool is_lro = false;
	int err = 0;

	if (!(features & NETIF_F_NTUPLE)) {
		if (aq_nic->ndev->features & NETIF_F_NTUPLE) {
			err = aq_clear_rxnfc_all_rules(aq_nic);
			if (unlikely(err))
				goto err_exit;
		}
	}
	if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER)) {
		if (aq_nic->ndev->features & NETIF_F_HW_VLAN_CTAG_FILTER) {
			err = aq_filters_vlan_offload_off(aq_nic);
			if (unlikely(err))
				goto err_exit;
		}
	}

	aq_cfg->features = features;

	if (aq_cfg->aq_hw_caps->hw_features & NETIF_F_LRO) {
		is_lro = features & NETIF_F_LRO;

		if (aq_cfg->is_lro != is_lro) {
			aq_cfg->is_lro = is_lro;

			if (netif_running(ndev)) {
				aq_ndev_close(ndev);
				aq_ndev_open(ndev);
			}
		}
	}
	if ((aq_nic->ndev->features ^ features) & NETIF_F_RXCSUM)
		err = aq_nic->aq_hw_ops->hw_set_offload(aq_nic->aq_hw,
							aq_cfg);

err_exit:
	return err;
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

	aq_nic_set_packet_filter(aq_nic, ndev->flags);

	aq_nic_set_multicast_list(aq_nic, ndev);
}

static int aq_ndo_vlan_rx_add_vid(struct net_device *ndev, __be16 proto,
				  u16 vid)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	if (!aq_nic->aq_hw_ops->hw_filter_vlan_set)
		return -EOPNOTSUPP;

	set_bit(vid, aq_nic->active_vlans);

	return aq_filters_vlans_update(aq_nic);
}

static int aq_ndo_vlan_rx_kill_vid(struct net_device *ndev, __be16 proto,
				   u16 vid)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	if (!aq_nic->aq_hw_ops->hw_filter_vlan_set)
		return -EOPNOTSUPP;

	clear_bit(vid, aq_nic->active_vlans);

	if (-ENOENT == aq_del_fvlan_by_vlan(aq_nic, vid))
		return aq_filters_vlans_update(aq_nic);

	return 0;
}

static const struct net_device_ops aq_ndev_ops = {
	.ndo_open = aq_ndev_open,
	.ndo_stop = aq_ndev_close,
	.ndo_start_xmit = aq_ndev_start_xmit,
	.ndo_set_rx_mode = aq_ndev_set_multicast_settings,
	.ndo_change_mtu = aq_ndev_change_mtu,
	.ndo_set_mac_address = aq_ndev_set_mac_address,
	.ndo_set_features = aq_ndev_set_features,
	.ndo_vlan_rx_add_vid = aq_ndo_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = aq_ndo_vlan_rx_kill_vid,
};

static int __init aq_ndev_init_module(void)
{
	int ret;

	aq_ndev_wq = create_singlethread_workqueue(aq_ndev_driver_name);
	if (!aq_ndev_wq) {
		pr_err("Failed to create workqueue\n");
		return -ENOMEM;
	}

	ret = aq_pci_func_register_driver();
	if (ret) {
		destroy_workqueue(aq_ndev_wq);
		return ret;
	}

	return 0;
}

static void __exit aq_ndev_exit_module(void)
{
	aq_pci_func_unregister_driver();

	if (aq_ndev_wq) {
		destroy_workqueue(aq_ndev_wq);
		aq_ndev_wq = NULL;
	}
}

module_init(aq_ndev_init_module);
module_exit(aq_ndev_exit_module);
