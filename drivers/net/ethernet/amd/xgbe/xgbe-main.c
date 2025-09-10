// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/notifier.h>

#include "xgbe.h"
#include "xgbe-common.h"

MODULE_AUTHOR("Tom Lendacky <thomas.lendacky@amd.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(XGBE_DRV_DESC);

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, " Network interface message level setting");

static const u32 default_msg_level = (NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
				      NETIF_MSG_IFUP);

static void xgbe_default_config(struct xgbe_prv_data *pdata)
{
	DBGPR("-->xgbe_default_config\n");

	pdata->blen = DMA_SBMR_BLEN_64;
	pdata->pbl = DMA_PBL_128;
	pdata->aal = 1;
	pdata->rd_osr_limit = 8;
	pdata->wr_osr_limit = 8;
	pdata->tx_sf_mode = MTL_TSF_ENABLE;
	pdata->tx_threshold = MTL_TX_THRESHOLD_64;
	pdata->tx_osp_mode = DMA_OSP_ENABLE;
	pdata->rx_sf_mode = MTL_RSF_DISABLE;
	pdata->rx_threshold = MTL_RX_THRESHOLD_64;
	pdata->pause_autoneg = 1;
	pdata->tx_pause = 1;
	pdata->rx_pause = 1;
	pdata->phy_speed = SPEED_UNKNOWN;
	pdata->power_down = 0;

	DBGPR("<--xgbe_default_config\n");
}

static void xgbe_init_all_fptrs(struct xgbe_prv_data *pdata)
{
	xgbe_init_function_ptrs_dev(&pdata->hw_if);
	xgbe_init_function_ptrs_phy(&pdata->phy_if);
	xgbe_init_function_ptrs_i2c(&pdata->i2c_if);
	xgbe_init_function_ptrs_desc(&pdata->desc_if);

	pdata->vdata->init_function_ptrs_phy_impl(&pdata->phy_if);
}

struct xgbe_prv_data *xgbe_alloc_pdata(struct device *dev)
{
	struct xgbe_prv_data *pdata;
	struct net_device *netdev;

	netdev = alloc_etherdev_mq(sizeof(struct xgbe_prv_data),
				   XGBE_MAX_DMA_CHANNELS);
	if (!netdev) {
		dev_err(dev, "alloc_etherdev_mq failed\n");
		return ERR_PTR(-ENOMEM);
	}
	SET_NETDEV_DEV(netdev, dev);
	pdata = netdev_priv(netdev);
	pdata->netdev = netdev;
	pdata->dev = dev;

	spin_lock_init(&pdata->lock);
	spin_lock_init(&pdata->xpcs_lock);
	mutex_init(&pdata->rss_mutex);
	spin_lock_init(&pdata->tstamp_lock);
	mutex_init(&pdata->i2c_mutex);
	init_completion(&pdata->i2c_complete);
	init_completion(&pdata->mdio_complete);

	pdata->msg_enable = netif_msg_init(debug, default_msg_level);

	set_bit(XGBE_DOWN, &pdata->dev_state);
	set_bit(XGBE_STOPPED, &pdata->dev_state);

	return pdata;
}

void xgbe_free_pdata(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;

	free_netdev(netdev);
}

void xgbe_set_counts(struct xgbe_prv_data *pdata)
{
	/* Set all the function pointers */
	xgbe_init_all_fptrs(pdata);

	/* Populate the hardware features */
	xgbe_get_all_hw_features(pdata);

	/* Set default max values if not provided */
	if (!pdata->tx_max_channel_count)
		pdata->tx_max_channel_count = pdata->hw_feat.tx_ch_cnt;
	if (!pdata->rx_max_channel_count)
		pdata->rx_max_channel_count = pdata->hw_feat.rx_ch_cnt;

	if (!pdata->tx_max_q_count)
		pdata->tx_max_q_count = pdata->hw_feat.tx_q_cnt;
	if (!pdata->rx_max_q_count)
		pdata->rx_max_q_count = pdata->hw_feat.rx_q_cnt;

	/* Calculate the number of Tx and Rx rings to be created
	 *  -Tx (DMA) Channels map 1-to-1 to Tx Queues so set
	 *   the number of Tx queues to the number of Tx channels
	 *   enabled
	 *  -Rx (DMA) Channels do not map 1-to-1 so use the actual
	 *   number of Rx queues or maximum allowed
	 */
	pdata->tx_ring_count = min_t(unsigned int, num_online_cpus(),
				     pdata->hw_feat.tx_ch_cnt);
	pdata->tx_ring_count = min_t(unsigned int, pdata->tx_ring_count,
				     pdata->tx_max_channel_count);
	pdata->tx_ring_count = min_t(unsigned int, pdata->tx_ring_count,
				     pdata->tx_max_q_count);

	pdata->tx_q_count = pdata->tx_ring_count;

	pdata->rx_ring_count = min_t(unsigned int, num_online_cpus(),
				     pdata->hw_feat.rx_ch_cnt);
	pdata->rx_ring_count = min_t(unsigned int, pdata->rx_ring_count,
				     pdata->rx_max_channel_count);

	pdata->rx_q_count = min_t(unsigned int, pdata->hw_feat.rx_q_cnt,
				  pdata->rx_max_q_count);

	if (netif_msg_probe(pdata)) {
		dev_dbg(pdata->dev, "TX/RX DMA channel count = %u/%u\n",
			pdata->tx_ring_count, pdata->rx_ring_count);
		dev_dbg(pdata->dev, "TX/RX hardware queue count = %u/%u\n",
			pdata->tx_q_count, pdata->rx_q_count);
	}
}

int xgbe_config_netdev(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;
	struct device *dev = pdata->dev;
	int ret;

	netdev->irq = pdata->dev_irq;
	netdev->base_addr = (unsigned long)pdata->xgmac_regs;
	eth_hw_addr_set(netdev, pdata->mac_addr);

	/* Initialize ECC timestamps */
	pdata->tx_sec_period = jiffies;
	pdata->tx_ded_period = jiffies;
	pdata->rx_sec_period = jiffies;
	pdata->rx_ded_period = jiffies;
	pdata->desc_sec_period = jiffies;
	pdata->desc_ded_period = jiffies;

	/* Issue software reset to device */
	ret = pdata->hw_if.exit(pdata);
	if (ret) {
		dev_err(dev, "software reset failed\n");
		return ret;
	}

	/* Set default configuration data */
	xgbe_default_config(pdata);

	/* Set the DMA mask */
	ret = dma_set_mask_and_coherent(dev,
					DMA_BIT_MASK(pdata->hw_feat.dma_width));
	if (ret) {
		dev_err(dev, "dma_set_mask_and_coherent failed\n");
		return ret;
	}

	/* Set default max values if not provided */
	if (!pdata->tx_max_fifo_size)
		pdata->tx_max_fifo_size = pdata->hw_feat.tx_fifo_size;
	if (!pdata->rx_max_fifo_size)
		pdata->rx_max_fifo_size = pdata->hw_feat.rx_fifo_size;

	/* Set and validate the number of descriptors for a ring */
	BUILD_BUG_ON_NOT_POWER_OF_2(XGBE_TX_DESC_CNT);
	pdata->tx_desc_count = XGBE_TX_DESC_CNT;

	BUILD_BUG_ON_NOT_POWER_OF_2(XGBE_RX_DESC_CNT);
	pdata->rx_desc_count = XGBE_RX_DESC_CNT;

	/* Adjust the number of queues based on interrupts assigned */
	if (pdata->channel_irq_count) {
		pdata->tx_ring_count = min_t(unsigned int, pdata->tx_ring_count,
					     pdata->channel_irq_count);
		pdata->rx_ring_count = min_t(unsigned int, pdata->rx_ring_count,
					     pdata->channel_irq_count);

		if (netif_msg_probe(pdata))
			dev_dbg(pdata->dev,
				"adjusted TX/RX DMA channel count = %u/%u\n",
				pdata->tx_ring_count, pdata->rx_ring_count);
	}

	/* Initialize RSS hash key */
	netdev_rss_key_fill(pdata->rss_key, sizeof(pdata->rss_key));

	XGMAC_SET_BITS(pdata->rss_options, MAC_RSSCR, IP2TE, 1);
	XGMAC_SET_BITS(pdata->rss_options, MAC_RSSCR, TCP4TE, 1);
	XGMAC_SET_BITS(pdata->rss_options, MAC_RSSCR, UDP4TE, 1);

	/* Call MDIO/PHY initialization routine */
	pdata->debugfs_an_cdr_workaround = pdata->vdata->an_cdr_workaround;
	ret = pdata->phy_if.phy_init(pdata);
	if (ret)
		return ret;

	/* Set device operations */
	netdev->netdev_ops = xgbe_get_netdev_ops();
	netdev->ethtool_ops = xgbe_get_ethtool_ops();
#ifdef CONFIG_AMD_XGBE_DCB
	netdev->dcbnl_ops = xgbe_get_dcbnl_ops();
#endif

	/* Set device features */
	netdev->hw_features = NETIF_F_SG |
			      NETIF_F_IP_CSUM |
			      NETIF_F_IPV6_CSUM |
			      NETIF_F_RXCSUM |
			      NETIF_F_TSO |
			      NETIF_F_TSO6 |
			      NETIF_F_GRO |
			      NETIF_F_HW_VLAN_CTAG_RX |
			      NETIF_F_HW_VLAN_CTAG_TX |
			      NETIF_F_HW_VLAN_CTAG_FILTER;

	if (pdata->hw_feat.rss)
		netdev->hw_features |= NETIF_F_RXHASH;

	if (pdata->hw_feat.vxn) {
		netdev->hw_enc_features = NETIF_F_SG |
					  NETIF_F_IP_CSUM |
					  NETIF_F_IPV6_CSUM |
					  NETIF_F_RXCSUM |
					  NETIF_F_TSO |
					  NETIF_F_TSO6 |
					  NETIF_F_GRO |
					  NETIF_F_GSO_UDP_TUNNEL |
					  NETIF_F_GSO_UDP_TUNNEL_CSUM;

		netdev->hw_features |= NETIF_F_GSO_UDP_TUNNEL |
				       NETIF_F_GSO_UDP_TUNNEL_CSUM;

		netdev->udp_tunnel_nic_info = xgbe_get_udp_tunnel_info();
	}

	netdev->vlan_features |= NETIF_F_SG |
				 NETIF_F_IP_CSUM |
				 NETIF_F_IPV6_CSUM |
				 NETIF_F_TSO |
				 NETIF_F_TSO6;

	netdev->features |= netdev->hw_features;
	pdata->netdev_features = netdev->features;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->min_mtu = 0;
	netdev->max_mtu = XGMAC_GIANT_PACKET_MTU - XGBE_ETH_FRAME_HDR;

	/* Use default watchdog timeout */
	netdev->watchdog_timeo = 0;

	xgbe_init_rx_coalesce(pdata);
	xgbe_init_tx_coalesce(pdata);

	netif_carrier_off(netdev);
	ret = register_netdev(netdev);
	if (ret) {
		dev_err(dev, "net device registration failed\n");
		return ret;
	}

	if (IS_REACHABLE(CONFIG_PTP_1588_CLOCK))
		xgbe_ptp_register(pdata);

	xgbe_debugfs_init(pdata);

	netif_dbg(pdata, drv, pdata->netdev, "%u Tx software queues\n",
		  pdata->tx_ring_count);
	netif_dbg(pdata, drv, pdata->netdev, "%u Rx software queues\n",
		  pdata->rx_ring_count);

	return 0;
}

void xgbe_deconfig_netdev(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;

	xgbe_debugfs_exit(pdata);

	if (IS_REACHABLE(CONFIG_PTP_1588_CLOCK))
		xgbe_ptp_unregister(pdata);

	unregister_netdev(netdev);

	pdata->phy_if.phy_exit(pdata);
}

static int xgbe_netdev_event(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(data);
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	if (netdev->netdev_ops != xgbe_get_netdev_ops())
		goto out;

	switch (event) {
	case NETDEV_CHANGENAME:
		xgbe_debugfs_rename(pdata);
		break;

	default:
		break;
	}

out:
	return NOTIFY_DONE;
}

static struct notifier_block xgbe_netdev_notifier = {
	.notifier_call = xgbe_netdev_event,
};

static int __init xgbe_mod_init(void)
{
	int ret;

	ret = register_netdevice_notifier(&xgbe_netdev_notifier);
	if (ret)
		return ret;

	ret = xgbe_platform_init();
	if (ret)
		goto err_platform_init;

	ret = xgbe_pci_init();
	if (ret)
		goto err_pci_init;

	return 0;

err_pci_init:
	xgbe_platform_exit();
err_platform_init:
	unregister_netdevice_notifier(&xgbe_netdev_notifier);
	return ret;
}

static void __exit xgbe_mod_exit(void)
{
	xgbe_pci_exit();

	xgbe_platform_exit();

	unregister_netdevice_notifier(&xgbe_netdev_notifier);
}

module_init(xgbe_mod_init);
module_exit(xgbe_mod_exit);
