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
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/pinctrl/consumer.h>

#include "core.h"

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

/* CORE STAT register bits */
#define USBSS_CORE_OPERATIONAL_MODE_MASK	GENMASK(13, 12)
#define USBSS_CORE_OPERATIONAL_MODE_SHIFT	12

/* MODE CONTROL register bits */
#define USBSS_MODE_VALID	BIT(0)

/* WAKEUP CONFIG register bits */
#define USBSS_WAKEUP_CFG_OVERCURRENT_EN	BIT(3)
#define USBSS_WAKEUP_CFG_LINESTATE_EN	BIT(2)
#define USBSS_WAKEUP_CFG_SESSVALID_EN	BIT(1)
#define USBSS_WAKEUP_CFG_VBUSVALID_EN	BIT(0)

#define USBSS_WAKEUP_CFG_ALL	(USBSS_WAKEUP_CFG_VBUSVALID_EN | \
				 USBSS_WAKEUP_CFG_SESSVALID_EN | \
				 USBSS_WAKEUP_CFG_LINESTATE_EN | \
				 USBSS_WAKEUP_CFG_OVERCURRENT_EN)

#define USBSS_WAKEUP_CFG_NONE	0

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

/* USB_PHY_CTRL register bits in CTRL_MMR */
#define PHY_CORE_VOLTAGE_MASK	BIT(31)
#define PHY_PLL_REFCLK_MASK	GENMASK(3, 0)

#define DWC3_AM62_AUTOSUSPEND_DELAY	100

struct dwc3_am62 {
	struct device *dev;
	void __iomem *usbss;
	struct clk *usb2_refclk;
	int rate_code;
	struct regmap *syscon;
	unsigned int offset;
	unsigned int vbus_divider;
	u32 wakeup_stat;
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

static inline u32 dwc3_ti_readl(struct dwc3_am62 *am62, u32 offset)
{
	return readl((am62->usbss) + offset);
}

static inline void dwc3_ti_writel(struct dwc3_am62 *am62, u32 offset, u32 value)
{
	writel(value, (am62->usbss) + offset);
}

static int phy_syscon_pll_refclk(struct dwc3_am62 *am62)
{
	struct device *dev = am62->dev;
	struct device_node *node = dev->of_node;
	struct of_phandle_args args;
	struct regmap *syscon;
	int ret;

	syscon = syscon_regmap_lookup_by_phandle(node, "ti,syscon-phy-pll-refclk");
	if (IS_ERR(syscon)) {
		dev_err(dev, "unable to get ti,syscon-phy-pll-refclk regmap\n");
		return PTR_ERR(syscon);
	}

	am62->syscon = syscon;

	ret = of_parse_phandle_with_fixed_args(node, "ti,syscon-phy-pll-refclk", 1,
					       0, &args);
	if (ret)
		return ret;

	am62->offset = args.args[0];

	/* Core voltage. PHY_CORE_VOLTAGE bit Recommended to be 0 always */
	ret = regmap_update_bits(am62->syscon, am62->offset, PHY_CORE_VOLTAGE_MASK, 0);
	if (ret) {
		dev_err(dev, "failed to set phy core voltage\n");
		return ret;
	}

	ret = regmap_update_bits(am62->syscon, am62->offset, PHY_PLL_REFCLK_MASK, am62->rate_code);
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
	struct dwc3_am62 *am62;
	int i, ret;
	unsigned long rate;
	u32 reg;

	am62 = devm_kzalloc(dev, sizeof(*am62), GFP_KERNEL);
	if (!am62)
		return -ENOMEM;

	am62->dev = dev;
	platform_set_drvdata(pdev, am62);

	am62->usbss = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(am62->usbss)) {
		dev_err(dev, "can't map IOMEM resource\n");
		return PTR_ERR(am62->usbss);
	}

	am62->usb2_refclk = devm_clk_get(dev, "ref");
	if (IS_ERR(am62->usb2_refclk)) {
		dev_err(dev, "can't get usb2_refclk\n");
		return PTR_ERR(am62->usb2_refclk);
	}

	/* Calculate the rate code */
	rate = clk_get_rate(am62->usb2_refclk);
	rate /= 1000;	// To KHz
	for (i = 0; i < ARRAY_SIZE(dwc3_ti_rate_table); i++) {
		if (dwc3_ti_rate_table[i] == rate)
			break;
	}

	if (i == ARRAY_SIZE(dwc3_ti_rate_table)) {
		dev_err(dev, "unsupported usb2_refclk rate: %lu KHz\n", rate);
		return -EINVAL;
	}

	am62->rate_code = i;

	/* Read the syscon property and set the rate code */
	ret = phy_syscon_pll_refclk(am62);
	if (ret)
		return ret;

	/* VBUS divider select */
	am62->vbus_divider = device_property_read_bool(dev, "ti,vbus-divider");
	reg = dwc3_ti_readl(am62, USBSS_PHY_CONFIG);
	if (am62->vbus_divider)
		reg |= 1 << USBSS_PHY_VBUS_SEL_SHIFT;

	dwc3_ti_writel(am62, USBSS_PHY_CONFIG, reg);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Don't ignore its dependencies with its children
	 */
	pm_suspend_ignore_children(dev, false);
	clk_prepare_enable(am62->usb2_refclk);
	pm_runtime_get_noresume(dev);

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to create dwc3 core: %d\n", ret);
		goto err_pm_disable;
	}

	/* Set mode valid bit to indicate role is valid */
	reg = dwc3_ti_readl(am62, USBSS_MODE_CONTROL);
	reg |= USBSS_MODE_VALID;
	dwc3_ti_writel(am62, USBSS_MODE_CONTROL, reg);

	/* Device has capability to wakeup system from sleep */
	device_set_wakeup_capable(dev, true);
	ret = device_wakeup_enable(dev);
	if (ret)
		dev_err(dev, "couldn't enable device as a wakeup source: %d\n", ret);

	/* Setting up autosuspend */
	pm_runtime_set_autosuspend_delay(dev, DWC3_AM62_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return 0;

err_pm_disable:
	clk_disable_unprepare(am62->usb2_refclk);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	return ret;
}

static void dwc3_ti_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dwc3_am62 *am62 = platform_get_drvdata(pdev);
	u32 reg;

	pm_runtime_get_sync(dev);
	device_init_wakeup(dev, false);
	of_platform_depopulate(dev);

	/* Clear mode valid bit */
	reg = dwc3_ti_readl(am62, USBSS_MODE_CONTROL);
	reg &= ~USBSS_MODE_VALID;
	dwc3_ti_writel(am62, USBSS_MODE_CONTROL, reg);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
}

#ifdef CONFIG_PM
static int dwc3_ti_suspend_common(struct device *dev)
{
	struct dwc3_am62 *am62 = dev_get_drvdata(dev);
	u32 reg, current_prtcap_dir;

	if (device_may_wakeup(dev)) {
		reg = dwc3_ti_readl(am62, USBSS_CORE_STAT);
		current_prtcap_dir = (reg & USBSS_CORE_OPERATIONAL_MODE_MASK)
				     >> USBSS_CORE_OPERATIONAL_MODE_SHIFT;
		/* Set wakeup config enable bits */
		reg = dwc3_ti_readl(am62, USBSS_WAKEUP_CONFIG);
		if (current_prtcap_dir == DWC3_GCTL_PRTCAP_HOST) {
			reg = USBSS_WAKEUP_CFG_LINESTATE_EN | USBSS_WAKEUP_CFG_OVERCURRENT_EN;
		} else {
			reg = USBSS_WAKEUP_CFG_VBUSVALID_EN | USBSS_WAKEUP_CFG_SESSVALID_EN;
			/*
			 * Enable LINESTATE wake up only if connected to bus
			 * and in U2/L3 state else it causes spurious wake-up.
			 */
		}
		dwc3_ti_writel(am62, USBSS_WAKEUP_CONFIG, reg);
		/* clear wakeup status so we know what caused the wake up */
		dwc3_ti_writel(am62, USBSS_WAKEUP_STAT, USBSS_WAKEUP_STAT_CLR);
	}

	clk_disable_unprepare(am62->usb2_refclk);

	return 0;
}

static int dwc3_ti_resume_common(struct device *dev)
{
	struct dwc3_am62 *am62 = dev_get_drvdata(dev);
	u32 reg;

	clk_prepare_enable(am62->usb2_refclk);

	if (device_may_wakeup(dev)) {
		/* Clear wakeup config enable bits */
		dwc3_ti_writel(am62, USBSS_WAKEUP_CONFIG, USBSS_WAKEUP_CFG_NONE);
	}

	reg = dwc3_ti_readl(am62, USBSS_WAKEUP_STAT);
	am62->wakeup_stat = reg;

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
	.remove_new	= dwc3_ti_remove,
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
