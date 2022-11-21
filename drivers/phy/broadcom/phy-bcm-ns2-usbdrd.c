/*
 * Copyright (C) 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define ICFG_DRD_AFE		0x0
#define ICFG_MISC_STAT		0x18
#define ICFG_DRD_P0CTL		0x1C
#define ICFG_STRAP_CTRL		0x20
#define ICFG_FSM_CTRL		0x24

#define ICFG_DEV_BIT		BIT(2)
#define IDM_RST_BIT		BIT(0)
#define AFE_CORERDY_VDDC	BIT(18)
#define PHY_PLL_RESETB		BIT(15)
#define PHY_RESETB		BIT(14)
#define PHY_PLL_LOCK		BIT(0)

#define DRD_DEV_MODE		BIT(20)
#define OHCI_OVRCUR_POL		BIT(11)
#define ICFG_OFF_MODE		BIT(6)
#define PLL_LOCK_RETRY		1000

#define EVT_DEVICE		0
#define EVT_HOST		1

#define DRD_HOST_MODE		(BIT(2) | BIT(3))
#define DRD_DEVICE_MODE		(BIT(4) | BIT(5))
#define DRD_HOST_VAL		0x803
#define DRD_DEV_VAL		0x807
#define GPIO_DELAY		20

struct ns2_phy_data;
struct ns2_phy_driver {
	void __iomem *icfgdrd_regs;
	void __iomem *idmdrd_rst_ctrl;
	void __iomem *crmu_usb2_ctrl;
	void __iomem *usb2h_strap_reg;
	struct ns2_phy_data *data;
	struct extcon_dev *edev;
	struct gpio_desc *vbus_gpiod;
	struct gpio_desc *id_gpiod;
	int id_irq;
	int vbus_irq;
	unsigned long debounce_jiffies;
	struct delayed_work wq_extcon;
};

struct ns2_phy_data {
	struct ns2_phy_driver *driver;
	struct phy *phy;
	int new_state;
};

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static inline int pll_lock_stat(u32 usb_reg, int reg_mask,
				struct ns2_phy_driver *driver)
{
	u32 val;

	return readl_poll_timeout_atomic(driver->icfgdrd_regs + usb_reg,
					 val, (val & reg_mask), 1,
					 PLL_LOCK_RETRY);
}

static int ns2_drd_phy_init(struct phy *phy)
{
	struct ns2_phy_data *data = phy_get_drvdata(phy);
	struct ns2_phy_driver *driver = data->driver;
	u32 val;

	val = readl(driver->icfgdrd_regs + ICFG_FSM_CTRL);

	if (data->new_state == EVT_HOST) {
		val &= ~DRD_DEVICE_MODE;
		val |= DRD_HOST_MODE;
	} else {
		val &= ~DRD_HOST_MODE;
		val |= DRD_DEVICE_MODE;
	}
	writel(val, driver->icfgdrd_regs + ICFG_FSM_CTRL);

	return 0;
}

static int ns2_drd_phy_poweroff(struct phy *phy)
{
	struct ns2_phy_data *data = phy_get_drvdata(phy);
	struct ns2_phy_driver *driver = data->driver;
	u32 val;

	val = readl(driver->crmu_usb2_ctrl);
	val &= ~AFE_CORERDY_VDDC;
	writel(val, driver->crmu_usb2_ctrl);

	val = readl(driver->crmu_usb2_ctrl);
	val &= ~DRD_DEV_MODE;
	writel(val, driver->crmu_usb2_ctrl);

	/* Disable Host and Device Mode */
	val = readl(driver->icfgdrd_regs + ICFG_FSM_CTRL);
	val &= ~(DRD_HOST_MODE | DRD_DEVICE_MODE | ICFG_OFF_MODE);
	writel(val, driver->icfgdrd_regs + ICFG_FSM_CTRL);

	return 0;
}

static int ns2_drd_phy_poweron(struct phy *phy)
{
	struct ns2_phy_data *data = phy_get_drvdata(phy);
	struct ns2_phy_driver *driver = data->driver;
	u32 extcon_event = data->new_state;
	int ret;
	u32 val;

	if (extcon_event == EVT_DEVICE) {
		writel(DRD_DEV_VAL, driver->icfgdrd_regs + ICFG_DRD_P0CTL);

		val = readl(driver->idmdrd_rst_ctrl);
		val &= ~IDM_RST_BIT;
		writel(val, driver->idmdrd_rst_ctrl);

		val = readl(driver->crmu_usb2_ctrl);
		val |= (AFE_CORERDY_VDDC | DRD_DEV_MODE);
		writel(val, driver->crmu_usb2_ctrl);

		/* Bring PHY and PHY_PLL out of Reset */
		val = readl(driver->crmu_usb2_ctrl);
		val |= (PHY_PLL_RESETB | PHY_RESETB);
		writel(val, driver->crmu_usb2_ctrl);

		ret = pll_lock_stat(ICFG_MISC_STAT, PHY_PLL_LOCK, driver);
		if (ret < 0) {
			dev_err(&phy->dev, "Phy PLL lock failed\n");
			return ret;
		}
	} else {
		writel(DRD_HOST_VAL, driver->icfgdrd_regs + ICFG_DRD_P0CTL);

		val = readl(driver->crmu_usb2_ctrl);
		val |= AFE_CORERDY_VDDC;
		writel(val, driver->crmu_usb2_ctrl);

		ret = pll_lock_stat(ICFG_MISC_STAT, PHY_PLL_LOCK, driver);
		if (ret < 0) {
			dev_err(&phy->dev, "Phy PLL lock failed\n");
			return ret;
		}

		val = readl(driver->idmdrd_rst_ctrl);
		val &= ~IDM_RST_BIT;
		writel(val, driver->idmdrd_rst_ctrl);

		/* port over current Polarity */
		val = readl(driver->usb2h_strap_reg);
		val |= OHCI_OVRCUR_POL;
		writel(val, driver->usb2h_strap_reg);
	}

	return 0;
}

static void connect_change(struct ns2_phy_driver *driver)
{
	u32 extcon_event;
	u32 val;

	extcon_event = driver->data->new_state;
	val = readl(driver->icfgdrd_regs + ICFG_FSM_CTRL);

	switch (extcon_event) {
	case EVT_DEVICE:
		val &= ~(DRD_HOST_MODE | DRD_DEVICE_MODE);
		writel(val, driver->icfgdrd_regs + ICFG_FSM_CTRL);

		val = (val & ~DRD_HOST_MODE) | DRD_DEVICE_MODE;
		writel(val, driver->icfgdrd_regs + ICFG_FSM_CTRL);

		val = readl(driver->icfgdrd_regs + ICFG_DRD_P0CTL);
		val |= ICFG_DEV_BIT;
		writel(val, driver->icfgdrd_regs + ICFG_DRD_P0CTL);
		break;

	case EVT_HOST:
		val &= ~(DRD_HOST_MODE | DRD_DEVICE_MODE);
		writel(val, driver->icfgdrd_regs + ICFG_FSM_CTRL);

		val = (val & ~DRD_DEVICE_MODE) | DRD_HOST_MODE;
		writel(val, driver->icfgdrd_regs + ICFG_FSM_CTRL);

		val = readl(driver->usb2h_strap_reg);
		val |= OHCI_OVRCUR_POL;
		writel(val, driver->usb2h_strap_reg);

		val = readl(driver->icfgdrd_regs + ICFG_DRD_P0CTL);
		val &= ~ICFG_DEV_BIT;
		writel(val, driver->icfgdrd_regs + ICFG_DRD_P0CTL);
		break;

	default:
		pr_err("Invalid extcon event\n");
		break;
	}
}

static void extcon_work(struct work_struct *work)
{
	struct ns2_phy_driver *driver;
	int vbus;
	int id;

	driver  = container_of(to_delayed_work(work),
			       struct ns2_phy_driver, wq_extcon);

	id = gpiod_get_value_cansleep(driver->id_gpiod);
	vbus = gpiod_get_value_cansleep(driver->vbus_gpiod);

	if (!id && vbus) { /* Host connected */
		extcon_set_state_sync(driver->edev, EXTCON_USB_HOST, true);
		pr_debug("Host cable connected\n");
		driver->data->new_state = EVT_HOST;
		connect_change(driver);
	} else if (id && !vbus) { /* Disconnected */
		extcon_set_state_sync(driver->edev, EXTCON_USB_HOST, false);
		extcon_set_state_sync(driver->edev, EXTCON_USB, false);
		pr_debug("Cable disconnected\n");
	} else if (id && vbus) { /* Device connected */
		extcon_set_state_sync(driver->edev, EXTCON_USB, true);
		pr_debug("Device cable connected\n");
		driver->data->new_state = EVT_DEVICE;
		connect_change(driver);
	}
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct ns2_phy_driver *driver = dev_id;

	queue_delayed_work(system_power_efficient_wq, &driver->wq_extcon,
			   driver->debounce_jiffies);

	return IRQ_HANDLED;
}

static const struct phy_ops ops = {
	.init		= ns2_drd_phy_init,
	.power_on	= ns2_drd_phy_poweron,
	.power_off	= ns2_drd_phy_poweroff,
	.owner		= THIS_MODULE,
};

static const struct of_device_id ns2_drd_phy_dt_ids[] = {
	{ .compatible = "brcm,ns2-drd-phy", },
	{ }
};
MODULE_DEVICE_TABLE(of, ns2_drd_phy_dt_ids);

static int ns2_drd_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct ns2_phy_driver *driver;
	struct ns2_phy_data *data;
	int ret;
	u32 val;

	driver = devm_kzalloc(dev, sizeof(struct ns2_phy_driver),
			      GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->data = devm_kzalloc(dev, sizeof(struct ns2_phy_data),
				  GFP_KERNEL);
	if (!driver->data)
		return -ENOMEM;

	driver->icfgdrd_regs = devm_platform_ioremap_resource_byname(pdev, "icfg");
	if (IS_ERR(driver->icfgdrd_regs))
		return PTR_ERR(driver->icfgdrd_regs);

	driver->idmdrd_rst_ctrl = devm_platform_ioremap_resource_byname(pdev, "rst-ctrl");
	if (IS_ERR(driver->idmdrd_rst_ctrl))
		return PTR_ERR(driver->idmdrd_rst_ctrl);

	driver->crmu_usb2_ctrl = devm_platform_ioremap_resource_byname(pdev, "crmu-ctrl");
	if (IS_ERR(driver->crmu_usb2_ctrl))
		return PTR_ERR(driver->crmu_usb2_ctrl);

	driver->usb2h_strap_reg = devm_platform_ioremap_resource_byname(pdev, "usb2-strap");
	if (IS_ERR(driver->usb2h_strap_reg))
		return PTR_ERR(driver->usb2h_strap_reg);

	 /* create extcon */
	driver->id_gpiod = devm_gpiod_get(&pdev->dev, "id", GPIOD_IN);
	if (IS_ERR(driver->id_gpiod)) {
		dev_err(dev, "failed to get ID GPIO\n");
		return PTR_ERR(driver->id_gpiod);
	}
	driver->vbus_gpiod = devm_gpiod_get(&pdev->dev, "vbus", GPIOD_IN);
	if (IS_ERR(driver->vbus_gpiod)) {
		dev_err(dev, "failed to get VBUS GPIO\n");
		return PTR_ERR(driver->vbus_gpiod);
	}

	driver->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(driver->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, driver->edev);
	if (ret < 0) {
		dev_err(dev, "failed to register extcon device\n");
		return ret;
	}

	ret = gpiod_set_debounce(driver->id_gpiod, GPIO_DELAY * 1000);
	if (ret < 0)
		driver->debounce_jiffies = msecs_to_jiffies(GPIO_DELAY);

	INIT_DELAYED_WORK(&driver->wq_extcon, extcon_work);

	driver->id_irq = gpiod_to_irq(driver->id_gpiod);
	if (driver->id_irq < 0) {
		dev_err(dev, "failed to get ID IRQ\n");
		return driver->id_irq;
	}

	driver->vbus_irq = gpiod_to_irq(driver->vbus_gpiod);
	if (driver->vbus_irq < 0) {
		dev_err(dev, "failed to get ID IRQ\n");
		return driver->vbus_irq;
	}

	ret = devm_request_irq(dev, driver->id_irq, gpio_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			       "usb_id", driver);
	if (ret < 0) {
		dev_err(dev, "failed to request handler for ID IRQ\n");
		return ret;
	}

	ret = devm_request_irq(dev, driver->vbus_irq, gpio_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			       "usb_vbus", driver);
	if (ret < 0) {
		dev_err(dev, "failed to request handler for VBUS IRQ\n");
		return ret;
	}

	dev_set_drvdata(dev, driver);

	/* Shutdown all ports. They can be powered up as required */
	val = readl(driver->crmu_usb2_ctrl);
	val &= ~(AFE_CORERDY_VDDC | PHY_RESETB);
	writel(val, driver->crmu_usb2_ctrl);

	data = driver->data;
	data->phy = devm_phy_create(dev, dev->of_node, &ops);
	if (IS_ERR(data->phy)) {
		dev_err(dev, "Failed to create usb drd phy\n");
		return PTR_ERR(data->phy);
	}

	data->driver = driver;
	phy_set_drvdata(data->phy, data);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "Failed to register as phy provider\n");
		return PTR_ERR(phy_provider);
	}

	platform_set_drvdata(pdev, driver);

	dev_info(dev, "Registered NS2 DRD Phy device\n");
	queue_delayed_work(system_power_efficient_wq, &driver->wq_extcon,
			   driver->debounce_jiffies);

	return 0;
}

static struct platform_driver ns2_drd_phy_driver = {
	.probe = ns2_drd_phy_probe,
	.driver = {
		.name = "bcm-ns2-usbphy",
		.of_match_table = of_match_ptr(ns2_drd_phy_dt_ids),
	},
};
module_platform_driver(ns2_drd_phy_driver);

MODULE_ALIAS("platform:bcm-ns2-drd-phy");
MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom NS2 USB2 PHY driver");
MODULE_LICENSE("GPL v2");
