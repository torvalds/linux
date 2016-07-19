/*
 * Copyright (c) 2015, Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/ulpi.h>

#define HSPHY_AHBBURST			0x0090
#define HSPHY_AHBMODE			0x0098
#define HSPHY_GENCONFIG			0x009c
#define HSPHY_GENCONFIG_2		0x00a0

#define HSPHY_USBCMD			0x0140
#define HSPHY_ULPI_VIEWPORT		0x0170
#define HSPHY_CTRL			0x0240

#define HSPHY_TXFIFO_IDLE_FORCE_DIS	BIT(4)
#define HSPHY_SESS_VLD_CTRL_EN		BIT(7)
#define HSPHY_POR_ASSERT		BIT(0)
#define HSPHY_RETEN			BIT(1)

#define HSPHY_SESS_VLD_CTRL		BIT(25)

#define ULPI_PWR_CLK_MNG_REG		0x88
#define ULPI_PWR_OTG_COMP_DISABLE	BIT(0)

#define ULPI_MISC_A			0x96
#define ULPI_MISC_A_VBUSVLDEXTSEL	BIT(1)
#define ULPI_MISC_A_VBUSVLDEXT		BIT(0)

#define HSPHY_3P3_MIN			3050000 /* uV */
#define HSPHY_3P3_MAX			3300000 /* uV */

#define HSPHY_1P8_MIN			1800000 /* uV */
#define HSPHY_1P8_MAX			1800000 /* uV */

#define HSPHY_VDD_MIN			5
#define HSPHY_VDD_MAX			7

struct phy_8x16 {
	struct usb_phy			phy;
	void __iomem			*regs;
	struct clk			*core_clk;
	struct clk			*iface_clk;
	struct regulator_bulk_data	regulator[3];

	struct reset_control		*phy_reset;

	struct extcon_dev		*vbus_edev;
	struct notifier_block		vbus_notify;

	struct gpio_desc		*switch_gpio;
	struct notifier_block		reboot_notify;
};

static int phy_8x16_notify_connect(struct usb_phy *phy,
				   enum usb_device_speed speed)
{
	struct phy_8x16 *qphy = container_of(phy, struct phy_8x16, phy);
	u32 val;

	val = ULPI_MISC_A_VBUSVLDEXTSEL | ULPI_MISC_A_VBUSVLDEXT;
	usb_phy_io_write(&qphy->phy, val, ULPI_SET(ULPI_MISC_A));

	val = readl(qphy->regs + HSPHY_USBCMD);
	val |= HSPHY_SESS_VLD_CTRL;
	writel(val, qphy->regs + HSPHY_USBCMD);

	return 0;
}

static int phy_8x16_notify_disconnect(struct usb_phy *phy,
				      enum usb_device_speed speed)
{
	struct phy_8x16 *qphy = container_of(phy, struct phy_8x16, phy);
	u32 val;

	val = ULPI_MISC_A_VBUSVLDEXT | ULPI_MISC_A_VBUSVLDEXTSEL;
	usb_phy_io_write(&qphy->phy, val, ULPI_CLR(ULPI_MISC_A));

	val = readl(qphy->regs + HSPHY_USBCMD);
	val &= ~HSPHY_SESS_VLD_CTRL;
	writel(val, qphy->regs + HSPHY_USBCMD);

	return 0;
}

static int phy_8x16_vbus_on(struct phy_8x16 *qphy)
{
	phy_8x16_notify_connect(&qphy->phy, USB_SPEED_UNKNOWN);

	/* Switch D+/D- lines to Device connector */
	gpiod_set_value_cansleep(qphy->switch_gpio, 0);

	return 0;
}

static int phy_8x16_vbus_off(struct phy_8x16 *qphy)
{
	phy_8x16_notify_disconnect(&qphy->phy, USB_SPEED_UNKNOWN);

	/* Switch D+/D- lines to USB HUB */
	gpiod_set_value_cansleep(qphy->switch_gpio, 1);

	return 0;
}

static int phy_8x16_vbus_notify(struct notifier_block *nb, unsigned long event,
				void *ptr)
{
	struct phy_8x16 *qphy = container_of(nb, struct phy_8x16, vbus_notify);

	if (event)
		phy_8x16_vbus_on(qphy);
	else
		phy_8x16_vbus_off(qphy);

	return NOTIFY_DONE;
}

static int phy_8x16_init(struct usb_phy *phy)
{
	struct phy_8x16 *qphy = container_of(phy, struct phy_8x16, phy);
	u32 val, init[] = {0x44, 0x6B, 0x24, 0x13};
	u32 addr = ULPI_EXT_VENDOR_SPECIFIC;
	int idx, state;

	for (idx = 0; idx < ARRAY_SIZE(init); idx++)
		usb_phy_io_write(phy, init[idx], addr + idx);

	reset_control_reset(qphy->phy_reset);

	/* Assert USB HSPHY_POR */
	val = readl(qphy->regs + HSPHY_CTRL);
	val |= HSPHY_POR_ASSERT;
	writel(val, qphy->regs + HSPHY_CTRL);

	/*
	 * wait for minimum 10 microseconds as suggested in HPG.
	 * Use a slightly larger value since the exact value didn't
	 * work 100% of the time.
	 */
	usleep_range(12, 15);

	/* Deassert USB HSPHY_POR */
	val = readl(qphy->regs + HSPHY_CTRL);
	val &= ~HSPHY_POR_ASSERT;
	writel(val, qphy->regs + HSPHY_CTRL);

	usleep_range(10, 15);

	writel(0x00, qphy->regs + HSPHY_AHBBURST);
	writel(0x08, qphy->regs + HSPHY_AHBMODE);

	/* workaround for rx buffer collision issue */
	val = readl(qphy->regs + HSPHY_GENCONFIG);
	val &= ~HSPHY_TXFIFO_IDLE_FORCE_DIS;
	writel(val, qphy->regs + HSPHY_GENCONFIG);

	val = readl(qphy->regs + HSPHY_GENCONFIG_2);
	val |= HSPHY_SESS_VLD_CTRL_EN;
	writel(val, qphy->regs + HSPHY_GENCONFIG_2);

	val = ULPI_PWR_OTG_COMP_DISABLE;
	usb_phy_io_write(phy, val, ULPI_SET(ULPI_PWR_CLK_MNG_REG));

	state = extcon_get_cable_state_(qphy->vbus_edev, EXTCON_USB);
	if (state)
		phy_8x16_vbus_on(qphy);
	else
		phy_8x16_vbus_off(qphy);

	val = usb_phy_io_read(&qphy->phy, ULPI_FUNC_CTRL);
	val &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	val |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
	usb_phy_io_write(&qphy->phy, val, ULPI_FUNC_CTRL);

	return 0;
}

static void phy_8x16_shutdown(struct usb_phy *phy)
{
	u32 val;

	/* Put the controller in non-driving mode */
	val = usb_phy_io_read(phy, ULPI_FUNC_CTRL);
	val &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	val |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
	usb_phy_io_write(phy, val, ULPI_FUNC_CTRL);
}

static int phy_8x16_read_devicetree(struct phy_8x16 *qphy)
{
	struct device *dev = qphy->phy.dev;
	int ret;

	qphy->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(qphy->core_clk))
		return PTR_ERR(qphy->core_clk);

	qphy->iface_clk = devm_clk_get(dev, "iface");
	if (IS_ERR(qphy->iface_clk))
		return PTR_ERR(qphy->iface_clk);

	qphy->regulator[0].supply = "v3p3";
	qphy->regulator[1].supply = "v1p8";
	qphy->regulator[2].supply = "vddcx";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(qphy->regulator),
				      qphy->regulator);
	if (ret)
		return ret;

	qphy->phy_reset = devm_reset_control_get(dev, "phy");
	if (IS_ERR(qphy->phy_reset))
		return PTR_ERR(qphy->phy_reset);

	qphy->switch_gpio = devm_gpiod_get_optional(dev, "switch",
						   GPIOD_OUT_LOW);
	if (IS_ERR(qphy->switch_gpio))
		return PTR_ERR(qphy->switch_gpio);

	return 0;
}

static int phy_8x16_reboot_notify(struct notifier_block *this,
				  unsigned long code, void *unused)
{
	struct phy_8x16 *qphy;

	qphy = container_of(this, struct phy_8x16, reboot_notify);

	/*
	 * Ensure that D+/D- lines are routed to uB connector, so
	 * we could load bootloader/kernel at next reboot_notify
	 */
	gpiod_set_value_cansleep(qphy->switch_gpio, 0);
	return NOTIFY_DONE;
}

static int phy_8x16_probe(struct platform_device *pdev)
{
	struct phy_8x16 *qphy;
	struct resource *res;
	struct usb_phy *phy;
	int ret;

	qphy = devm_kzalloc(&pdev->dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	platform_set_drvdata(pdev, qphy);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	qphy->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!qphy->regs)
		return -ENOMEM;

	phy			= &qphy->phy;
	phy->dev		= &pdev->dev;
	phy->label		= dev_name(&pdev->dev);
	phy->init		= phy_8x16_init;
	phy->shutdown		= phy_8x16_shutdown;
	phy->notify_connect	= phy_8x16_notify_connect;
	phy->notify_disconnect	= phy_8x16_notify_disconnect;
	phy->io_priv		= qphy->regs + HSPHY_ULPI_VIEWPORT;
	phy->io_ops		= &ulpi_viewport_access_ops;
	phy->type		= USB_PHY_TYPE_USB2;

	ret = phy_8x16_read_devicetree(qphy);
	if (ret < 0)
		return ret;

	qphy->vbus_edev = extcon_get_edev_by_phandle(phy->dev, 0);
	if (IS_ERR(qphy->vbus_edev))
		return PTR_ERR(qphy->vbus_edev);

	ret = clk_set_rate(qphy->core_clk, INT_MAX);
	if (ret < 0)
		dev_dbg(phy->dev, "Can't boost core clock\n");

	ret = clk_prepare_enable(qphy->core_clk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(qphy->iface_clk);
	if (ret < 0)
		goto off_core;

	ret = regulator_bulk_enable(ARRAY_SIZE(qphy->regulator),
				    qphy->regulator);
	if (WARN_ON(ret))
		goto off_clks;

	qphy->vbus_notify.notifier_call = phy_8x16_vbus_notify;
	ret = extcon_register_notifier(qphy->vbus_edev, EXTCON_USB,
				       &qphy->vbus_notify);
	if (ret < 0)
		goto off_power;

	ret = usb_add_phy_dev(&qphy->phy);
	if (ret)
		goto off_extcon;

	qphy->reboot_notify.notifier_call = phy_8x16_reboot_notify;
	register_reboot_notifier(&qphy->reboot_notify);

	return 0;

off_extcon:
	extcon_unregister_notifier(qphy->vbus_edev, EXTCON_USB,
				   &qphy->vbus_notify);
off_power:
	regulator_bulk_disable(ARRAY_SIZE(qphy->regulator), qphy->regulator);
off_clks:
	clk_disable_unprepare(qphy->iface_clk);
off_core:
	clk_disable_unprepare(qphy->core_clk);
	return ret;
}

static int phy_8x16_remove(struct platform_device *pdev)
{
	struct phy_8x16 *qphy = platform_get_drvdata(pdev);

	unregister_reboot_notifier(&qphy->reboot_notify);
	extcon_unregister_notifier(qphy->vbus_edev, EXTCON_USB,
				   &qphy->vbus_notify);

	/*
	 * Ensure that D+/D- lines are routed to uB connector, so
	 * we could load bootloader/kernel at next reboot_notify
	 */
	gpiod_set_value_cansleep(qphy->switch_gpio, 0);

	usb_remove_phy(&qphy->phy);

	clk_disable_unprepare(qphy->iface_clk);
	clk_disable_unprepare(qphy->core_clk);
	regulator_bulk_disable(ARRAY_SIZE(qphy->regulator), qphy->regulator);
	return 0;
}

static const struct of_device_id phy_8x16_dt_match[] = {
	{ .compatible = "qcom,usb-8x16-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, phy_8x16_dt_match);

static struct platform_driver phy_8x16_driver = {
	.probe	= phy_8x16_probe,
	.remove = phy_8x16_remove,
	.driver = {
		.name = "phy-qcom-8x16-usb",
		.of_match_table = phy_8x16_dt_match,
	},
};
module_platform_driver(phy_8x16_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm APQ8016/MSM8916 chipsets USB transceiver driver");
