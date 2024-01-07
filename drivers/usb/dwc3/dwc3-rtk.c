// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-rtk.c - Realtek DWC3 Specific Glue layer
 *
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/suspend.h>
#include <linux/sys_soc.h>
#include <linux/usb/otg.h>
#include <linux/usb/of.h>
#include <linux/usb/role.h>

#include "core.h"

#define WRAP_CTR_REG  0x0
#define DISABLE_MULTI_REQ BIT(1)
#define DESC_R2W_MULTI_DISABLE BIT(9)
#define FORCE_PIPE3_PHY_STATUS_TO_0 BIT(13)

#define WRAP_USB2_PHY_UTMI_REG 0x8
#define TXHSVM_EN BIT(3)

#define WRAP_PHY_PIPE_REG 0xC
#define RESET_DISABLE_PIPE3_P0 BIT(0)
#define CLOCK_ENABLE_FOR_PIPE3_PCLK BIT(1)

#define WRAP_USB_HMAC_CTR0_REG 0x60
#define U3PORT_DIS BIT(8)

#define WRAP_USB2_PHY_REG  0x70
#define USB2_PHY_EN_PHY_PLL_PORT0 BIT(12)
#define USB2_PHY_EN_PHY_PLL_PORT1 BIT(13)
#define USB2_PHY_SWITCH_MASK 0x707
#define USB2_PHY_SWITCH_DEVICE 0x0
#define USB2_PHY_SWITCH_HOST 0x606

#define WRAP_APHY_REG 0x128
#define USB3_MBIAS_ENABLE BIT(1)

/* pm control */
#define WRAP_USB_DBUS_PWR_CTRL_REG 0x160
#define USB_DBUS_PWR_CTRL_REG 0x0
#define DBUS_PWR_CTRL_EN BIT(0)

struct dwc3_rtk {
	struct device *dev;
	void __iomem *regs;
	size_t regs_size;
	void __iomem *pm_base;

	struct dwc3 *dwc;

	enum usb_role cur_role;
	struct usb_role_switch *role_switch;
};

static void switch_usb2_role(struct dwc3_rtk *rtk, enum usb_role role)
{
	void __iomem *reg;
	int val;

	reg = rtk->regs + WRAP_USB2_PHY_REG;
	val = ~USB2_PHY_SWITCH_MASK & readl(reg);

	switch (role) {
	case USB_ROLE_DEVICE:
		writel(USB2_PHY_SWITCH_DEVICE | val, reg);
		break;
	case USB_ROLE_HOST:
		writel(USB2_PHY_SWITCH_HOST | val, reg);
		break;
	default:
		dev_dbg(rtk->dev, "%s: role=%d\n", __func__, role);
		break;
	}
}

static void switch_dwc3_role(struct dwc3_rtk *rtk, enum usb_role role)
{
	if (!rtk->dwc->role_sw)
		return;

	usb_role_switch_set_role(rtk->dwc->role_sw, role);
}

static enum usb_role dwc3_rtk_get_role(struct dwc3_rtk *rtk)
{
	enum usb_role role;

	role = rtk->cur_role;

	if (rtk->dwc && rtk->dwc->role_sw)
		role = usb_role_switch_get_role(rtk->dwc->role_sw);
	else
		dev_dbg(rtk->dev, "%s not usb_role_switch role=%d\n", __func__, role);

	return role;
}

static void dwc3_rtk_set_role(struct dwc3_rtk *rtk, enum usb_role role)
{
	rtk->cur_role = role;

	switch_dwc3_role(rtk, role);
	mdelay(10);
	switch_usb2_role(rtk, role);
}

#if IS_ENABLED(CONFIG_USB_ROLE_SWITCH)
static int dwc3_usb_role_switch_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct dwc3_rtk *rtk = usb_role_switch_get_drvdata(sw);

	dwc3_rtk_set_role(rtk, role);

	return 0;
}

static enum usb_role dwc3_usb_role_switch_get(struct usb_role_switch *sw)
{
	struct dwc3_rtk *rtk = usb_role_switch_get_drvdata(sw);

	return dwc3_rtk_get_role(rtk);
}

static int dwc3_rtk_setup_role_switch(struct dwc3_rtk *rtk)
{
	struct usb_role_switch_desc dwc3_role_switch = {NULL};

	dwc3_role_switch.name = dev_name(rtk->dev);
	dwc3_role_switch.driver_data = rtk;
	dwc3_role_switch.allow_userspace_control = true;
	dwc3_role_switch.fwnode = dev_fwnode(rtk->dev);
	dwc3_role_switch.set = dwc3_usb_role_switch_set;
	dwc3_role_switch.get = dwc3_usb_role_switch_get;
	rtk->role_switch = usb_role_switch_register(rtk->dev, &dwc3_role_switch);
	if (IS_ERR(rtk->role_switch))
		return PTR_ERR(rtk->role_switch);

	return 0;
}

static int dwc3_rtk_remove_role_switch(struct dwc3_rtk *rtk)
{
	if (rtk->role_switch)
		usb_role_switch_unregister(rtk->role_switch);

	rtk->role_switch = NULL;

	return 0;
}
#else
#define dwc3_rtk_setup_role_switch(x) 0
#define dwc3_rtk_remove_role_switch(x) 0
#endif

static const char *const speed_names[] = {
	[USB_SPEED_UNKNOWN] = "UNKNOWN",
	[USB_SPEED_LOW] = "low-speed",
	[USB_SPEED_FULL] = "full-speed",
	[USB_SPEED_HIGH] = "high-speed",
	[USB_SPEED_WIRELESS] = "wireless",
	[USB_SPEED_SUPER] = "super-speed",
	[USB_SPEED_SUPER_PLUS] = "super-speed-plus",
};

static enum usb_device_speed __get_dwc3_maximum_speed(struct device_node *np)
{
	struct device_node *dwc3_np;
	const char *maximum_speed;
	int ret;

	dwc3_np = of_get_compatible_child(np, "snps,dwc3");
	if (!dwc3_np)
		return USB_SPEED_UNKNOWN;

	ret = of_property_read_string(dwc3_np, "maximum-speed", &maximum_speed);
	if (ret < 0)
		goto out;

	ret = match_string(speed_names, ARRAY_SIZE(speed_names), maximum_speed);

out:
	of_node_put(dwc3_np);

	return (ret < 0) ? USB_SPEED_UNKNOWN : ret;
}

static int dwc3_rtk_init(struct dwc3_rtk *rtk)
{
	struct device *dev = rtk->dev;
	void __iomem *reg;
	int val;
	enum usb_device_speed maximum_speed;
	const struct soc_device_attribute rtk_soc_kylin_a00[] = {
		{ .family = "Realtek Kylin", .revision = "A00", },
		{ /* empty */ } };
	const struct soc_device_attribute rtk_soc_hercules[] = {
		{ .family = "Realtek Hercules", }, { /* empty */ } };
	const struct soc_device_attribute rtk_soc_thor[] = {
		{ .family = "Realtek Thor", }, { /* empty */ } };

	if (soc_device_match(rtk_soc_kylin_a00)) {
		reg = rtk->regs + WRAP_CTR_REG;
		val = readl(reg);
		writel(DISABLE_MULTI_REQ | val, reg);
		dev_info(dev, "[bug fixed] 1295/1296 A00: add workaround to disable multiple request for D-Bus");
	}

	if (soc_device_match(rtk_soc_hercules)) {
		reg = rtk->regs + WRAP_USB2_PHY_REG;
		val = readl(reg);
		writel(USB2_PHY_EN_PHY_PLL_PORT1 | val, reg);
		dev_info(dev, "[bug fixed] 1395 add workaround to disable usb2 port 2 suspend!");
	}

	reg = rtk->regs + WRAP_USB2_PHY_UTMI_REG;
	val = readl(reg);
	writel(TXHSVM_EN | val, reg);

	maximum_speed = __get_dwc3_maximum_speed(dev->of_node);
	if (maximum_speed != USB_SPEED_UNKNOWN && maximum_speed <= USB_SPEED_HIGH) {
		if (soc_device_match(rtk_soc_thor)) {
			reg = rtk->regs + WRAP_USB_HMAC_CTR0_REG;
			val = readl(reg);
			writel(U3PORT_DIS | val, reg);
		} else {
			reg = rtk->regs + WRAP_CTR_REG;
			val = readl(reg);
			writel(FORCE_PIPE3_PHY_STATUS_TO_0 | val, reg);

			reg = rtk->regs + WRAP_PHY_PIPE_REG;
			val = ~CLOCK_ENABLE_FOR_PIPE3_PCLK & readl(reg);
			writel(RESET_DISABLE_PIPE3_P0 | val, reg);

			reg =  rtk->regs + WRAP_USB_HMAC_CTR0_REG;
			val = readl(reg);
			writel(U3PORT_DIS | val, reg);

			reg = rtk->regs + WRAP_APHY_REG;
			val = readl(reg);
			writel(~USB3_MBIAS_ENABLE & val, reg);

			dev_dbg(rtk->dev, "%s: disable usb 3.0 phy\n", __func__);
		}
	}

	reg = rtk->regs + WRAP_CTR_REG;
	val = readl(reg);
	writel(DESC_R2W_MULTI_DISABLE | val, reg);

	/* Set phy Dp/Dm initial state to host mode to avoid the Dp glitch */
	reg = rtk->regs + WRAP_USB2_PHY_REG;
	val = ~USB2_PHY_SWITCH_MASK & readl(reg);
	writel(USB2_PHY_SWITCH_HOST | val, reg);

	if (rtk->pm_base) {
		reg = rtk->pm_base + USB_DBUS_PWR_CTRL_REG;
		val = DBUS_PWR_CTRL_EN | readl(reg);
		writel(val, reg);
	}

	return 0;
}

static int dwc3_rtk_probe_dwc3_core(struct dwc3_rtk *rtk)
{
	struct device *dev = rtk->dev;
	struct device_node *node = dev->of_node;
	struct platform_device *dwc3_pdev;
	struct device *dwc3_dev;
	struct device_node *dwc3_node;
	enum usb_dr_mode dr_mode;
	int ret = 0;

	ret = dwc3_rtk_init(rtk);
	if (ret)
		return -EINVAL;

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to add dwc3 core\n");
		return ret;
	}

	dwc3_node = of_get_compatible_child(node, "snps,dwc3");
	if (!dwc3_node) {
		dev_err(dev, "failed to find dwc3 core node\n");
		ret = -ENODEV;
		goto depopulate;
	}

	dwc3_pdev = of_find_device_by_node(dwc3_node);
	if (!dwc3_pdev) {
		dev_err(dev, "failed to find dwc3 core platform_device\n");
		ret = -ENODEV;
		goto err_node_put;
	}

	dwc3_dev = &dwc3_pdev->dev;
	rtk->dwc = platform_get_drvdata(dwc3_pdev);
	if (!rtk->dwc) {
		dev_err(dev, "failed to find dwc3 core\n");
		ret = -ENODEV;
		goto err_pdev_put;
	}

	dr_mode = usb_get_dr_mode(dwc3_dev);
	if (dr_mode != rtk->dwc->dr_mode) {
		dev_info(dev, "dts set dr_mode=%d, but dwc3 set dr_mode=%d\n",
			 dr_mode, rtk->dwc->dr_mode);
		dr_mode = rtk->dwc->dr_mode;
	}

	switch (dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		rtk->cur_role = USB_ROLE_DEVICE;
		break;
	case USB_DR_MODE_HOST:
		rtk->cur_role = USB_ROLE_HOST;
		break;
	default:
		dev_dbg(rtk->dev, "%s: dr_mode=%d\n", __func__, dr_mode);
		break;
	}

	if (device_property_read_bool(dwc3_dev, "usb-role-switch")) {
		ret = dwc3_rtk_setup_role_switch(rtk);
		if (ret) {
			dev_err(dev, "dwc3_rtk_setup_role_switch fail=%d\n", ret);
			goto err_pdev_put;
		}
		rtk->cur_role = dwc3_rtk_get_role(rtk);
	}

	switch_usb2_role(rtk, rtk->cur_role);

	platform_device_put(dwc3_pdev);
	of_node_put(dwc3_node);

	return 0;

err_pdev_put:
	platform_device_put(dwc3_pdev);
err_node_put:
	of_node_put(dwc3_node);
depopulate:
	of_platform_depopulate(dev);

	return ret;
}

static int dwc3_rtk_probe(struct platform_device *pdev)
{
	struct dwc3_rtk *rtk;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *regs;
	int ret = 0;

	rtk = devm_kzalloc(dev, sizeof(*rtk), GFP_KERNEL);
	if (!rtk) {
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, rtk);

	rtk->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing memory resource\n");
		ret = -ENODEV;
		goto out;
	}

	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto out;
	}

	rtk->regs = regs;
	rtk->regs_size = resource_size(res);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		rtk->pm_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(rtk->pm_base)) {
			ret = PTR_ERR(rtk->pm_base);
			goto out;
		}
	}

	ret = dwc3_rtk_probe_dwc3_core(rtk);

out:
	return ret;
}

static void dwc3_rtk_remove(struct platform_device *pdev)
{
	struct dwc3_rtk *rtk = platform_get_drvdata(pdev);

	rtk->dwc = NULL;

	dwc3_rtk_remove_role_switch(rtk);

	of_platform_depopulate(rtk->dev);
}

static void dwc3_rtk_shutdown(struct platform_device *pdev)
{
	struct dwc3_rtk *rtk = platform_get_drvdata(pdev);

	of_platform_depopulate(rtk->dev);
}

static const struct of_device_id rtk_dwc3_match[] = {
	{ .compatible = "realtek,rtd-dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_dwc3_match);

#ifdef CONFIG_PM_SLEEP
static int dwc3_rtk_suspend(struct device *dev)
{
	return 0;
}

static int dwc3_rtk_resume(struct device *dev)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);

	dwc3_rtk_init(rtk);

	switch_usb2_role(rtk, rtk->cur_role);

	/* runtime set active to reflect active state. */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static const struct dev_pm_ops dwc3_rtk_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_rtk_suspend, dwc3_rtk_resume)
};

#define DEV_PM_OPS	(&dwc3_rtk_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_rtk_driver = {
	.probe		= dwc3_rtk_probe,
	.remove_new	= dwc3_rtk_remove,
	.driver		= {
		.name	= "rtk-dwc3",
		.of_match_table = rtk_dwc3_match,
		.pm	= DEV_PM_OPS,
	},
	.shutdown	= dwc3_rtk_shutdown,
};

module_platform_driver(dwc3_rtk_driver);

MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
MODULE_DESCRIPTION("DesignWare USB3 Realtek Glue Layer");
MODULE_ALIAS("platform:rtk-dwc3");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: phy_rtk_usb2 phy_rtk_usb3");
