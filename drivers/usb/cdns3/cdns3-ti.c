// SPDX-License-Identifier: GPL-2.0
/**
 * cdns3-ti.c - TI specific Glue layer for Cadence USB Controller
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
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
#include <linux/pm_runtime.h>

/* USB Wrapper register offsets */
#define USBSS_PID		0x0
#define	USBSS_W1		0x4
#define USBSS_STATIC_CONFIG	0x8
#define USBSS_PHY_TEST		0xc
#define	USBSS_DEBUG_CTRL	0x10
#define	USBSS_DEBUG_INFO	0x14
#define	USBSS_DEBUG_LINK_STATE	0x18
#define	USBSS_DEVICE_CTRL	0x1c

/* Wrapper 1 register bits */
#define USBSS_W1_PWRUP_RST		BIT(0)
#define USBSS_W1_OVERCURRENT_SEL	BIT(8)
#define USBSS_W1_MODESTRAP_SEL		BIT(9)
#define USBSS_W1_OVERCURRENT		BIT(16)
#define USBSS_W1_MODESTRAP_MASK		GENMASK(18, 17)
#define USBSS_W1_MODESTRAP_SHIFT	17
#define USBSS_W1_USB2_ONLY		BIT(19)

/* Static config register bits */
#define USBSS1_STATIC_PLL_REF_SEL_MASK	GENMASK(8, 5)
#define USBSS1_STATIC_PLL_REF_SEL_SHIFT	5
#define USBSS1_STATIC_LOOPBACK_MODE_MASK	GENMASK(4, 3)
#define USBSS1_STATIC_LOOPBACK_MODE_SHIFT	3
#define USBSS1_STATIC_VBUS_SEL_MASK	GENMASK(2, 1)
#define USBSS1_STATIC_VBUS_SEL_SHIFT	1
#define USBSS1_STATIC_LANE_REVERSE	BIT(0)

/* Modestrap modes */
enum modestrap_mode { USBSS_MODESTRAP_MODE_NONE,
		      USBSS_MODESTRAP_MODE_HOST,
		      USBSS_MODESTRAP_MODE_PERIPHERAL};

struct cdns_ti {
	struct device *dev;
	void __iomem *usbss;
	int usb2_only:1;
	int vbus_divider:1;
	struct clk *usb2_refclk;
	struct clk *lpm_clk;
};

static const int cdns_ti_rate_table[] = {	/* in KHZ */
	9600,
	10000,
	12000,
	19200,
	20000,
	24000,
	25000,
	26000,
	38400,
	40000,
	58000,
	50000,
	52000,
};

static inline u32 cdns_ti_readl(struct cdns_ti *data, u32 offset)
{
	return readl(data->usbss + offset);
}

static inline void cdns_ti_writel(struct cdns_ti *data, u32 offset, u32 value)
{
	writel(value, data->usbss + offset);
}

static int cdns_ti_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct cdns_ti *data;
	int error;
	u32 reg;
	int rate_code, i;
	unsigned long rate;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->dev = dev;

	data->usbss = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->usbss)) {
		dev_err(dev, "can't map IOMEM resource\n");
		return PTR_ERR(data->usbss);
	}

	data->usb2_refclk = devm_clk_get(dev, "ref");
	if (IS_ERR(data->usb2_refclk)) {
		dev_err(dev, "can't get usb2_refclk\n");
		return PTR_ERR(data->usb2_refclk);
	}

	data->lpm_clk = devm_clk_get(dev, "lpm");
	if (IS_ERR(data->lpm_clk)) {
		dev_err(dev, "can't get lpm_clk\n");
		return PTR_ERR(data->lpm_clk);
	}

	rate = clk_get_rate(data->usb2_refclk);
	rate /= 1000;	/* To KHz */
	for (i = 0; i < ARRAY_SIZE(cdns_ti_rate_table); i++) {
		if (cdns_ti_rate_table[i] == rate)
			break;
	}

	if (i == ARRAY_SIZE(cdns_ti_rate_table)) {
		dev_err(dev, "unsupported usb2_refclk rate: %lu KHz\n", rate);
		return -EINVAL;
	}

	rate_code = i;

	pm_runtime_enable(dev);
	error = pm_runtime_get_sync(dev);
	if (error < 0) {
		dev_err(dev, "pm_runtime_get_sync failed: %d\n", error);
		goto err_get;
	}

	/* assert RESET */
	reg = cdns_ti_readl(data, USBSS_W1);
	reg &= ~USBSS_W1_PWRUP_RST;
	cdns_ti_writel(data, USBSS_W1, reg);

	/* set static config */
	reg = cdns_ti_readl(data, USBSS_STATIC_CONFIG);
	reg &= ~USBSS1_STATIC_PLL_REF_SEL_MASK;
	reg |= rate_code << USBSS1_STATIC_PLL_REF_SEL_SHIFT;

	reg &= ~USBSS1_STATIC_VBUS_SEL_MASK;
	data->vbus_divider = device_property_read_bool(dev, "ti,vbus-divider");
	if (data->vbus_divider)
		reg |= 1 << USBSS1_STATIC_VBUS_SEL_SHIFT;

	cdns_ti_writel(data, USBSS_STATIC_CONFIG, reg);
	reg = cdns_ti_readl(data, USBSS_STATIC_CONFIG);

	/* set USB2_ONLY mode if requested */
	reg = cdns_ti_readl(data, USBSS_W1);
	data->usb2_only = device_property_read_bool(dev, "ti,usb2-only");
	if (data->usb2_only)
		reg |= USBSS_W1_USB2_ONLY;

	/* set default modestrap */
	reg |= USBSS_W1_MODESTRAP_SEL;
	reg &= ~USBSS_W1_MODESTRAP_MASK;
	reg |= USBSS_MODESTRAP_MODE_NONE << USBSS_W1_MODESTRAP_SHIFT;
	cdns_ti_writel(data, USBSS_W1, reg);

	/* de-assert RESET */
	reg |= USBSS_W1_PWRUP_RST;
	cdns_ti_writel(data, USBSS_W1, reg);

	error = of_platform_populate(node, NULL, NULL, dev);
	if (error) {
		dev_err(dev, "failed to create children: %d\n", error);
		goto err;
	}

	return 0;

err:
	pm_runtime_put_sync(data->dev);
err_get:
	pm_runtime_disable(data->dev);

	return error;
}

static int cdns_ti_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int cdns_ti_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_for_each_child(dev, NULL, cdns_ti_remove_core);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id cdns_ti_of_match[] = {
	{ .compatible = "ti,j721e-usb", },
	{},
};
MODULE_DEVICE_TABLE(of, cdns_ti_of_match);

static struct platform_driver cdns_ti_driver = {
	.probe		= cdns_ti_probe,
	.remove		= cdns_ti_remove,
	.driver		= {
		.name	= "cdns3-ti",
		.of_match_table	= cdns_ti_of_match,
	},
};

module_platform_driver(cdns_ti_driver);

MODULE_ALIAS("platform:cdns3-ti");
MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 TI Glue Layer");
