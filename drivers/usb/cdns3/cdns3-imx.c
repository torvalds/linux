// SPDX-License-Identifier: GPL-2.0
/**
 * cdns3-imx.c - NXP i.MX specific Glue layer for Cadence USB Controller
 *
 * Copyright (C) 2019 NXP
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include "core.h"

#define USB3_CORE_CTRL1    0x00
#define USB3_CORE_CTRL2    0x04
#define USB3_INT_REG       0x08
#define USB3_CORE_STATUS   0x0c
#define XHCI_DEBUG_LINK_ST 0x10
#define XHCI_DEBUG_BUS     0x14
#define USB3_SSPHY_CTRL1   0x40
#define USB3_SSPHY_CTRL2   0x44
#define USB3_SSPHY_STATUS  0x4c
#define USB2_PHY_CTRL1     0x50
#define USB2_PHY_CTRL2     0x54
#define USB2_PHY_STATUS    0x5c

/* Register bits definition */

/* USB3_CORE_CTRL1 */
#define SW_RESET_MASK	GENMASK(31, 26)
#define PWR_SW_RESET	BIT(31)
#define APB_SW_RESET	BIT(30)
#define AXI_SW_RESET	BIT(29)
#define RW_SW_RESET	BIT(28)
#define PHY_SW_RESET	BIT(27)
#define PHYAHB_SW_RESET	BIT(26)
#define ALL_SW_RESET	(PWR_SW_RESET | APB_SW_RESET | AXI_SW_RESET | \
		RW_SW_RESET | PHY_SW_RESET | PHYAHB_SW_RESET)
#define OC_DISABLE	BIT(9)
#define MDCTRL_CLK_SEL	BIT(7)
#define MODE_STRAP_MASK	(0x7)
#define DEV_MODE	(1 << 2)
#define HOST_MODE	(1 << 1)
#define OTG_MODE	(1 << 0)

/* USB3_INT_REG */
#define CLK_125_REQ	BIT(29)
#define LPM_CLK_REQ	BIT(28)
#define DEVU3_WAEKUP_EN	BIT(14)
#define OTG_WAKEUP_EN	BIT(12)
#define DEV_INT_EN	(3 << 8) /* DEV INT b9:8 */
#define HOST_INT1_EN	(1 << 0) /* HOST INT b7:0 */

/* USB3_CORE_STATUS */
#define MDCTRL_CLK_STATUS	BIT(15)
#define DEV_POWER_ON_READY	BIT(13)
#define HOST_POWER_ON_READY	BIT(12)

/* USB3_SSPHY_STATUS */
#define CLK_VALID_MASK		(0x3f << 26)
#define CLK_VALID_COMPARE_BITS	(0xf << 28)
#define PHY_REFCLK_REQ		(1 << 0)

/* OTG registers definition */
#define OTGSTS		0x4
/* OTGSTS */
#define OTG_NRDY	BIT(11)

/* xHCI registers definition  */
#define XECP_PM_PMCSR		0x8018
#define XECP_AUX_CTRL_REG1	0x8120

/* Register bits definition */
/* XECP_AUX_CTRL_REG1 */
#define CFG_RXDET_P3_EN		BIT(15)

/* XECP_PM_PMCSR */
#define PS_MASK			GENMASK(1, 0)
#define PS_D0			0
#define PS_D1			1

struct cdns_imx {
	struct device *dev;
	void __iomem *noncore;
	struct clk_bulk_data *clks;
	int num_clks;
	struct platform_device *cdns3_pdev;
};

static inline u32 cdns_imx_readl(struct cdns_imx *data, u32 offset)
{
	return readl(data->noncore + offset);
}

static inline void cdns_imx_writel(struct cdns_imx *data, u32 offset, u32 value)
{
	writel(value, data->noncore + offset);
}

static const struct clk_bulk_data imx_cdns3_core_clks[] = {
	{ .id = "usb3_lpm_clk" },
	{ .id = "usb3_bus_clk" },
	{ .id = "usb3_aclk" },
	{ .id = "usb3_ipg_clk" },
	{ .id = "usb3_core_pclk" },
};

static int cdns_imx_noncore_init(struct cdns_imx *data)
{
	u32 value;
	int ret;
	struct device *dev = data->dev;

	cdns_imx_writel(data, USB3_SSPHY_STATUS, CLK_VALID_MASK);
	udelay(1);
	ret = readl_poll_timeout(data->noncore + USB3_SSPHY_STATUS, value,
		(value & CLK_VALID_COMPARE_BITS) == CLK_VALID_COMPARE_BITS,
		10, 100000);
	if (ret) {
		dev_err(dev, "wait clkvld timeout\n");
		return ret;
	}

	value = cdns_imx_readl(data, USB3_CORE_CTRL1);
	value |= ALL_SW_RESET;
	cdns_imx_writel(data, USB3_CORE_CTRL1, value);
	udelay(1);

	value = cdns_imx_readl(data, USB3_CORE_CTRL1);
	value = (value & ~MODE_STRAP_MASK) | OTG_MODE | OC_DISABLE;
	cdns_imx_writel(data, USB3_CORE_CTRL1, value);

	value = cdns_imx_readl(data, USB3_INT_REG);
	value |= HOST_INT1_EN | DEV_INT_EN;
	cdns_imx_writel(data, USB3_INT_REG, value);

	value = cdns_imx_readl(data, USB3_CORE_CTRL1);
	value &= ~ALL_SW_RESET;
	cdns_imx_writel(data, USB3_CORE_CTRL1, value);
	return ret;
}

static int cdns_imx_platform_suspend(struct device *dev,
	bool suspend, bool wakeup);
static struct cdns3_platform_data cdns_imx_pdata = {
	.platform_suspend = cdns_imx_platform_suspend,
	.quirks		  = CDNS3_DEFAULT_PM_RUNTIME_ALLOW,
};

static const struct of_dev_auxdata cdns_imx_auxdata[] = {
	{
		.compatible = "cdns,usb3",
		.platform_data = &cdns_imx_pdata,
	},
	{},
};

static int cdns_imx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct cdns_imx *data;
	int ret;

	if (!node)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	data->dev = dev;
	data->noncore = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->noncore)) {
		dev_err(dev, "can't map IOMEM resource\n");
		return PTR_ERR(data->noncore);
	}

	data->num_clks = ARRAY_SIZE(imx_cdns3_core_clks);
	data->clks = devm_kmemdup(dev, imx_cdns3_core_clks,
				sizeof(imx_cdns3_core_clks), GFP_KERNEL);
	if (!data->clks)
		return -ENOMEM;

	ret = devm_clk_bulk_get(dev, data->num_clks, data->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(data->num_clks, data->clks);
	if (ret)
		return ret;

	ret = cdns_imx_noncore_init(data);
	if (ret)
		goto err;

	ret = of_platform_populate(node, NULL, cdns_imx_auxdata, dev);
	if (ret) {
		dev_err(dev, "failed to create children: %d\n", ret);
		goto err;
	}

	device_set_wakeup_capable(dev, true);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return ret;
err:
	clk_bulk_disable_unprepare(data->num_clks, data->clks);
	return ret;
}

static int cdns_imx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_imx *data = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	of_platform_depopulate(dev);
	clk_bulk_disable_unprepare(data->num_clks, data->clks);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static void cdns3_set_wakeup(struct cdns_imx *data, bool enable)
{
	u32 value;

	value = cdns_imx_readl(data, USB3_INT_REG);
	if (enable)
		value |= OTG_WAKEUP_EN | DEVU3_WAEKUP_EN;
	else
		value &= ~(OTG_WAKEUP_EN | DEVU3_WAEKUP_EN);

	cdns_imx_writel(data, USB3_INT_REG, value);
}

static int cdns_imx_platform_suspend(struct device *dev,
		bool suspend, bool wakeup)
{
	struct cdns3 *cdns = dev_get_drvdata(dev);
	struct device *parent = dev->parent;
	struct cdns_imx *data = dev_get_drvdata(parent);
	void __iomem *otg_regs = (void __iomem *)(cdns->otg_regs);
	void __iomem *xhci_regs = cdns->xhci_regs;
	u32 value;
	int ret = 0;

	if (cdns->role != USB_ROLE_HOST)
		return 0;

	if (suspend) {
		/* SW request low power when all usb ports allow to it ??? */
		value = readl(xhci_regs + XECP_PM_PMCSR);
		value &= ~PS_MASK;
		value |= PS_D1;
		writel(value, xhci_regs + XECP_PM_PMCSR);

		/* mdctrl_clk_sel */
		value = cdns_imx_readl(data, USB3_CORE_CTRL1);
		value |= MDCTRL_CLK_SEL;
		cdns_imx_writel(data, USB3_CORE_CTRL1, value);

		/* wait for mdctrl_clk_status */
		value = cdns_imx_readl(data, USB3_CORE_STATUS);
		ret = readl_poll_timeout(data->noncore + USB3_CORE_STATUS, value,
			(value & MDCTRL_CLK_STATUS) == MDCTRL_CLK_STATUS,
			10, 100000);
		if (ret)
			dev_warn(parent, "wait mdctrl_clk_status timeout\n");

		/* wait lpm_clk_req to be 0 */
		value = cdns_imx_readl(data, USB3_INT_REG);
		ret = readl_poll_timeout(data->noncore + USB3_INT_REG, value,
			(value & LPM_CLK_REQ) != LPM_CLK_REQ,
			10, 100000);
		if (ret)
			dev_warn(parent, "wait lpm_clk_req timeout\n");

		/* wait phy_refclk_req to be 0 */
		value = cdns_imx_readl(data, USB3_SSPHY_STATUS);
		ret = readl_poll_timeout(data->noncore + USB3_SSPHY_STATUS, value,
			(value & PHY_REFCLK_REQ) != PHY_REFCLK_REQ,
			10, 100000);
		if (ret)
			dev_warn(parent, "wait phy_refclk_req timeout\n");

		cdns3_set_wakeup(data, wakeup);
	} else {
		cdns3_set_wakeup(data, false);

		/* SW request D0 */
		value = readl(xhci_regs + XECP_PM_PMCSR);
		value &= ~PS_MASK;
		value |= PS_D0;
		writel(value, xhci_regs + XECP_PM_PMCSR);

		/* clr CFG_RXDET_P3_EN */
		value = readl(xhci_regs + XECP_AUX_CTRL_REG1);
		value &= ~CFG_RXDET_P3_EN;
		writel(value, xhci_regs + XECP_AUX_CTRL_REG1);

		/* clear mdctrl_clk_sel */
		value = cdns_imx_readl(data, USB3_CORE_CTRL1);
		value &= ~MDCTRL_CLK_SEL;
		cdns_imx_writel(data, USB3_CORE_CTRL1, value);

		/* wait CLK_125_REQ to be 1 */
		value = cdns_imx_readl(data, USB3_INT_REG);
		ret = readl_poll_timeout(data->noncore + USB3_INT_REG, value,
			(value & CLK_125_REQ) == CLK_125_REQ,
			10, 100000);
		if (ret)
			dev_warn(parent, "wait CLK_125_REQ timeout\n");

		/* wait for mdctrl_clk_status is cleared */
		value = cdns_imx_readl(data, USB3_CORE_STATUS);
		ret = readl_poll_timeout(data->noncore + USB3_CORE_STATUS, value,
			(value & MDCTRL_CLK_STATUS) != MDCTRL_CLK_STATUS,
			10, 100000);
		if (ret)
			dev_warn(parent, "wait mdctrl_clk_status cleared timeout\n");

		/* Wait until OTG_NRDY is 0 */
		value = readl(otg_regs + OTGSTS);
		ret = readl_poll_timeout(otg_regs + OTGSTS, value,
			(value & OTG_NRDY) != OTG_NRDY,
			10, 100000);
		if (ret)
			dev_warn(parent, "wait OTG ready timeout\n");
	}

	return ret;

}

static int cdns_imx_resume(struct device *dev)
{
	struct cdns_imx *data = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(data->num_clks, data->clks);
}

static int cdns_imx_suspend(struct device *dev)
{
	struct cdns_imx *data = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(data->num_clks, data->clks);

	return 0;
}
#else
static int cdns_imx_platform_suspend(struct device *dev,
	bool suspend, bool wakeup)
{
	return 0;
}

#endif /* CONFIG_PM */

static const struct dev_pm_ops cdns_imx_pm_ops = {
	SET_RUNTIME_PM_OPS(cdns_imx_suspend, cdns_imx_resume, NULL)
};

static const struct of_device_id cdns_imx_of_match[] = {
	{ .compatible = "fsl,imx8qm-usb3", },
	{},
};
MODULE_DEVICE_TABLE(of, cdns_imx_of_match);

static struct platform_driver cdns_imx_driver = {
	.probe		= cdns_imx_probe,
	.remove		= cdns_imx_remove,
	.driver		= {
		.name	= "cdns3-imx",
		.of_match_table	= cdns_imx_of_match,
		.pm	= &cdns_imx_pm_ops,
	},
};
module_platform_driver(cdns_imx_driver);

MODULE_ALIAS("platform:cdns3-imx");
MODULE_AUTHOR("Peter Chen <peter.chen@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 i.MX Glue Layer");
