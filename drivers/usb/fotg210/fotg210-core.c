// SPDX-License-Identifier: GPL-2.0+
/*
 * Central probing code for the FOTG210 dual-role controller.
 *
 * The role is selected once at probe time.  The driver does not attempt to
 * switch between the host and peripheral blocks while it is running.
 */
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/string_choices.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/otg.h>

#include "fotg210.h"

/* OTG control/status register. */
#define FOTG210_OTGCSR			0x80
#define FOTG210_OTGCSR_ID		BIT(21)
#define FOTG210_OTGCSR_CROLE		BIT(20)
#define FOTG210_OTGCSR_A_VBUS_VLD	BIT(19)
#define FOTG210_OTGCSR_A_BUS_DROP	BIT(5)
#define FOTG210_OTGCSR_A_BUS_REQ	BIT(4)
#define FOTG210_OTGCSR_B_HNP_EN		BIT(1)

/* OTG interrupt status and enable registers. */
#define FOTG210_OTGISR			0x84
#define FOTG210_OTGIEN			0x88

/* Global interrupt mask register; set bits mask the corresponding source. */
#define FOTG210_GINTM			0xc4
#define FOTG210_GINTM_INT_POLARITY	BIT(3)
#define FOTG210_GINTM_MHC_INT		BIT(2)
#define FOTG210_GINTM_MOTG_INT		BIT(1)
#define FOTG210_GINTM_MDEV_INT		BIT(0)

/* Gemini global miscellaneous-control register. */
#define GEMINI_GLOBAL_MISC_CTRL		0x30
#define GEMINI_MISC_USB0_WAKEUP		BIT(14)
#define GEMINI_MISC_USB1_WAKEUP		BIT(15)
#define GEMINI_MISC_USB0_VBUS_ON	BIT(22)
#define GEMINI_MISC_USB1_VBUS_ON	BIT(23)
#define GEMINI_MISC_USB0_MINI_B		BIT(29)
#define GEMINI_MISC_USB1_MINI_B		BIT(30)

static int fotg210_gemini_init(struct fotg210 *fotg)
{
	struct device *dev = fotg->dev;
	struct device_node *np = dev->of_node;
	bool wakeup = of_property_read_bool(np, "wakeup-source");
	u32 mask, val;
	int ret;

	fotg->map = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(fotg->map))
		return dev_err_probe(dev, PTR_ERR(fotg->map), "no syscon\n");

	if (fotg->res->start == 0x69000000) {
		fotg->port = GEMINI_PORT_1;
		mask = GEMINI_MISC_USB1_VBUS_ON | GEMINI_MISC_USB1_MINI_B |
		       GEMINI_MISC_USB1_WAKEUP;
		val = fotg->mode == USB_DR_MODE_HOST ?
		      GEMINI_MISC_USB1_VBUS_ON : GEMINI_MISC_USB1_MINI_B;
		if (wakeup)
			val |= GEMINI_MISC_USB1_WAKEUP;
	} else {
		fotg->port = GEMINI_PORT_0;
		mask = GEMINI_MISC_USB0_VBUS_ON | GEMINI_MISC_USB0_MINI_B |
		       GEMINI_MISC_USB0_WAKEUP;
		val = fotg->mode == USB_DR_MODE_HOST ?
		      GEMINI_MISC_USB0_VBUS_ON : GEMINI_MISC_USB0_MINI_B;
		if (wakeup)
			val |= GEMINI_MISC_USB0_WAKEUP;
	}

	ret = regmap_update_bits(fotg->map, GEMINI_GLOBAL_MISC_CTRL,
				 mask, val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize Gemini PHY\n");

	dev_info(dev, "initialized Gemini PHY in %s mode\n",
		 fotg->mode == USB_DR_MODE_HOST ? "host" : "gadget");
	return 0;
}

/**
 * fotg210_vbus() - enable or disable the A-device VBUS supply
 * @fotg: controller state
 * @enable: whether to drive VBUS
 */
void fotg210_vbus(struct fotg210 *fotg, bool enable)
{
	u32 mask = 0;
	u32 val;
	int ret;

	val = readl(fotg->base + FOTG210_OTGCSR);
	if (enable) {
		val &= ~FOTG210_OTGCSR_A_BUS_DROP;
		val |= FOTG210_OTGCSR_A_BUS_REQ;
	} else {
		val &= ~FOTG210_OTGCSR_A_BUS_REQ;
		val |= FOTG210_OTGCSR_A_BUS_DROP;
	}
	writel(val, fotg->base + FOTG210_OTGCSR);

	switch (fotg->port) {
	case GEMINI_PORT_0:
		mask = GEMINI_MISC_USB0_VBUS_ON;
		break;
	case GEMINI_PORT_1:
		mask = GEMINI_MISC_USB1_VBUS_ON;
		break;
	case GEMINI_PORT_NONE:
		break;
	}

	if (mask) {
		ret = regmap_update_bits(fotg->map, GEMINI_GLOBAL_MISC_CTRL,
					 mask, enable ? mask : 0);
		if (ret) {
			dev_err(fotg->dev, "failed to %s VBUS\n",
				str_enable_disable(enable));
			return;
		}
	}

	ret = readl_poll_timeout(fotg->base + FOTG210_OTGCSR, val,
				 enable == !!(val & FOTG210_OTGCSR_A_VBUS_VLD),
				 1000, 500000);
	if (ret)
		dev_warn(fotg->dev, "timeout waiting for VBUS to %s\n",
			 str_enable_disable(enable));
}

static void fotg210_init_host(struct fotg210 *fotg)
{
	u32 val;

	/* This driver keeps a fixed A-host role and does not negotiate HNP. */
	val = readl(fotg->base + FOTG210_OTGCSR);
	val &= ~FOTG210_OTGCSR_B_HNP_EN;
	writel(val, fotg->base + FOTG210_OTGCSR);

	/* Mask peripheral and OTG sources, enable the host source, active high. */
	writel(FOTG210_GINTM_MDEV_INT | FOTG210_GINTM_MOTG_INT |
	       FOTG210_GINTM_INT_POLARITY, fotg->base + FOTG210_GINTM);
	writel(0, fotg->base + FOTG210_OTGIEN);
	writel(readl(fotg->base + FOTG210_OTGISR),
	       fotg->base + FOTG210_OTGISR);

	/* Vendor trees cycle the A bus before starting the host controller. */
	fotg210_vbus(fotg, false);
	usleep_range(10000, 12000);
	fotg210_vbus(fotg, true);
	usleep_range(10000, 12000);
}

static void fotg210_init_peripheral(struct fotg210 *fotg)
{
	/* Mask host and OTG sources, enable the device source, active high. */
	writel(FOTG210_GINTM_MHC_INT | FOTG210_GINTM_MOTG_INT |
	       FOTG210_GINTM_INT_POLARITY, fotg->base + FOTG210_GINTM);
	writel(0, fotg->base + FOTG210_OTGIEN);
	writel(readl(fotg->base + FOTG210_OTGISR),
	       fotg->base + FOTG210_OTGISR);
}

static int fotg210_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reset_control *rst;
	struct fotg210 *fotg;
	u32 val;
	int ret;

	fotg = devm_kzalloc(dev, sizeof(*fotg), GFP_KERNEL);
	if (!fotg)
		return -ENOMEM;
	fotg->dev = dev;

	fotg->base = devm_platform_get_and_ioremap_resource(pdev, 0, &fotg->res);
	if (IS_ERR(fotg->base))
		return PTR_ERR(fotg->base);

	fotg->pclk = devm_clk_get_enabled(dev, "PCLK");
	if (IS_ERR(fotg->pclk))
		return dev_err_probe(dev, PTR_ERR(fotg->pclk),
				     "failed to enable PCLK\n");

	rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst),
				     "failed to get reset\n");
	ret = reset_control_reset(rst);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to reset controller\n");

	val = readl(fotg->base + FOTG210_OTGCSR);
	fotg->mode = usb_get_dr_mode(dev);
	/* Keep the historical fixed-host default for old device trees. */
	if (fotg->mode == USB_DR_MODE_UNKNOWN)
		fotg->mode = USB_DR_MODE_HOST;
	if (fotg->mode != USB_DR_MODE_HOST &&
	    fotg->mode != USB_DR_MODE_PERIPHERAL)
		return dev_err_probe(dev, -EINVAL,
				     "dr_mode must select host or peripheral\n");

	if (of_device_is_compatible(dev->of_node, "cortina,gemini-usb")) {
		fotg->is_fotg210 = true;
		ret = fotg210_gemini_init(fotg);
		if (ret)
			return ret;
	} else if (of_device_is_compatible(dev->of_node, "faraday,fotg210")) {
		fotg->is_fotg210 = true;
	}

	if (fotg->mode == USB_DR_MODE_PERIPHERAL) {
		if (!(val & FOTG210_OTGCSR_CROLE))
			dev_warn(dev, "controller does not report device role\n");
		if (!(val & FOTG210_OTGCSR_ID))
			dev_warn(dev, "controller does not report B-device role\n");
		fotg210_init_peripheral(fotg);
		return fotg210_udc_probe(pdev, fotg);
	}

	if (val & FOTG210_OTGCSR_CROLE)
		dev_warn(dev, "controller does not report host role\n");
	if (val & FOTG210_OTGCSR_ID)
		dev_warn(dev, "controller does not report A-device role\n");
	fotg210_init_host(fotg);
	ret = fotg210_hcd_probe(pdev, fotg);
	if (ret)
		fotg210_vbus(fotg, false);

	return ret;
}

static void fotg210_remove(struct platform_device *pdev)
{
	/* The subdrivers own drvdata, so recover the fixed role from firmware. */
	if (usb_get_dr_mode(&pdev->dev) == USB_DR_MODE_PERIPHERAL)
		fotg210_udc_remove(pdev);
	else
		fotg210_hcd_remove(pdev);
}

static void fotg210_shutdown(struct platform_device *pdev)
{
	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD) &&
	    usb_get_dr_mode(&pdev->dev) != USB_DR_MODE_PERIPHERAL)
		usb_hcd_platform_shutdown(pdev);
}

static int fotg210_suspend(struct device *dev)
{
	if (usb_get_dr_mode(dev) == USB_DR_MODE_PERIPHERAL)
		return 0;

	return fotg210_hcd_suspend(dev);
}

static int fotg210_resume(struct device *dev)
{
	if (usb_get_dr_mode(dev) == USB_DR_MODE_PERIPHERAL)
		return 0;

	return fotg210_hcd_resume(dev);
}

static DEFINE_SIMPLE_DEV_PM_OPS(fotg210_pm_ops, fotg210_suspend,
				fotg210_resume);

static const struct of_device_id fotg210_of_match[] = {
	{ .compatible = "faraday,fotg200" },
	{ .compatible = "faraday,fotg210" },
	{},
};
MODULE_DEVICE_TABLE(of, fotg210_of_match);

static struct platform_driver fotg210_driver = {
	.driver = {
		.name = "fotg210",
		.of_match_table = fotg210_of_match,
		.pm = pm_sleep_ptr(&fotg210_pm_ops),
	},
	.probe = fotg210_probe,
	.remove = fotg210_remove,
	.shutdown = fotg210_shutdown,
};

static int __init fotg210_init(void)
{
	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD) && !usb_disabled())
		fotg210_hcd_init();
	return platform_driver_register(&fotg210_driver);
}
module_init(fotg210_init);

static void __exit fotg210_cleanup(void)
{
	platform_driver_unregister(&fotg210_driver);
	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD))
		fotg210_hcd_cleanup();
}
module_exit(fotg210_cleanup);

MODULE_AUTHOR("Yuan-Hsin Chen, Feng-Hsin Chiang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FOTG210 Dual Role Controller Driver");
