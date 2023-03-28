// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtl_core.h"
#include "r8192E_hw.h"
#include "r8190P_rtl8256.h"
#include "rtl_pm.h"


int rtl92e_suspend(struct device *dev_d)
{
	struct net_device *dev = dev_get_drvdata(dev_d);
	struct r8192_priv *priv = rtllib_priv(dev);
	u32	ulRegRead;

	netdev_info(dev, "============> r8192E suspend call.\n");
	del_timer_sync(&priv->gpio_polling_timer);
	cancel_delayed_work_sync(&priv->gpio_change_rf_wq);
	priv->polling_timer_on = 0;

	if (!netif_running(dev)) {
		netdev_info(dev,
			    "RTL819XE:UI is open out of suspend function\n");
		goto out_pci_suspend;
	}

	if (dev->netdev_ops->ndo_stop)
		dev->netdev_ops->ndo_stop(dev);
	netif_device_detach(dev);

	if (!priv->rtllib->bSupportRemoteWakeUp) {
		rtl92e_set_rf_state(dev, rf_off, RF_CHANGE_BY_INIT);
		ulRegRead = rtl92e_readl(dev, CPU_GEN);
		ulRegRead |= CPU_GEN_SYSTEM_RESET;
		rtl92e_writel(dev, CPU_GEN, ulRegRead);
	} else {
		rtl92e_writel(dev, WFCRC0, 0xffffffff);
		rtl92e_writel(dev, WFCRC1, 0xffffffff);
		rtl92e_writel(dev, WFCRC2, 0xffffffff);
		rtl92e_writeb(dev, PMR, 0x5);
		rtl92e_writeb(dev, MAC_BLK_CTRL, 0xa);
	}
out_pci_suspend:
	netdev_info(dev, "WOL is %s\n", priv->rtllib->bSupportRemoteWakeUp ?
			    "Supported" : "Not supported");
	device_set_wakeup_enable(dev_d, priv->rtllib->bSupportRemoteWakeUp);

	mdelay(20);

	return 0;
}

int rtl92e_resume(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct net_device *dev = dev_get_drvdata(dev_d);
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 val;

	netdev_info(dev, "================>r8192E resume call.\n");


	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	device_wakeup_disable(dev_d);

	if (priv->polling_timer_on == 0)
		rtl92e_check_rfctrl_gpio_timer(&priv->gpio_polling_timer);

	if (!netif_running(dev)) {
		netdev_info(dev,
			    "RTL819XE:UI is open out of resume function\n");
		goto out;
	}

	netif_device_attach(dev);
	if (dev->netdev_ops->ndo_open)
		dev->netdev_ops->ndo_open(dev);

	if (!priv->rtllib->bSupportRemoteWakeUp)
		rtl92e_set_rf_state(dev, rf_on, RF_CHANGE_BY_INIT);

out:
	return 0;
}

