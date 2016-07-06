/**
 * dwc3-st.c Support for dwc3 platform devices on ST Microelectronics platforms
 *
 * This is a small driver for the dwc3 to provide the glue logic
 * to configure the controller. Tested on STi platforms.
 *
 * Copyright (C) 2014 Stmicroelectronics
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 * Contributors: Aymen Bouattay <aymen.bouattay@st.com>
 *               Peter Griffin <peter.griffin@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Inspired by dwc3-omap.c and dwc3-exynos.c.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/of.h>

#include "core.h"
#include "io.h"

/* glue registers */
#define CLKRST_CTRL		0x00
#define AUX_CLK_EN		BIT(0)
#define SW_PIPEW_RESET_N	BIT(4)
#define EXT_CFG_RESET_N		BIT(8)
/*
 * 1'b0 : The host controller complies with the xHCI revision 0.96
 * 1'b1 : The host controller complies with the xHCI revision 1.0
 */
#define XHCI_REVISION		BIT(12)

#define USB2_VBUS_MNGMNT_SEL1	0x2C
/*
 * For all fields in USB2_VBUS_MNGMNT_SEL1
 * 2’b00 : Override value from Reg 0x30 is selected
 * 2’b01 : utmiotg_<signal_name> from usb3_top is selected
 * 2’b10 : pipew_<signal_name> from PIPEW instance is selected
 * 2’b11 : value is 1'b0
 */
#define USB2_VBUS_REG30		0x0
#define USB2_VBUS_UTMIOTG	0x1
#define USB2_VBUS_PIPEW		0x2
#define USB2_VBUS_ZERO		0x3

#define SEL_OVERRIDE_VBUSVALID(n)	(n << 0)
#define SEL_OVERRIDE_POWERPRESENT(n)	(n << 4)
#define SEL_OVERRIDE_BVALID(n)		(n << 8)

/* Static DRD configuration */
#define USB3_CONTROL_MASK		0xf77

#define USB3_DEVICE_NOT_HOST		BIT(0)
#define USB3_FORCE_VBUSVALID		BIT(1)
#define USB3_DELAY_VBUSVALID		BIT(2)
#define USB3_SEL_FORCE_OPMODE		BIT(4)
#define USB3_FORCE_OPMODE(n)		(n << 5)
#define USB3_SEL_FORCE_DPPULLDOWN2	BIT(8)
#define USB3_FORCE_DPPULLDOWN2		BIT(9)
#define USB3_SEL_FORCE_DMPULLDOWN2	BIT(10)
#define USB3_FORCE_DMPULLDOWN2		BIT(11)

/**
 * struct st_dwc3 - dwc3-st driver private structure
 * @dev:		device pointer
 * @glue_base:		ioaddr for the glue registers
 * @regmap:		regmap pointer for getting syscfg
 * @syscfg_reg_off:	usb syscfg control offset
 * @dr_mode:		drd static host/device config
 * @rstc_pwrdn:		rest controller for powerdown signal
 * @rstc_rst:		reset controller for softreset signal
 */

struct st_dwc3 {
	struct device *dev;
	void __iomem *glue_base;
	struct regmap *regmap;
	int syscfg_reg_off;
	enum usb_dr_mode dr_mode;
	struct reset_control *rstc_pwrdn;
	struct reset_control *rstc_rst;
};

static inline u32 st_dwc3_readl(void __iomem *base, u32 offset)
{
	return readl_relaxed(base + offset);
}

static inline void st_dwc3_writel(void __iomem *base, u32 offset, u32 value)
{
	writel_relaxed(value, base + offset);
}

/**
 * st_dwc3_drd_init: program the port
 * @dwc3_data: driver private structure
 * Description: this function is to program the port as either host or device
 * according to the static configuration passed from devicetree.
 * OTG and dual role are not yet supported!
 */
static int st_dwc3_drd_init(struct st_dwc3 *dwc3_data)
{
	u32 val;
	int err;

	err = regmap_read(dwc3_data->regmap, dwc3_data->syscfg_reg_off, &val);
	if (err)
		return err;

	val &= USB3_CONTROL_MASK;

	switch (dwc3_data->dr_mode) {
	case USB_DR_MODE_PERIPHERAL:

		val &= ~(USB3_DELAY_VBUSVALID
			| USB3_SEL_FORCE_OPMODE | USB3_FORCE_OPMODE(0x3)
			| USB3_SEL_FORCE_DPPULLDOWN2 | USB3_FORCE_DPPULLDOWN2
			| USB3_SEL_FORCE_DMPULLDOWN2 | USB3_FORCE_DMPULLDOWN2);

		/*
		 * USB3_PORT2_FORCE_VBUSVALID When '1' and when
		 * USB3_PORT2_DEVICE_NOT_HOST = 1, forces VBUSVLDEXT2 input
		 * of the pico PHY to 1.
		 */

		val |= USB3_DEVICE_NOT_HOST | USB3_FORCE_VBUSVALID;
		break;

	case USB_DR_MODE_HOST:

		val &= ~(USB3_DEVICE_NOT_HOST | USB3_FORCE_VBUSVALID
			| USB3_SEL_FORCE_OPMODE	| USB3_FORCE_OPMODE(0x3)
			| USB3_SEL_FORCE_DPPULLDOWN2 | USB3_FORCE_DPPULLDOWN2
			| USB3_SEL_FORCE_DMPULLDOWN2 | USB3_FORCE_DMPULLDOWN2);

		/*
		 * USB3_DELAY_VBUSVALID is ANDed with USB_C_VBUSVALID. Thus,
		 * when set to ‘0‘, it can delay the arrival of VBUSVALID
		 * information to VBUSVLDEXT2 input of the pico PHY.
		 * We don't want to do that so we set the bit to '1'.
		 */

		val |= USB3_DELAY_VBUSVALID;
		break;

	default:
		dev_err(dwc3_data->dev, "Unsupported mode of operation %d\n",
			dwc3_data->dr_mode);
		return -EINVAL;
	}

	return regmap_write(dwc3_data->regmap, dwc3_data->syscfg_reg_off, val);
}

/**
 * st_dwc3_init: init the controller via glue logic
 * @dwc3_data: driver private structure
 */
static void st_dwc3_init(struct st_dwc3 *dwc3_data)
{
	u32 reg = st_dwc3_readl(dwc3_data->glue_base, CLKRST_CTRL);

	reg |= AUX_CLK_EN | EXT_CFG_RESET_N | XHCI_REVISION;
	reg &= ~SW_PIPEW_RESET_N;
	st_dwc3_writel(dwc3_data->glue_base, CLKRST_CTRL, reg);

	/* configure mux for vbus, powerpresent and bvalid signals */
	reg = st_dwc3_readl(dwc3_data->glue_base, USB2_VBUS_MNGMNT_SEL1);

	reg |= SEL_OVERRIDE_VBUSVALID(USB2_VBUS_UTMIOTG) |
		SEL_OVERRIDE_POWERPRESENT(USB2_VBUS_UTMIOTG) |
		SEL_OVERRIDE_BVALID(USB2_VBUS_UTMIOTG);

	st_dwc3_writel(dwc3_data->glue_base, USB2_VBUS_MNGMNT_SEL1, reg);

	reg = st_dwc3_readl(dwc3_data->glue_base, CLKRST_CTRL);
	reg |= SW_PIPEW_RESET_N;
	st_dwc3_writel(dwc3_data->glue_base, CLKRST_CTRL, reg);
}

static int st_dwc3_probe(struct platform_device *pdev)
{
	struct st_dwc3 *dwc3_data;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node, *child;
	struct platform_device *child_pdev;
	struct regmap *regmap;
	int ret;

	dwc3_data = devm_kzalloc(dev, sizeof(*dwc3_data), GFP_KERNEL);
	if (!dwc3_data)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg-glue");
	dwc3_data->glue_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dwc3_data->glue_base))
		return PTR_ERR(dwc3_data->glue_base);

	regmap = syscon_regmap_lookup_by_phandle(node, "st,syscfg");
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	dma_set_coherent_mask(dev, dev->coherent_dma_mask);
	dwc3_data->dev = dev;
	dwc3_data->regmap = regmap;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "syscfg-reg");
	if (!res) {
		ret = -ENXIO;
		goto undo_platform_dev_alloc;
	}

	dwc3_data->syscfg_reg_off = res->start;

	dev_vdbg(&pdev->dev, "glue-logic addr 0x%p, syscfg-reg offset 0x%x\n",
		 dwc3_data->glue_base, dwc3_data->syscfg_reg_off);

	dwc3_data->rstc_pwrdn = devm_reset_control_get(dev, "powerdown");
	if (IS_ERR(dwc3_data->rstc_pwrdn)) {
		dev_err(&pdev->dev, "could not get power controller\n");
		ret = PTR_ERR(dwc3_data->rstc_pwrdn);
		goto undo_platform_dev_alloc;
	}

	/* Manage PowerDown */
	reset_control_deassert(dwc3_data->rstc_pwrdn);

	dwc3_data->rstc_rst = devm_reset_control_get(dev, "softreset");
	if (IS_ERR(dwc3_data->rstc_rst)) {
		dev_err(&pdev->dev, "could not get reset controller\n");
		ret = PTR_ERR(dwc3_data->rstc_rst);
		goto undo_powerdown;
	}

	/* Manage SoftReset */
	reset_control_deassert(dwc3_data->rstc_rst);

	child = of_get_child_by_name(node, "dwc3");
	if (!child) {
		dev_err(&pdev->dev, "failed to find dwc3 core node\n");
		ret = -ENODEV;
		goto undo_softreset;
	}

	/* Allocate and initialize the core */
	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to add dwc3 core\n");
		goto undo_softreset;
	}

	child_pdev = of_find_device_by_node(child);
	if (!child_pdev) {
		dev_err(dev, "failed to find dwc3 core device\n");
		ret = -ENODEV;
		goto undo_softreset;
	}

	dwc3_data->dr_mode = usb_get_dr_mode(&child_pdev->dev);

	/*
	 * Configure the USB port as device or host according to the static
	 * configuration passed from DT.
	 * DRD is the only mode currently supported so this will be enhanced
	 * as soon as OTG is available.
	 */
	ret = st_dwc3_drd_init(dwc3_data);
	if (ret) {
		dev_err(dev, "drd initialisation failed\n");
		goto undo_softreset;
	}

	/* ST glue logic init */
	st_dwc3_init(dwc3_data);

	platform_set_drvdata(pdev, dwc3_data);
	return 0;

undo_softreset:
	reset_control_assert(dwc3_data->rstc_rst);
undo_powerdown:
	reset_control_assert(dwc3_data->rstc_pwrdn);
undo_platform_dev_alloc:
	platform_device_put(pdev);
	return ret;
}

static int st_dwc3_remove(struct platform_device *pdev)
{
	struct st_dwc3 *dwc3_data = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);

	reset_control_assert(dwc3_data->rstc_pwrdn);
	reset_control_assert(dwc3_data->rstc_rst);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int st_dwc3_suspend(struct device *dev)
{
	struct st_dwc3 *dwc3_data = dev_get_drvdata(dev);

	reset_control_assert(dwc3_data->rstc_pwrdn);
	reset_control_assert(dwc3_data->rstc_rst);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int st_dwc3_resume(struct device *dev)
{
	struct st_dwc3 *dwc3_data = dev_get_drvdata(dev);
	int ret;

	pinctrl_pm_select_default_state(dev);

	reset_control_deassert(dwc3_data->rstc_pwrdn);
	reset_control_deassert(dwc3_data->rstc_rst);

	ret = st_dwc3_drd_init(dwc3_data);
	if (ret) {
		dev_err(dev, "drd initialisation failed\n");
		return ret;
	}

	/* ST glue logic init */
	st_dwc3_init(dwc3_data);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(st_dwc3_dev_pm_ops, st_dwc3_suspend, st_dwc3_resume);

static const struct of_device_id st_dwc3_match[] = {
	{ .compatible = "st,stih407-dwc3" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, st_dwc3_match);

static struct platform_driver st_dwc3_driver = {
	.probe = st_dwc3_probe,
	.remove = st_dwc3_remove,
	.driver = {
		.name = "usb-st-dwc3",
		.of_match_table = st_dwc3_match,
		.pm = &st_dwc3_dev_pm_ops,
	},
};

module_platform_driver(st_dwc3_driver);

MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_DESCRIPTION("DesignWare USB3 STi Glue Layer");
MODULE_LICENSE("GPL v2");
