// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>
#include "hbg_common.h"
#include "hbg_err.h"
#include "hbg_hw.h"

static void hbg_restore_mac_table(struct hbg_priv *priv)
{
	struct hbg_mac_filter *filter = &priv->filter;
	u64 addr;
	u32 i;

	for (i = 0; i < filter->table_max_len; i++)
		if (!is_zero_ether_addr(filter->mac_table[i].addr)) {
			addr = ether_addr_to_u64(filter->mac_table[i].addr);
			hbg_hw_set_uc_addr(priv, addr, i);
		}

	hbg_hw_set_mac_filter_enable(priv, priv->filter.enabled);
}

static void hbg_restore_user_def_settings(struct hbg_priv *priv)
{
	struct ethtool_pauseparam *pause_param = &priv->user_def.pause_param;

	hbg_restore_mac_table(priv);
	hbg_hw_set_mtu(priv, priv->netdev->mtu);
	hbg_hw_set_pause_enable(priv, pause_param->tx_pause,
				pause_param->rx_pause);
}

int hbg_rebuild(struct hbg_priv *priv)
{
	int ret;

	ret = hbg_hw_init(priv);
	if (ret)
		return ret;

	hbg_restore_user_def_settings(priv);
	return 0;
}

static int hbg_reset_prepare(struct hbg_priv *priv, enum hbg_reset_type type)
{
	int ret;

	ASSERT_RTNL();

	if (netif_running(priv->netdev)) {
		dev_warn(&priv->pdev->dev,
			 "failed to reset because port is up\n");
		return -EBUSY;
	}

	priv->reset_type = type;
	set_bit(HBG_NIC_STATE_RESETTING, &priv->state);
	clear_bit(HBG_NIC_STATE_RESET_FAIL, &priv->state);
	ret = hbg_hw_event_notify(priv, HBG_HW_EVENT_RESET);
	if (ret) {
		set_bit(HBG_NIC_STATE_RESET_FAIL, &priv->state);
		clear_bit(HBG_NIC_STATE_RESETTING, &priv->state);
	}

	return ret;
}

static int hbg_reset_done(struct hbg_priv *priv, enum hbg_reset_type type)
{
	int ret;

	if (!test_bit(HBG_NIC_STATE_RESETTING, &priv->state) ||
	    type != priv->reset_type)
		return 0;

	ASSERT_RTNL();

	clear_bit(HBG_NIC_STATE_RESETTING, &priv->state);
	ret = hbg_rebuild(priv);
	if (ret) {
		set_bit(HBG_NIC_STATE_RESET_FAIL, &priv->state);
		dev_err(&priv->pdev->dev, "failed to rebuild after reset\n");
		return ret;
	}

	dev_info(&priv->pdev->dev, "reset done\n");
	return ret;
}

/* must be protected by rtnl lock */
int hbg_reset(struct hbg_priv *priv)
{
	int ret;

	ASSERT_RTNL();
	ret = hbg_reset_prepare(priv, HBG_RESET_TYPE_FUNCTION);
	if (ret)
		return ret;

	return hbg_reset_done(priv, HBG_RESET_TYPE_FUNCTION);
}

void hbg_err_reset(struct hbg_priv *priv)
{
	bool running;

	rtnl_lock();
	running = netif_running(priv->netdev);
	if (running)
		dev_close(priv->netdev);

	hbg_reset(priv);

	/* in hbg_pci_err_detected(), we will detach first,
	 * so we need to attach before open
	 */
	if (!netif_device_present(priv->netdev))
		netif_device_attach(priv->netdev);

	if (running)
		dev_open(priv->netdev, NULL);
	rtnl_unlock();
}

static pci_ers_result_t hbg_pci_err_detected(struct pci_dev *pdev,
					     pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_disable_device(pdev);
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t hbg_pci_err_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hbg_priv *priv = netdev_priv(netdev);

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
			"failed to re-enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	hbg_err_reset(priv);
	netif_device_attach(netdev);
	return PCI_ERS_RESULT_RECOVERED;
}

static void hbg_pci_err_reset_prepare(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hbg_priv *priv = netdev_priv(netdev);

	rtnl_lock();
	hbg_reset_prepare(priv, HBG_RESET_TYPE_FLR);
}

static void hbg_pci_err_reset_done(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hbg_priv *priv = netdev_priv(netdev);

	hbg_reset_done(priv, HBG_RESET_TYPE_FLR);
	rtnl_unlock();
}

static const struct pci_error_handlers hbg_pci_err_handler = {
	.error_detected = hbg_pci_err_detected,
	.slot_reset = hbg_pci_err_slot_reset,
	.reset_prepare = hbg_pci_err_reset_prepare,
	.reset_done = hbg_pci_err_reset_done,
};

void hbg_set_pci_err_handler(struct pci_driver *pdrv)
{
	pdrv->err_handler = &hbg_pci_err_handler;
}
