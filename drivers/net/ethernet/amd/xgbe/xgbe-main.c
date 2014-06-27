/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
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
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
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
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/clk.h>

#include "xgbe.h"
#include "xgbe-common.h"


MODULE_AUTHOR("Tom Lendacky <thomas.lendacky@amd.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(XGBE_DRV_VERSION);
MODULE_DESCRIPTION(XGBE_DRV_DESC);

static struct xgbe_channel *xgbe_alloc_rings(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel_mem, *channel;
	struct xgbe_ring *tx_ring, *rx_ring;
	unsigned int count, i;

	DBGPR("-->xgbe_alloc_rings\n");

	count = max_t(unsigned int, pdata->tx_ring_count, pdata->rx_ring_count);

	channel_mem = devm_kcalloc(pdata->dev, count,
				   sizeof(struct xgbe_channel), GFP_KERNEL);
	if (!channel_mem)
		return NULL;

	tx_ring = devm_kcalloc(pdata->dev, pdata->tx_ring_count,
			       sizeof(struct xgbe_ring), GFP_KERNEL);
	if (!tx_ring)
		return NULL;

	rx_ring = devm_kcalloc(pdata->dev, pdata->rx_ring_count,
			       sizeof(struct xgbe_ring), GFP_KERNEL);
	if (!rx_ring)
		return NULL;

	for (i = 0, channel = channel_mem; i < count; i++, channel++) {
		snprintf(channel->name, sizeof(channel->name), "channel-%d", i);
		channel->pdata = pdata;
		channel->queue_index = i;
		channel->dma_regs = pdata->xgmac_regs + DMA_CH_BASE +
				    (DMA_CH_INC * i);

		if (i < pdata->tx_ring_count) {
			spin_lock_init(&tx_ring->lock);
			channel->tx_ring = tx_ring++;
		}

		if (i < pdata->rx_ring_count) {
			spin_lock_init(&tx_ring->lock);
			channel->rx_ring = rx_ring++;
		}

		DBGPR("  %s - queue_index=%u, dma_regs=%p, tx=%p, rx=%p\n",
		      channel->name, channel->queue_index, channel->dma_regs,
		      channel->tx_ring, channel->rx_ring);
	}

	pdata->channel_count = count;

	DBGPR("<--xgbe_alloc_rings\n");

	return channel_mem;
}

static void xgbe_default_config(struct xgbe_prv_data *pdata)
{
	DBGPR("-->xgbe_default_config\n");

	pdata->pblx8 = DMA_PBL_X8_ENABLE;
	pdata->tx_sf_mode = MTL_TSF_ENABLE;
	pdata->tx_threshold = MTL_TX_THRESHOLD_64;
	pdata->tx_pbl = DMA_PBL_16;
	pdata->tx_osp_mode = DMA_OSP_ENABLE;
	pdata->rx_sf_mode = MTL_RSF_DISABLE;
	pdata->rx_threshold = MTL_RX_THRESHOLD_64;
	pdata->rx_pbl = DMA_PBL_16;
	pdata->pause_autoneg = 1;
	pdata->tx_pause = 1;
	pdata->rx_pause = 1;
	pdata->power_down = 0;
	pdata->default_autoneg = AUTONEG_ENABLE;
	pdata->default_speed = SPEED_10000;

	DBGPR("<--xgbe_default_config\n");
}

static void xgbe_init_all_fptrs(struct xgbe_prv_data *pdata)
{
	xgbe_init_function_ptrs_dev(&pdata->hw_if);
	xgbe_init_function_ptrs_desc(&pdata->desc_if);
}

static int xgbe_probe(struct platform_device *pdev)
{
	struct xgbe_prv_data *pdata;
	struct xgbe_hw_if *hw_if;
	struct xgbe_desc_if *desc_if;
	struct net_device *netdev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const u8 *mac_addr;
	int ret;

	DBGPR("--> xgbe_probe\n");

	netdev = alloc_etherdev_mq(sizeof(struct xgbe_prv_data),
				   XGBE_MAX_DMA_CHANNELS);
	if (!netdev) {
		dev_err(dev, "alloc_etherdev failed\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	SET_NETDEV_DEV(netdev, dev);
	pdata = netdev_priv(netdev);
	pdata->netdev = netdev;
	pdata->pdev = pdev;
	pdata->dev = dev;
	platform_set_drvdata(pdev, netdev);

	spin_lock_init(&pdata->lock);
	mutex_init(&pdata->xpcs_mutex);

	/* Set and validate the number of descriptors for a ring */
	BUILD_BUG_ON_NOT_POWER_OF_2(XGBE_TX_DESC_CNT);
	pdata->tx_desc_count = XGBE_TX_DESC_CNT;
	if (pdata->tx_desc_count & (pdata->tx_desc_count - 1)) {
		dev_err(dev, "tx descriptor count (%d) is not valid\n",
			pdata->tx_desc_count);
		ret = -EINVAL;
		goto err_io;
	}
	BUILD_BUG_ON_NOT_POWER_OF_2(XGBE_RX_DESC_CNT);
	pdata->rx_desc_count = XGBE_RX_DESC_CNT;
	if (pdata->rx_desc_count & (pdata->rx_desc_count - 1)) {
		dev_err(dev, "rx descriptor count (%d) is not valid\n",
			pdata->rx_desc_count);
		ret = -EINVAL;
		goto err_io;
	}

	/* Obtain the system clock setting */
	pdata->sysclock = devm_clk_get(dev, NULL);
	if (IS_ERR(pdata->sysclock)) {
		dev_err(dev, "devm_clk_get failed\n");
		ret = PTR_ERR(pdata->sysclock);
		goto err_io;
	}

	/* Obtain the mmio areas for the device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->xgmac_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->xgmac_regs)) {
		dev_err(dev, "xgmac ioremap failed\n");
		ret = PTR_ERR(pdata->xgmac_regs);
		goto err_io;
	}
	DBGPR("  xgmac_regs = %p\n", pdata->xgmac_regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pdata->xpcs_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->xpcs_regs)) {
		dev_err(dev, "xpcs ioremap failed\n");
		ret = PTR_ERR(pdata->xpcs_regs);
		goto err_io;
	}
	DBGPR("  xpcs_regs  = %p\n", pdata->xpcs_regs);

	/* Set the DMA mask */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	*(dev->dma_mask) = DMA_BIT_MASK(40);
	dev->coherent_dma_mask = DMA_BIT_MASK(40);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "platform_get_irq failed\n");
		goto err_io;
	}
	netdev->irq = ret;
	netdev->base_addr = (unsigned long)pdata->xgmac_regs;

	/* Set all the function pointers */
	xgbe_init_all_fptrs(pdata);
	hw_if = &pdata->hw_if;
	desc_if = &pdata->desc_if;

	/* Issue software reset to device */
	hw_if->exit(pdata);

	/* Populate the hardware features */
	xgbe_get_all_hw_features(pdata);

	/* Retrieve the MAC address */
	mac_addr = of_get_mac_address(dev->of_node);
	if (!mac_addr) {
		dev_err(dev, "invalid mac address for this device\n");
		ret = -EINVAL;
		goto err_io;
	}
	memcpy(netdev->dev_addr, mac_addr, netdev->addr_len);

	/* Retrieve the PHY mode - it must be "xgmii" */
	pdata->phy_mode = of_get_phy_mode(dev->of_node);
	if (pdata->phy_mode != PHY_INTERFACE_MODE_XGMII) {
		dev_err(dev, "invalid phy-mode specified for this device\n");
		ret = -EINVAL;
		goto err_io;
	}

	/* Set default configuration data */
	xgbe_default_config(pdata);

	/* Calculate the number of Tx and Rx rings to be created */
	pdata->tx_ring_count = min_t(unsigned int, num_online_cpus(),
				     pdata->hw_feat.tx_ch_cnt);
	if (netif_set_real_num_tx_queues(netdev, pdata->tx_ring_count)) {
		dev_err(dev, "error setting real tx queue count\n");
		goto err_io;
	}

	pdata->rx_ring_count = min_t(unsigned int,
				     netif_get_num_default_rss_queues(),
				     pdata->hw_feat.rx_ch_cnt);
	ret = netif_set_real_num_rx_queues(netdev, pdata->rx_ring_count);
	if (ret) {
		dev_err(dev, "error setting real rx queue count\n");
		goto err_io;
	}

	/* Allocate the rings for the DMA channels */
	pdata->channel = xgbe_alloc_rings(pdata);
	if (!pdata->channel) {
		dev_err(dev, "ring allocation failed\n");
		ret = -ENOMEM;
		goto err_io;
	}

	/* Prepare to regsiter with MDIO */
	pdata->mii_bus_id = kasprintf(GFP_KERNEL, "%s", pdev->name);
	if (!pdata->mii_bus_id) {
		dev_err(dev, "failed to allocate mii bus id\n");
		ret = -ENOMEM;
		goto err_io;
	}
	ret = xgbe_mdio_register(pdata);
	if (ret)
		goto err_bus_id;

	/* Set network and ethtool operations */
	netdev->netdev_ops = xgbe_get_netdev_ops();
	netdev->ethtool_ops = xgbe_get_ethtool_ops();

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

	netdev->vlan_features |= NETIF_F_SG |
				 NETIF_F_IP_CSUM |
				 NETIF_F_IPV6_CSUM |
				 NETIF_F_TSO |
				 NETIF_F_TSO6;

	netdev->features |= netdev->hw_features;
	pdata->netdev_features = netdev->features;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	xgbe_init_rx_coalesce(pdata);
	xgbe_init_tx_coalesce(pdata);

	netif_carrier_off(netdev);
	ret = register_netdev(netdev);
	if (ret) {
		dev_err(dev, "net device registration failed\n");
		goto err_reg_netdev;
	}

	xgbe_debugfs_init(pdata);

	netdev_notice(netdev, "net device enabled\n");

	DBGPR("<-- xgbe_probe\n");

	return 0;

err_reg_netdev:
	xgbe_mdio_unregister(pdata);

err_bus_id:
	kfree(pdata->mii_bus_id);

err_io:
	free_netdev(netdev);

err_alloc:
	dev_notice(dev, "net device not enabled\n");

	return ret;
}

static int xgbe_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	DBGPR("-->xgbe_remove\n");

	xgbe_debugfs_exit(pdata);

	unregister_netdev(netdev);

	xgbe_mdio_unregister(pdata);

	kfree(pdata->mii_bus_id);

	free_netdev(netdev);

	DBGPR("<--xgbe_remove\n");

	return 0;
}

#ifdef CONFIG_PM
static int xgbe_suspend(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	int ret;

	DBGPR("-->xgbe_suspend\n");

	if (!netif_running(netdev)) {
		DBGPR("<--xgbe_dev_suspend\n");
		return -EINVAL;
	}

	ret = xgbe_powerdown(netdev, XGMAC_DRIVER_CONTEXT);

	DBGPR("<--xgbe_suspend\n");

	return ret;
}

static int xgbe_resume(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	int ret;

	DBGPR("-->xgbe_resume\n");

	if (!netif_running(netdev)) {
		DBGPR("<--xgbe_dev_resume\n");
		return -EINVAL;
	}

	ret = xgbe_powerup(netdev, XGMAC_DRIVER_CONTEXT);

	DBGPR("<--xgbe_resume\n");

	return ret;
}
#endif /* CONFIG_PM */

static const struct of_device_id xgbe_of_match[] = {
	{ .compatible = "amd,xgbe-seattle-v1a", },
	{},
};

MODULE_DEVICE_TABLE(of, xgbe_of_match);
static SIMPLE_DEV_PM_OPS(xgbe_pm_ops, xgbe_suspend, xgbe_resume);

static struct platform_driver xgbe_driver = {
	.driver = {
		.name = "amd-xgbe",
		.of_match_table = xgbe_of_match,
		.pm = &xgbe_pm_ops,
	},
	.probe = xgbe_probe,
	.remove = xgbe_remove,
};

module_platform_driver(xgbe_driver);
