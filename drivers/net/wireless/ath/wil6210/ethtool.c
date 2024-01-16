// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014,2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>

#include "wil6210.h"

static int
wil_ethtoolops_get_coalesce(struct net_device *ndev,
			    struct ethtool_coalesce *cp,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct wil6210_priv *wil = ndev_to_wil(ndev);
	u32 tx_itr_en, tx_itr_val = 0;
	u32 rx_itr_en, rx_itr_val = 0;
	int ret;

	mutex_lock(&wil->mutex);
	wil_dbg_misc(wil, "ethtoolops_get_coalesce\n");

	ret = wil_pm_runtime_get(wil);
	if (ret < 0)
		goto out;

	tx_itr_en = wil_r(wil, RGF_DMA_ITR_TX_CNT_CTL);
	if (tx_itr_en & BIT_DMA_ITR_TX_CNT_CTL_EN)
		tx_itr_val = wil_r(wil, RGF_DMA_ITR_TX_CNT_TRSH);

	rx_itr_en = wil_r(wil, RGF_DMA_ITR_RX_CNT_CTL);
	if (rx_itr_en & BIT_DMA_ITR_RX_CNT_CTL_EN)
		rx_itr_val = wil_r(wil, RGF_DMA_ITR_RX_CNT_TRSH);

	wil_pm_runtime_put(wil);

	cp->tx_coalesce_usecs = tx_itr_val;
	cp->rx_coalesce_usecs = rx_itr_val;
	ret = 0;

out:
	mutex_unlock(&wil->mutex);
	return ret;
}

static int
wil_ethtoolops_set_coalesce(struct net_device *ndev,
			    struct ethtool_coalesce *cp,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct wil6210_priv *wil = ndev_to_wil(ndev);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	int ret;

	mutex_lock(&wil->mutex);
	wil_dbg_misc(wil, "ethtoolops_set_coalesce: rx %d usec, tx %d usec\n",
		     cp->rx_coalesce_usecs, cp->tx_coalesce_usecs);

	if (wdev->iftype == NL80211_IFTYPE_MONITOR) {
		wil_dbg_misc(wil, "No IRQ coalescing in monitor mode\n");
		ret = -EINVAL;
		goto out;
	}

	/* only @rx_coalesce_usecs and @tx_coalesce_usecs supported,
	 * ignore other parameters
	 */

	if (cp->rx_coalesce_usecs > WIL6210_ITR_TRSH_MAX ||
	    cp->tx_coalesce_usecs > WIL6210_ITR_TRSH_MAX)
		goto out_bad;

	wil->tx_max_burst_duration = cp->tx_coalesce_usecs;
	wil->rx_max_burst_duration = cp->rx_coalesce_usecs;

	ret = wil_pm_runtime_get(wil);
	if (ret < 0)
		goto out;

	wil->txrx_ops.configure_interrupt_moderation(wil);

	wil_pm_runtime_put(wil);
	ret = 0;

out:
	mutex_unlock(&wil->mutex);
	return ret;

out_bad:
	wil_dbg_misc(wil, "Unsupported coalescing params. Raw command:\n");
	print_hex_dump_debug("DBG[MISC] coal ", DUMP_PREFIX_OFFSET, 16, 4,
			     cp, sizeof(*cp), false);
	mutex_unlock(&wil->mutex);
	return -EINVAL;
}

static const struct ethtool_ops wil_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS,
	.get_drvinfo	= cfg80211_get_drvinfo,
	.get_coalesce	= wil_ethtoolops_get_coalesce,
	.set_coalesce	= wil_ethtoolops_set_coalesce,
};

void wil_set_ethtoolops(struct net_device *ndev)
{
	ndev->ethtool_ops = &wil_ethtool_ops;
}
