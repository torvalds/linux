// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-am62.c - TI specific Glue layer for AM62 DWC3 USB Controller
 *
 * Copyright (C) 2022 Texas Instruments Incorporated - https://www.ti.com
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/pinctrl/consumer.h>

/* USB WRAPPER register offsets */
#define USBSS_PID			0x0
#define USBSS_OVERCURRENT_CTRL		0x4
#define USBSS_PHY_CONFIG		0x8
#define USBSS_PHY_TEST			0xc
#define USBSS_CORE_STAT			0x14
#define USBSS_HOST_VBUS_CTRL		0x18
#define USBSS_MODE_CONTROL		0x1c
#define USBSS_WAKEUP_CONFIG		0x30
#define USBSS_WAKEUP_STAT		0x34
#define USBSS_OVERRIDE_CONFIG		0x38
#define USBSS_IRQ_MISC_STATUS_RAW	0x430
#define USBSS_IRQ_MISC_STATUS		0x434
#define USBSS_IRQ_MISC_ENABLE_SET	0x438
#define USBSS_IRQ_MISC_ENABLE_CLR	0x43c
#define USBSS_IRQ_MISC_EOI		0x440
#define USBSS_INTR_TEST			0x490
#define USBSS_VBUS_FILTER		0x614
#define USBSS_VBUS_STAT			0x618
#define USBSS_DEBUG_CFG			0x708
#define USBSS_DEBUG_DATA		0x70c
#define USBSS_HOST_HUB_CTRL		0x714

/* PHY CONFIG register bits */
#define USBSS_PHY_VBUS_SEL_MASK		GENMASK(2, 1)
#define USBSS_PHY_VBUS_SEL_SHIFT	1
#define USBSS_PHY_LANE_REVERSE		BIT(0)

/* MODE CONTROL register bits */
#define USBSS_MODE_VALID	BIT(0)

/* WAKEUP CONFIG register bits */
#define USBSS_WAKEUP_CFG_OVERCURRENT_EN	BIT(3)
#define USBSS_WAKEUP_CFG_LINESTATE_EN	BIT(2)
#define USBSS_WAKEUP_CFG_SESSVALID_EN	BIT(1)
#define USBSS_WAKEUP_CFG_VBUSVALID_EN	BIT(0)

/* WAKEUP STAT register bits */
#define USBSS_WAKEUP_STAT_OVERCURRENT	BIT(4)
#define USBSS_WAKEUP_STAT_LINESTATE	BIT(3)
#define USBSS_WAKEUP_STAT_SESSVALID	BIT(2)
#define USBSS_WAKEUP_STAT_VBUSVALID	BIT(1)
#define USBSS_WAKEUP_STAT_CLR		BIT(0)

/* IRQ_MISC_STATUS_RAW register bits */
#define USBSS_IRQ_MISC_RAW_VBUSVALID	BIT(22)
#define USBSS_IRQ_MISC_RAW_SESSVALID	BIT(20)

/* IRQ_MISC_STATUS register bits */
#define USBSS_IRQ_MISC_VBUSVALID	BIT(22)
#define USBSS_IRQ_MISC_SESSVALID	BIT(20)

/* IRQ_MISC_ENABLE_SET register bits */
#define USBSS_IRQ_MISC_ENABLE_SET_VBUSVALID	BIT(22)
#define USBSS_IRQ_MISC_ENABLE_SET_SESSVALID	BIT(20)

/* IRQ_MISC_ENABLE_CLR register bits */
#define USBSS_IRQ_MISC_ENABLE_CLR_VBUSVALID	BIT(22)
#define USBSS_IRQ_MISC_ENABLE_CLR_SESSVALID	BIT(20)

/* IRQ_MISC_EOI register bits */
#define USBSS_IRQ_MISC_EOI_VECTOR	BIT(0)

/* VBUS_STAT register bits */
#define USBSS_VBUS_STAT_SESSVALID	BIT(2)
#define USBSS_VBUS_STAT_VBUSVALID	BIT(0)

/* Mask for PHY PLL REFCLK */
#define PHY_PLL_REFCLK_MASK	GENMASK(3, 0)

#define DWC3_AM62_AUTOSUSPEND_DELAY	100

struct dwc3_data {
	struct device *dev;
	void __iomem *usbss;
	struct clk *usb2_refclk;
	int rate_code;
	struct regmap *syscon;
	unsigned int offset;
	unsigned int vbus_divider;
};

static const int dwc3_ti_rate_table[] = {	/* in KHZ */
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

static inline u32 dwc3_ti_readl(struct dwc3_data *data, u32 offset)
{
	return readl((data->usbss) + offset);
}

static inline void dwc3_ti_writel(struct dwc3_data *data, u32 offset, u32 value)
{
	writel(value, (data->usbss) + offset);
}

static int phy_syscon_pll_refclk(struct dwc3_data *data)
{
	struct device *dev = data->dev;
	struct device_node *node = dev->of_node;
	struct of_phandle_args args;
	struct regmap *syscon;
	int ret;

	syscon = syscon_regmap_lookup_by_phandle(node, "ti,syscon-phy-pll-refclk");
	if (IS_ERR(syscon)) {
		dev_err(dev, "unable to get ti,syscon-phy-pll-refclk regmap\n");
		return PTR_ERR(syscon);
	}

	data->syscon = syscon;

	ret = of_parse_phandle_with_fixed_args(node, "ti,syscon-phy-pll-refclk", 1,
					       0, &args);
	if (ret)
		return ret;

	data->offset = args.args[0];

	ret = regmap_update_bits(data->syscon, data->offset, PHY_PLL_REFCLK_MASK, data->rate_code);
	if (ret) {
		dev_err(dev, "failed to set phy pll reference clock rate\n");
		return ret;
	}

	return 0;
}

static int dwc3_ti_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct dwc3_data *data;
	int i, ret;
	unsigned long rate;
	u32 reg;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	platform_set_drvdata(pdev, data);

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

	/* Calculate the rate code */
	rate = clk_get_rate(data->usb2_refclk);
	rate /= 1000;	// To KHz
	for (i = 0; i < ARRAY_SIZE(dwc3_ti_rate_table); i++) {
		if (dwc3_ti_rate_table[i] == rate)
			break;
	}

	if (i == ARRAY_SIZE(dwc3_ti_rate_table)) {
		dev_err(dev, "unsupported usb2_refclk rate: %lu KHz\n", rate);
		return -EINVAL;
	}

	data->rate_code = i;

	/* Read the syscon property and set the rate code */
	ret = phy_syscon_pll_refclk(data);
	if (ret)
		return ret;

	/* VBUS divider select */
	data->vbus_divider = device_property_read_bool(dev, "ti,vbus-divider");
	reg = dwc3_ti_readl(data, USBSS_PHY_CONFIG);
	if (data->vbus_divider)
		reg |= 1 << USBSS_PHY_VBUS_SEL_SHIFT;

	dwc3_ti_writel(data, USBSS_PHY_CONFIG, reg);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Don't ignore its dependencies with its children
	 */
	pm_suspend_ignore_children(dev, false);
	clk_prepare_enable(data->usb2_refclk);
	pm_runtime_get_noresume(dev);

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to create dwc3 core: %d\n", ret);
		goto err_pm_disable;
	}

	/* Set mode valid bit to indicate role is valid */
	reg = dwc3_ti_readl(data, USBSS_MODE_CONTROL);
	reg |= USBSS_MODE_VALID;
	dwc3_ti_writel(data, USBSS_MODE_CONTROL, reg);

	/* Setting up autosuspend */
	pm_runtime_set_autosuspend_delay(dev, DWC3_AM62_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return 0;

err_pm_disable:
	clk_disable_unprepare(data->usb2_refclk);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	return ret;
}

static int dwc3_ti_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);
	return 0;
}

static int dwc3_ti_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dwc3_data *data = platform_get_drvdata(pdev);
	u32 reg;

	device_for_each_child(dev, NULL, dwc3_ti_remove_core);

	/* Clear mode valid bit */
	reg = dwc3_ti_readl(data, USBSS_MODE_CONTROL);
	reg &= ~USBSS_MODE_VALID;
	dwc3_ti_writel(data, USBSS_MODE_CONTROL, reg);

	pm_runtime_put_sync(dev);
	clk_disable_unprepare(data->usb2_refclk);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int dwc3_ti_suspend_common(struct device *dev)
{
	struct dwc3_data *data = dev_get_drvdata(dev);

	clk_disable_unprepare(data->usb2_refclk);

	return 0;
}

static int dwc3_ti_resume_common(struct device *dev)
{
	struct dwc3_data *data = dev_get_drvdata(dev);

	clk_prepare_enable(data->usb2_refclk);

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(dwc3_ti_pm_ops, dwc3_ti_suspend_common,
			    dwc3_ti_resume_common, NULL);

#define DEV_PM_OPS	(&dwc3_ti_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static const struct of_device_id dwc3_ti_of_match[] = {
	{ .compatible = "ti,am62-usb"},
	{},
};
MODULE_DEVICE_TABLE(of, dwc3_ti_of_match);

static struct platform_driver dwc3_ti_driver = {
	.probe		= dwc3_ti_probe,
	.remove		= dwc3_ti_remove,
	.driver		= {
		.name	= "dwc3-am62",
		.pm	= DEV_PM_OPS,
		.of_match_table = dwc3_ti_of_match,
	},
};

module_platform_driver(dwc3_ti_driver);

MODULE_ALIAS("platform:dwc3-am62");
MODULE_AUTHOR("Aswath Govindraju <a-govindraju@ti.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 TI Glue Layer");
