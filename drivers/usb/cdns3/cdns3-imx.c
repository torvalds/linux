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
#define SW_RESET_MASK	(0x3f << 26)
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
#define DEV_INT_EN (3 << 8) /* DEV INT b9:8 */
#define HOST_INT1_EN (1 << 0) /* HOST INT b7:0 */

/* USB3_CORE_STATUS */
#define MDCTRL_CLK_STATUS	BIT(15)
#define DEV_POWER_ON_READY	BIT(13)
#define HOST_POWER_ON_READY	BIT(12)

/* USB3_SSPHY_STATUS */
#define CLK_VALID_MASK		(0x3f << 26)
#define CLK_VALID_COMPARE_BITS	(0xf << 28)
#define PHY_REFCLK_REQ		(1 << 0)

struct cdns_imx {
	struct device *dev;
	void __iomem *noncore;
	struct clk_bulk_data *clks;
	int num_clks;
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
	data->clks = (struct clk_bulk_data *)imx_cdns3_core_clks;
	ret = devm_clk_bulk_get(dev, data->num_clks, data->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(data->num_clks, data->clks);
	if (ret)
		return ret;

	ret = cdns_imx_noncore_init(data);
	if (ret)
		goto err;

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to create children: %d\n", ret);
		goto err;
	}

	return ret;

err:
	clk_bulk_disable_unprepare(data->num_clks, data->clks);
	return ret;
}

static int cdns_imx_remove_core(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int cdns_imx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_for_each_child(dev, NULL, cdns_imx_remove_core);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

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
	},
};
module_platform_driver(cdns_imx_driver);

MODULE_ALIAS("platform:cdns3-imx");
MODULE_AUTHOR("Peter Chen <peter.chen@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 i.MX Glue Layer");
