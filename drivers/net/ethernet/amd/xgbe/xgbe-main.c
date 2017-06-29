/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/io.h>

#include "xgbe.h"
#include "xgbe-common.h"

MODULE_AUTHOR("Tom Lendacky <thomas.lendacky@amd.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(XGBE_DRV_VERSION);
MODULE_DESCRIPTION(XGBE_DRV_DESC);

static int debug = -1;
module_param(debug, int, S_IWUSR | S_IRUGO);
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
	unsigned int i;
	int ret;

	netdev->irq = pdata->dev_irq;
	netdev->base_addr = (unsigned long)pdata->xgmac_regs;
	memcpy(netdev->dev_addr, pdata->mac_addr, netdev->addr_len);

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

	/* Set the number of queues */
	ret = netif_set_real_num_tx_queues(netdev, pdata->tx_ring_count);
	if (ret) {
		dev_err(dev, "error setting real tx queue count\n");
		return ret;
	}

	ret = netif_set_real_num_rx_queues(netdev, pdata->rx_ring_count);
	if (ret) {
		dev_err(dev, "error setting real rx queue count\n");
		return ret;
	}

	/* Initialize RSS hash key and lookup table */
	netdev_rss_key_fill(pdata->rss_key, sizeof(pdata->rss_key));

	for (i = 0; i < XGBE_RSS_MAX_TABLE_SIZE; i++)
		XGMAC_SET_BITS(pdata->rss_table[i], MAC_RSSDR, DMCH,
			       i % pdata->rx_ring_count);

	XGMAC_SET_BITS(pdata->rss_options, MAC_RSSCR, IP2TE, 1);
	XGMAC_SET_BITS(pdata->rss_options, MAC_RSSCR, TCP4TE, 1);
	XGMAC_SET_BITS(pdata->rss_options, MAC_RSSCR, UDP4TE, 1);

	/* Call MDIO/PHY initialization routine */
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

	netdev->vlan_features |= NETIF_F_SG |
				 NETIF_F_IP_CSUM |
				 NETIF_F_IPV6_CSUM |
				 NETIF_F_TSO |
				 NETIF_F_TSO6;

	netdev->features |= netdev->hw_features;
	pdata->netdev_features = netdev->features;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->min_mtu = 0;
	netdev->max_mtu = XGMAC_JUMBO_PACKET_MTU;

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

	/* Create the PHY/ANEG name based on netdev name */
	snprintf(pdata->an_name, sizeof(pdata->an_name) - 1, "%s-pcs",
		 netdev_name(netdev));

	/* Create the ECC name based on netdev name */
	snprintf(pdata->ecc_name, sizeof(pdata->ecc_name) - 1, "%s-ecc",
		 netdev_name(netdev));

	/* Create the I2C name based on netdev name */
	snprintf(pdata->i2c_name, sizeof(pdata->i2c_name) - 1, "%s-i2c",
		 netdev_name(netdev));

	/* Create workqueues */
	pdata->dev_workqueue =
		create_singlethread_workqueue(netdev_name(netdev));
	if (!pdata->dev_workqueue) {
		netdev_err(netdev, "device workqueue creation failed\n");
		ret = -ENOMEM;
		goto err_netdev;
	}

	pdata->an_workqueue =
		create_singlethread_workqueue(pdata->an_name);
	if (!pdata->an_workqueue) {
		netdev_err(netdev, "phy workqueue creation failed\n");
		ret = -ENOMEM;
		goto err_wq;
	}

	if (IS_REACHABLE(CONFIG_PTP_1588_CLOCK))
		xgbe_ptp_register(pdata);

	xgbe_debugfs_init(pdata);

	netif_dbg(pdata, drv, pdata->netdev, "%u Tx software queues\n",
		  pdata->tx_ring_count);
	netif_dbg(pdata, drv, pdata->netdev, "%u Rx software queues\n",
		  pdata->rx_ring_count);

	return 0;

err_wq:
	destroy_workqueue(pdata->dev_workqueue);

err_netdev:
	unregister_netdev(netdev);

	return ret;
}

void xgbe_deconfig_netdev(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;

	xgbe_debugfs_exit(pdata);

	if (IS_REACHABLE(CONFIG_PTP_1588_CLOCK))
		xgbe_ptp_unregister(pdata);

	pdata->phy_if.phy_exit(pdata);

	flush_workqueue(pdata->an_workqueue);
	destroy_workqueue(pdata->an_workqueue);

	flush_workqueue(pdata->dev_workqueue);
	destroy_workqueue(pdata->dev_workqueue);

	unregister_netdev(netdev);
}

static int __init xgbe_mod_init(void)
{
	int ret;

	ret = xgbe_platform_init();
	if (ret)
		return ret;

	ret = xgbe_pci_init();
	if (ret)
		return ret;

	return 0;
}

static void __exit xgbe_mod_exit(void)
{
	xgbe_pci_exit();

	xgbe_platform_exit();
}

module_init(xgbe_mod_init);
module_exit(xgbe_mod_exit);
