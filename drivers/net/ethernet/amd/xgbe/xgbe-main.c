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
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/property.h>
#include <linux/acpi.h>
#include <linux/mdio.h>

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

static const u32 xgbe_serdes_blwc[] = {
	XGBE_SPEED_1000_BLWC,
	XGBE_SPEED_2500_BLWC,
	XGBE_SPEED_10000_BLWC,
};

static const u32 xgbe_serdes_cdr_rate[] = {
	XGBE_SPEED_1000_CDR,
	XGBE_SPEED_2500_CDR,
	XGBE_SPEED_10000_CDR,
};

static const u32 xgbe_serdes_pq_skew[] = {
	XGBE_SPEED_1000_PQ,
	XGBE_SPEED_2500_PQ,
	XGBE_SPEED_10000_PQ,
};

static const u32 xgbe_serdes_tx_amp[] = {
	XGBE_SPEED_1000_TXAMP,
	XGBE_SPEED_2500_TXAMP,
	XGBE_SPEED_10000_TXAMP,
};

static const u32 xgbe_serdes_dfe_tap_cfg[] = {
	XGBE_SPEED_1000_DFE_TAP_CONFIG,
	XGBE_SPEED_2500_DFE_TAP_CONFIG,
	XGBE_SPEED_10000_DFE_TAP_CONFIG,
};

static const u32 xgbe_serdes_dfe_tap_ena[] = {
	XGBE_SPEED_1000_DFE_TAP_ENABLE,
	XGBE_SPEED_2500_DFE_TAP_ENABLE,
	XGBE_SPEED_10000_DFE_TAP_ENABLE,
};

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
	pdata->phy_speed = SPEED_UNKNOWN;
	pdata->power_down = 0;

	DBGPR("<--xgbe_default_config\n");
}

static void xgbe_init_all_fptrs(struct xgbe_prv_data *pdata)
{
	xgbe_init_function_ptrs_dev(&pdata->hw_if);
	xgbe_init_function_ptrs_phy(&pdata->phy_if);
	xgbe_init_function_ptrs_desc(&pdata->desc_if);
}

#ifdef CONFIG_ACPI
static int xgbe_acpi_support(struct xgbe_prv_data *pdata)
{
	struct device *dev = pdata->dev;
	u32 property;
	int ret;

	/* Obtain the system clock setting */
	ret = device_property_read_u32(dev, XGBE_ACPI_DMA_FREQ, &property);
	if (ret) {
		dev_err(dev, "unable to obtain %s property\n",
			XGBE_ACPI_DMA_FREQ);
		return ret;
	}
	pdata->sysclk_rate = property;

	/* Obtain the PTP clock setting */
	ret = device_property_read_u32(dev, XGBE_ACPI_PTP_FREQ, &property);
	if (ret) {
		dev_err(dev, "unable to obtain %s property\n",
			XGBE_ACPI_PTP_FREQ);
		return ret;
	}
	pdata->ptpclk_rate = property;

	return 0;
}
#else   /* CONFIG_ACPI */
static int xgbe_acpi_support(struct xgbe_prv_data *pdata)
{
	return -EINVAL;
}
#endif  /* CONFIG_ACPI */

#ifdef CONFIG_OF
static int xgbe_of_support(struct xgbe_prv_data *pdata)
{
	struct device *dev = pdata->dev;

	/* Obtain the system clock setting */
	pdata->sysclk = devm_clk_get(dev, XGBE_DMA_CLOCK);
	if (IS_ERR(pdata->sysclk)) {
		dev_err(dev, "dma devm_clk_get failed\n");
		return PTR_ERR(pdata->sysclk);
	}
	pdata->sysclk_rate = clk_get_rate(pdata->sysclk);

	/* Obtain the PTP clock setting */
	pdata->ptpclk = devm_clk_get(dev, XGBE_PTP_CLOCK);
	if (IS_ERR(pdata->ptpclk)) {
		dev_err(dev, "ptp devm_clk_get failed\n");
		return PTR_ERR(pdata->ptpclk);
	}
	pdata->ptpclk_rate = clk_get_rate(pdata->ptpclk);

	return 0;
}

static struct platform_device *xgbe_of_get_phy_pdev(struct xgbe_prv_data *pdata)
{
	struct device *dev = pdata->dev;
	struct device_node *phy_node;
	struct platform_device *phy_pdev;

	phy_node = of_parse_phandle(dev->of_node, "phy-handle", 0);
	if (phy_node) {
		/* Old style device tree:
		 *   The XGBE and PHY resources are separate
		 */
		phy_pdev = of_find_device_by_node(phy_node);
		of_node_put(phy_node);
	} else {
		/* New style device tree:
		 *   The XGBE and PHY resources are grouped together with
		 *   the PHY resources listed last
		 */
		get_device(dev);
		phy_pdev = pdata->pdev;
	}

	return phy_pdev;
}
#else   /* CONFIG_OF */
static int xgbe_of_support(struct xgbe_prv_data *pdata)
{
	return -EINVAL;
}

static struct platform_device *xgbe_of_get_phy_pdev(struct xgbe_prv_data *pdata)
{
	return NULL;
}
#endif  /* CONFIG_OF */

static unsigned int xgbe_resource_count(struct platform_device *pdev,
					unsigned int type)
{
	unsigned int count;
	int i;

	for (i = 0, count = 0; i < pdev->num_resources; i++) {
		struct resource *res = &pdev->resource[i];

		if (type == resource_type(res))
			count++;
	}

	return count;
}

static struct platform_device *xgbe_get_phy_pdev(struct xgbe_prv_data *pdata)
{
	struct platform_device *phy_pdev;

	if (pdata->use_acpi) {
		get_device(pdata->dev);
		phy_pdev = pdata->pdev;
	} else {
		phy_pdev = xgbe_of_get_phy_pdev(pdata);
	}

	return phy_pdev;
}

static int xgbe_probe(struct platform_device *pdev)
{
	struct xgbe_prv_data *pdata;
	struct net_device *netdev;
	struct device *dev = &pdev->dev, *phy_dev;
	struct platform_device *phy_pdev;
	struct resource *res;
	const char *phy_mode;
	unsigned int i, phy_memnum, phy_irqnum;
	enum dev_dma_attr attr;
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
	pdata->adev = ACPI_COMPANION(dev);
	pdata->dev = dev;
	platform_set_drvdata(pdev, netdev);

	spin_lock_init(&pdata->lock);
	mutex_init(&pdata->xpcs_mutex);
	mutex_init(&pdata->rss_mutex);
	spin_lock_init(&pdata->tstamp_lock);

	pdata->msg_enable = netif_msg_init(debug, default_msg_level);

	set_bit(XGBE_DOWN, &pdata->dev_state);

	/* Check if we should use ACPI or DT */
	pdata->use_acpi = dev->of_node ? 0 : 1;

	phy_pdev = xgbe_get_phy_pdev(pdata);
	if (!phy_pdev) {
		dev_err(dev, "unable to obtain phy device\n");
		ret = -EINVAL;
		goto err_phydev;
	}
	phy_dev = &phy_pdev->dev;

	if (pdev == phy_pdev) {
		/* New style device tree or ACPI:
		 *   The XGBE and PHY resources are grouped together with
		 *   the PHY resources listed last
		 */
		phy_memnum = xgbe_resource_count(pdev, IORESOURCE_MEM) - 3;
		phy_irqnum = xgbe_resource_count(pdev, IORESOURCE_IRQ) - 1;
	} else {
		/* Old style device tree:
		 *   The XGBE and PHY resources are separate
		 */
		phy_memnum = 0;
		phy_irqnum = 0;
	}

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

	/* Obtain the mmio areas for the device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->xgmac_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->xgmac_regs)) {
		dev_err(dev, "xgmac ioremap failed\n");
		ret = PTR_ERR(pdata->xgmac_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "xgmac_regs = %p\n", pdata->xgmac_regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pdata->xpcs_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->xpcs_regs)) {
		dev_err(dev, "xpcs ioremap failed\n");
		ret = PTR_ERR(pdata->xpcs_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "xpcs_regs  = %p\n", pdata->xpcs_regs);

	res = platform_get_resource(phy_pdev, IORESOURCE_MEM, phy_memnum++);
	pdata->rxtx_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->rxtx_regs)) {
		dev_err(dev, "rxtx ioremap failed\n");
		ret = PTR_ERR(pdata->rxtx_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "rxtx_regs  = %p\n", pdata->rxtx_regs);

	res = platform_get_resource(phy_pdev, IORESOURCE_MEM, phy_memnum++);
	pdata->sir0_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->sir0_regs)) {
		dev_err(dev, "sir0 ioremap failed\n");
		ret = PTR_ERR(pdata->sir0_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "sir0_regs  = %p\n", pdata->sir0_regs);

	res = platform_get_resource(phy_pdev, IORESOURCE_MEM, phy_memnum++);
	pdata->sir1_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->sir1_regs)) {
		dev_err(dev, "sir1 ioremap failed\n");
		ret = PTR_ERR(pdata->sir1_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "sir1_regs  = %p\n", pdata->sir1_regs);

	/* Retrieve the MAC address */
	ret = device_property_read_u8_array(dev, XGBE_MAC_ADDR_PROPERTY,
					    pdata->mac_addr,
					    sizeof(pdata->mac_addr));
	if (ret || !is_valid_ether_addr(pdata->mac_addr)) {
		dev_err(dev, "invalid %s property\n", XGBE_MAC_ADDR_PROPERTY);
		if (!ret)
			ret = -EINVAL;
		goto err_io;
	}

	/* Retrieve the PHY mode - it must be "xgmii" */
	ret = device_property_read_string(dev, XGBE_PHY_MODE_PROPERTY,
					  &phy_mode);
	if (ret || strcmp(phy_mode, phy_modes(PHY_INTERFACE_MODE_XGMII))) {
		dev_err(dev, "invalid %s property\n", XGBE_PHY_MODE_PROPERTY);
		if (!ret)
			ret = -EINVAL;
		goto err_io;
	}
	pdata->phy_mode = PHY_INTERFACE_MODE_XGMII;

	/* Check for per channel interrupt support */
	if (device_property_present(dev, XGBE_DMA_IRQS_PROPERTY))
		pdata->per_channel_irq = 1;

	/* Retrieve the PHY speedset */
	ret = device_property_read_u32(phy_dev, XGBE_SPEEDSET_PROPERTY,
				       &pdata->speed_set);
	if (ret) {
		dev_err(dev, "invalid %s property\n", XGBE_SPEEDSET_PROPERTY);
		goto err_io;
	}

	switch (pdata->speed_set) {
	case XGBE_SPEEDSET_1000_10000:
	case XGBE_SPEEDSET_2500_10000:
		break;
	default:
		dev_err(dev, "invalid %s property\n", XGBE_SPEEDSET_PROPERTY);
		ret = -EINVAL;
		goto err_io;
	}

	/* Retrieve the PHY configuration properties */
	if (device_property_present(phy_dev, XGBE_BLWC_PROPERTY)) {
		ret = device_property_read_u32_array(phy_dev,
						     XGBE_BLWC_PROPERTY,
						     pdata->serdes_blwc,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(dev, "invalid %s property\n",
				XGBE_BLWC_PROPERTY);
			goto err_io;
		}
	} else {
		memcpy(pdata->serdes_blwc, xgbe_serdes_blwc,
		       sizeof(pdata->serdes_blwc));
	}

	if (device_property_present(phy_dev, XGBE_CDR_RATE_PROPERTY)) {
		ret = device_property_read_u32_array(phy_dev,
						     XGBE_CDR_RATE_PROPERTY,
						     pdata->serdes_cdr_rate,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(dev, "invalid %s property\n",
				XGBE_CDR_RATE_PROPERTY);
			goto err_io;
		}
	} else {
		memcpy(pdata->serdes_cdr_rate, xgbe_serdes_cdr_rate,
		       sizeof(pdata->serdes_cdr_rate));
	}

	if (device_property_present(phy_dev, XGBE_PQ_SKEW_PROPERTY)) {
		ret = device_property_read_u32_array(phy_dev,
						     XGBE_PQ_SKEW_PROPERTY,
						     pdata->serdes_pq_skew,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(dev, "invalid %s property\n",
				XGBE_PQ_SKEW_PROPERTY);
			goto err_io;
		}
	} else {
		memcpy(pdata->serdes_pq_skew, xgbe_serdes_pq_skew,
		       sizeof(pdata->serdes_pq_skew));
	}

	if (device_property_present(phy_dev, XGBE_TX_AMP_PROPERTY)) {
		ret = device_property_read_u32_array(phy_dev,
						     XGBE_TX_AMP_PROPERTY,
						     pdata->serdes_tx_amp,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(dev, "invalid %s property\n",
				XGBE_TX_AMP_PROPERTY);
			goto err_io;
		}
	} else {
		memcpy(pdata->serdes_tx_amp, xgbe_serdes_tx_amp,
		       sizeof(pdata->serdes_tx_amp));
	}

	if (device_property_present(phy_dev, XGBE_DFE_CFG_PROPERTY)) {
		ret = device_property_read_u32_array(phy_dev,
						     XGBE_DFE_CFG_PROPERTY,
						     pdata->serdes_dfe_tap_cfg,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(dev, "invalid %s property\n",
				XGBE_DFE_CFG_PROPERTY);
			goto err_io;
		}
	} else {
		memcpy(pdata->serdes_dfe_tap_cfg, xgbe_serdes_dfe_tap_cfg,
		       sizeof(pdata->serdes_dfe_tap_cfg));
	}

	if (device_property_present(phy_dev, XGBE_DFE_ENA_PROPERTY)) {
		ret = device_property_read_u32_array(phy_dev,
						     XGBE_DFE_ENA_PROPERTY,
						     pdata->serdes_dfe_tap_ena,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(dev, "invalid %s property\n",
				XGBE_DFE_ENA_PROPERTY);
			goto err_io;
		}
	} else {
		memcpy(pdata->serdes_dfe_tap_ena, xgbe_serdes_dfe_tap_ena,
		       sizeof(pdata->serdes_dfe_tap_ena));
	}

	/* Obtain device settings unique to ACPI/OF */
	if (pdata->use_acpi)
		ret = xgbe_acpi_support(pdata);
	else
		ret = xgbe_of_support(pdata);
	if (ret)
		goto err_io;

	/* Set the DMA coherency values */
	attr = device_get_dma_attr(dev);
	if (attr == DEV_DMA_NOT_SUPPORTED) {
		dev_err(dev, "DMA is not supported");
		goto err_io;
	}
	pdata->coherent = (attr == DEV_DMA_COHERENT);
	if (pdata->coherent) {
		pdata->axdomain = XGBE_DMA_OS_AXDOMAIN;
		pdata->arcache = XGBE_DMA_OS_ARCACHE;
		pdata->awcache = XGBE_DMA_OS_AWCACHE;
	} else {
		pdata->axdomain = XGBE_DMA_SYS_AXDOMAIN;
		pdata->arcache = XGBE_DMA_SYS_ARCACHE;
		pdata->awcache = XGBE_DMA_SYS_AWCACHE;
	}

	/* Get the device interrupt */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "platform_get_irq 0 failed\n");
		goto err_io;
	}
	pdata->dev_irq = ret;

	/* Get the auto-negotiation interrupt */
	ret = platform_get_irq(phy_pdev, phy_irqnum++);
	if (ret < 0) {
		dev_err(dev, "platform_get_irq phy 0 failed\n");
		goto err_io;
	}
	pdata->an_irq = ret;

	netdev->irq = pdata->dev_irq;
	netdev->base_addr = (unsigned long)pdata->xgmac_regs;
	memcpy(netdev->dev_addr, pdata->mac_addr, netdev->addr_len);

	/* Set all the function pointers */
	xgbe_init_all_fptrs(pdata);

	/* Issue software reset to device */
	pdata->hw_if.exit(pdata);

	/* Populate the hardware features */
	xgbe_get_all_hw_features(pdata);

	/* Set default configuration data */
	xgbe_default_config(pdata);

	/* Set the DMA mask */
	ret = dma_set_mask_and_coherent(dev,
					DMA_BIT_MASK(pdata->hw_feat.dma_width));
	if (ret) {
		dev_err(dev, "dma_set_mask_and_coherent failed\n");
		goto err_io;
	}

	/* Calculate the number of Tx and Rx rings to be created
	 *  -Tx (DMA) Channels map 1-to-1 to Tx Queues so set
	 *   the number of Tx queues to the number of Tx channels
	 *   enabled
	 *  -Rx (DMA) Channels do not map 1-to-1 so use the actual
	 *   number of Rx queues
	 */
	pdata->tx_ring_count = min_t(unsigned int, num_online_cpus(),
				     pdata->hw_feat.tx_ch_cnt);
	pdata->tx_q_count = pdata->tx_ring_count;
	ret = netif_set_real_num_tx_queues(netdev, pdata->tx_ring_count);
	if (ret) {
		dev_err(dev, "error setting real tx queue count\n");
		goto err_io;
	}

	pdata->rx_ring_count = min_t(unsigned int,
				     netif_get_num_default_rss_queues(),
				     pdata->hw_feat.rx_ch_cnt);
	pdata->rx_q_count = pdata->hw_feat.rx_q_cnt;
	ret = netif_set_real_num_rx_queues(netdev, pdata->rx_ring_count);
	if (ret) {
		dev_err(dev, "error setting real rx queue count\n");
		goto err_io;
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
	pdata->phy_if.phy_init(pdata);

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

	/* Use default watchdog timeout */
	netdev->watchdog_timeo = 0;

	xgbe_init_rx_coalesce(pdata);
	xgbe_init_tx_coalesce(pdata);

	netif_carrier_off(netdev);
	ret = register_netdev(netdev);
	if (ret) {
		dev_err(dev, "net device registration failed\n");
		goto err_io;
	}

	/* Create the PHY/ANEG name based on netdev name */
	snprintf(pdata->an_name, sizeof(pdata->an_name) - 1, "%s-pcs",
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

	xgbe_ptp_register(pdata);

	xgbe_debugfs_init(pdata);

	platform_device_put(phy_pdev);

	netdev_notice(netdev, "net device enabled\n");

	DBGPR("<-- xgbe_probe\n");

	return 0;

err_wq:
	destroy_workqueue(pdata->dev_workqueue);

err_netdev:
	unregister_netdev(netdev);

err_io:
	platform_device_put(phy_pdev);

err_phydev:
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

	xgbe_ptp_unregister(pdata);

	flush_workqueue(pdata->an_workqueue);
	destroy_workqueue(pdata->an_workqueue);

	flush_workqueue(pdata->dev_workqueue);
	destroy_workqueue(pdata->dev_workqueue);

	unregister_netdev(netdev);

	free_netdev(netdev);

	DBGPR("<--xgbe_remove\n");

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int xgbe_suspend(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret = 0;

	DBGPR("-->xgbe_suspend\n");

	if (netif_running(netdev))
		ret = xgbe_powerdown(netdev, XGMAC_DRIVER_CONTEXT);

	pdata->lpm_ctrl = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	pdata->lpm_ctrl |= MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, pdata->lpm_ctrl);

	DBGPR("<--xgbe_suspend\n");

	return ret;
}

static int xgbe_resume(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret = 0;

	DBGPR("-->xgbe_resume\n");

	pdata->lpm_ctrl &= ~MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, pdata->lpm_ctrl);

	if (netif_running(netdev))
		ret = xgbe_powerup(netdev, XGMAC_DRIVER_CONTEXT);

	DBGPR("<--xgbe_resume\n");

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgbe_acpi_match[] = {
	{ "AMDI8001", 0 },
	{},
};

MODULE_DEVICE_TABLE(acpi, xgbe_acpi_match);
#endif

#ifdef CONFIG_OF
static const struct of_device_id xgbe_of_match[] = {
	{ .compatible = "amd,xgbe-seattle-v1a", },
	{},
};

MODULE_DEVICE_TABLE(of, xgbe_of_match);
#endif

static SIMPLE_DEV_PM_OPS(xgbe_pm_ops, xgbe_suspend, xgbe_resume);

static struct platform_driver xgbe_driver = {
	.driver = {
		.name = "amd-xgbe",
#ifdef CONFIG_ACPI
		.acpi_match_table = xgbe_acpi_match,
#endif
#ifdef CONFIG_OF
		.of_match_table = xgbe_of_match,
#endif
		.pm = &xgbe_pm_ops,
	},
	.probe = xgbe_probe,
	.remove = xgbe_remove,
};

module_platform_driver(xgbe_driver);
