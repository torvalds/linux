// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
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
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/property.h>
#include <linux/acpi.h>
#include <linux/mdio.h>

#include "xgbe.h"
#include "xgbe-common.h"

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
		phy_pdev = pdata->platdev;
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
		phy_pdev = pdata->platdev;
	} else {
		phy_pdev = xgbe_of_get_phy_pdev(pdata);
	}

	return phy_pdev;
}

static int xgbe_platform_probe(struct platform_device *pdev)
{
	struct xgbe_prv_data *pdata;
	struct device *dev = &pdev->dev;
	struct platform_device *phy_pdev;
	const char *phy_mode;
	unsigned int phy_memnum, phy_irqnum;
	unsigned int dma_irqnum, dma_irqend;
	enum dev_dma_attr attr;
	int ret;

	pdata = xgbe_alloc_pdata(dev);
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		goto err_alloc;
	}

	pdata->platdev = pdev;
	pdata->adev = ACPI_COMPANION(dev);
	platform_set_drvdata(pdev, pdata);

	/* Check if we should use ACPI or DT */
	pdata->use_acpi = dev->of_node ? 0 : 1;

	/* Get the version data */
	pdata->vdata = (struct xgbe_version_data *)device_get_match_data(dev);

	phy_pdev = xgbe_get_phy_pdev(pdata);
	if (!phy_pdev) {
		dev_err(dev, "unable to obtain phy device\n");
		ret = -EINVAL;
		goto err_phydev;
	}
	pdata->phy_platdev = phy_pdev;
	pdata->phy_dev = &phy_pdev->dev;

	if (pdev == phy_pdev) {
		/* New style device tree or ACPI:
		 *   The XGBE and PHY resources are grouped together with
		 *   the PHY resources listed last
		 */
		phy_memnum = xgbe_resource_count(pdev, IORESOURCE_MEM) - 3;
		phy_irqnum = platform_irq_count(pdev) - 1;
		dma_irqnum = 1;
		dma_irqend = phy_irqnum;
	} else {
		/* Old style device tree:
		 *   The XGBE and PHY resources are separate
		 */
		phy_memnum = 0;
		phy_irqnum = 0;
		dma_irqnum = 1;
		dma_irqend = platform_irq_count(pdev);
	}

	/* Obtain the mmio areas for the device */
	pdata->xgmac_regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pdata->xgmac_regs)) {
		dev_err(dev, "xgmac ioremap failed\n");
		ret = PTR_ERR(pdata->xgmac_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "xgmac_regs = %p\n", pdata->xgmac_regs);

	pdata->xpcs_regs = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(pdata->xpcs_regs)) {
		dev_err(dev, "xpcs ioremap failed\n");
		ret = PTR_ERR(pdata->xpcs_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "xpcs_regs  = %p\n", pdata->xpcs_regs);

	pdata->rxtx_regs = devm_platform_ioremap_resource(phy_pdev,
							  phy_memnum++);
	if (IS_ERR(pdata->rxtx_regs)) {
		dev_err(dev, "rxtx ioremap failed\n");
		ret = PTR_ERR(pdata->rxtx_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "rxtx_regs  = %p\n", pdata->rxtx_regs);

	pdata->sir0_regs = devm_platform_ioremap_resource(phy_pdev,
							  phy_memnum++);
	if (IS_ERR(pdata->sir0_regs)) {
		dev_err(dev, "sir0 ioremap failed\n");
		ret = PTR_ERR(pdata->sir0_regs);
		goto err_io;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "sir0_regs  = %p\n", pdata->sir0_regs);

	pdata->sir1_regs = devm_platform_ioremap_resource(phy_pdev,
							  phy_memnum++);
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
	if (device_property_present(dev, XGBE_DMA_IRQS_PROPERTY)) {
		pdata->per_channel_irq = 1;
		pdata->channel_irq_mode = XGBE_IRQ_MODE_EDGE;
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
		ret = -ENODEV;
		goto err_io;
	}
	pdata->coherent = (attr == DEV_DMA_COHERENT);
	if (pdata->coherent) {
		pdata->arcr = XGBE_DMA_OS_ARCR;
		pdata->awcr = XGBE_DMA_OS_AWCR;
	} else {
		pdata->arcr = XGBE_DMA_SYS_ARCR;
		pdata->awcr = XGBE_DMA_SYS_AWCR;
	}

	/* Set the maximum fifo amounts */
	pdata->tx_max_fifo_size = pdata->vdata->tx_max_fifo_size;
	pdata->rx_max_fifo_size = pdata->vdata->rx_max_fifo_size;

	/* Set the hardware channel and queue counts */
	xgbe_set_counts(pdata);

	/* Always have XGMAC and XPCS (auto-negotiation) interrupts */
	pdata->irq_count = 2;

	/* Get the device interrupt */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_io;
	pdata->dev_irq = ret;

	/* Get the per channel DMA interrupts */
	if (pdata->per_channel_irq) {
		unsigned int i, max = ARRAY_SIZE(pdata->channel_irq);

		for (i = 0; (i < max) && (dma_irqnum < dma_irqend); i++) {
			ret = platform_get_irq(pdata->platdev, dma_irqnum++);
			if (ret < 0)
				goto err_io;

			pdata->channel_irq[i] = ret;
		}

		pdata->channel_irq_count = max;

		pdata->irq_count += max;
	}

	/* Get the auto-negotiation interrupt */
	ret = platform_get_irq(phy_pdev, phy_irqnum++);
	if (ret < 0)
		goto err_io;
	pdata->an_irq = ret;

	/* Configure the netdev resource */
	ret = xgbe_config_netdev(pdata);
	if (ret)
		goto err_io;

	netdev_notice(pdata->netdev, "net device enabled\n");

	return 0;

err_io:
	platform_device_put(phy_pdev);

err_phydev:
	xgbe_free_pdata(pdata);

err_alloc:
	dev_notice(dev, "net device not enabled\n");

	return ret;
}

static void xgbe_platform_remove(struct platform_device *pdev)
{
	struct xgbe_prv_data *pdata = platform_get_drvdata(pdev);

	xgbe_deconfig_netdev(pdata);

	platform_device_put(pdata->phy_platdev);

	xgbe_free_pdata(pdata);
}

#ifdef CONFIG_PM_SLEEP
static int xgbe_platform_suspend(struct device *dev)
{
	struct xgbe_prv_data *pdata = dev_get_drvdata(dev);
	struct net_device *netdev = pdata->netdev;
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

static int xgbe_platform_resume(struct device *dev)
{
	struct xgbe_prv_data *pdata = dev_get_drvdata(dev);
	struct net_device *netdev = pdata->netdev;
	int ret = 0;

	DBGPR("-->xgbe_resume\n");

	pdata->lpm_ctrl &= ~MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, pdata->lpm_ctrl);

	if (netif_running(netdev)) {
		ret = xgbe_powerup(netdev, XGMAC_DRIVER_CONTEXT);

		/* Schedule a restart in case the link or phy state changed
		 * while we were powered down.
		 */
		schedule_work(&pdata->restart_work);
	}

	DBGPR("<--xgbe_resume\n");

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static const struct xgbe_version_data xgbe_v1 = {
	.init_function_ptrs_phy_impl	= xgbe_init_function_ptrs_phy_v1,
	.xpcs_access			= XGBE_XPCS_ACCESS_V1,
	.tx_max_fifo_size		= 81920,
	.rx_max_fifo_size		= 81920,
	.tx_tstamp_workaround		= 1,
};

static const struct acpi_device_id xgbe_acpi_match[] = {
	{ .id = "AMDI8001",
	  .driver_data = (kernel_ulong_t)&xgbe_v1 },
	{},
};

MODULE_DEVICE_TABLE(acpi, xgbe_acpi_match);

static const struct of_device_id xgbe_of_match[] = {
	{ .compatible = "amd,xgbe-seattle-v1a",
	  .data = &xgbe_v1 },
	{},
};

MODULE_DEVICE_TABLE(of, xgbe_of_match);

static SIMPLE_DEV_PM_OPS(xgbe_platform_pm_ops,
			 xgbe_platform_suspend, xgbe_platform_resume);

static struct platform_driver xgbe_driver = {
	.driver = {
		.name = XGBE_DRV_NAME,
		.acpi_match_table = xgbe_acpi_match,
		.of_match_table = xgbe_of_match,
		.pm = &xgbe_platform_pm_ops,
	},
	.probe = xgbe_platform_probe,
	.remove = xgbe_platform_remove,
};

int xgbe_platform_init(void)
{
	return platform_driver_register(&xgbe_driver);
}

void xgbe_platform_exit(void)
{
	platform_driver_unregister(&xgbe_driver);
}
