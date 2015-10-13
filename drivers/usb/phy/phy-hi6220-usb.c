/*
 * Copyright (c) 2015 Linaro Ltd.
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>

#define SC_PERIPH_CTRL4			0x00c

#define CTRL4_PICO_SIDDQ		BIT(6)
#define CTRL4_PICO_OGDISABLE		BIT(8)
#define CTRL4_PICO_VBUSVLDEXT		BIT(10)
#define CTRL4_PICO_VBUSVLDEXTSEL	BIT(11)
#define CTRL4_OTG_PHY_SEL		BIT(21)

#define SC_PERIPH_CTRL5			0x010

#define CTRL5_USBOTG_RES_SEL		BIT(3)
#define CTRL5_PICOPHY_ACAENB		BIT(4)
#define CTRL5_PICOPHY_BC_MODE		BIT(5)
#define CTRL5_PICOPHY_CHRGSEL		BIT(6)
#define CTRL5_PICOPHY_VDATSRCEND	BIT(7)
#define CTRL5_PICOPHY_VDATDETENB	BIT(8)
#define CTRL5_PICOPHY_DCDENB		BIT(9)
#define CTRL5_PICOPHY_IDDIG		BIT(10)

#define SC_PERIPH_CTRL8			0x018
#define SC_PERIPH_RSTEN0		0x300
#define SC_PERIPH_RSTDIS0		0x304

#define RST0_USBOTG_BUS			BIT(4)
#define RST0_POR_PICOPHY		BIT(5)
#define RST0_USBOTG			BIT(6)
#define RST0_USBOTG_32K			BIT(7)

#define EYE_PATTERN_PARA		0x7053348c

struct hi6220_priv {
	struct usb_phy phy;
	struct delayed_work work;
	struct regmap *reg;
	struct clk *clk;
	struct regulator *vcc;
	struct device *dev;
	int gpio_vbus;
	int gpio_id;
	enum usb_otg_state state;
};

static void hi6220_start_periphrals(struct hi6220_priv *priv, bool on)
{
	struct usb_otg *otg = priv->phy.otg;

	if (!otg->gadget)
		return;

	if (on)
		usb_gadget_connect(otg->gadget);
	else
		usb_gadget_disconnect(otg->gadget);
}

static void hi6220_detect_work(struct work_struct *work)
{
	struct hi6220_priv *priv =
		container_of(work, struct hi6220_priv, work.work);
	int gpio_id, gpio_vbus;
	enum usb_otg_state state;

	if (!gpio_is_valid(priv->gpio_id) || !gpio_is_valid(priv->gpio_vbus))
		return;

	gpio_id = gpio_get_value_cansleep(priv->gpio_id);
	gpio_vbus = gpio_get_value_cansleep(priv->gpio_vbus);

	if (gpio_vbus == 0) {
		if (gpio_id == 1)
			state = OTG_STATE_B_PERIPHERAL;
		else
			state = OTG_STATE_A_HOST;
	} else {
		state = OTG_STATE_A_HOST;
	}

	if (priv->state != state) {
		hi6220_start_periphrals(priv, state == OTG_STATE_B_PERIPHERAL);
		priv->state = state;
	}
}

static irqreturn_t hiusb_gpio_intr(int irq, void *data)
{
	struct hi6220_priv *priv = (struct hi6220_priv *)data;

	/* add debounce time */
	schedule_delayed_work(&priv->work, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static int hi6220_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	otg->gadget = gadget;
	return 0;
}

static int hi6220_phy_setup(struct hi6220_priv *priv, bool on)
{
	struct regmap *reg = priv->reg;
	u32 val, mask;
	int ret;

	if (priv->reg == NULL)
		return 0;

	if (on) {
		val = RST0_USBOTG_BUS | RST0_POR_PICOPHY |
		      RST0_USBOTG | RST0_USBOTG_32K;
		mask = val;
		ret = regmap_update_bits(reg, SC_PERIPH_RSTEN0, mask, val);
		if (ret)
			goto out;
		ret = regmap_update_bits(reg, SC_PERIPH_RSTDIS0, mask, val);
		if (ret)
			goto out;

		val = CTRL5_USBOTG_RES_SEL | CTRL5_PICOPHY_ACAENB;
		mask = val | CTRL5_PICOPHY_BC_MODE;
		ret = regmap_update_bits(reg, SC_PERIPH_CTRL5, mask, val);
		if (ret)
			goto out;

		val =  CTRL4_PICO_VBUSVLDEXT | CTRL4_PICO_VBUSVLDEXTSEL |
		       CTRL4_OTG_PHY_SEL;
		mask = val | CTRL4_PICO_SIDDQ | CTRL4_PICO_OGDISABLE;
		ret = regmap_update_bits(reg, SC_PERIPH_CTRL4, mask, val);
		if (ret)
			goto out;

		ret = regmap_write(reg, SC_PERIPH_CTRL8, EYE_PATTERN_PARA);
		if (ret)
			goto out;
	} else {
		val = CTRL4_PICO_SIDDQ;
		mask = val;
		ret = regmap_update_bits(reg, SC_PERIPH_CTRL4, mask, val);
		if (ret)
			goto out;

		val = RST0_USBOTG_BUS | RST0_POR_PICOPHY |
		      RST0_USBOTG | RST0_USBOTG_32K;
		mask = val;
		ret = regmap_update_bits(reg, SC_PERIPH_RSTEN0, mask, val);
		if (ret)
			goto out;
	}

	return 0;
out:
	dev_err(priv->dev, "failed to setup phy ret: %d\n", ret);
	return ret;
}

static int hi6220_phy_probe(struct platform_device *pdev)
{
	struct hi6220_priv *priv;
	struct usb_otg *otg;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	otg = devm_kzalloc(dev, sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	priv->dev = dev;
	priv->phy.dev = &pdev->dev;
	priv->phy.otg = otg;
	priv->phy.label = "hi6220";
	priv->phy.type = USB_PHY_TYPE_USB2;
	otg->set_peripheral = hi6220_set_peripheral;
	platform_set_drvdata(pdev, priv);

	priv->gpio_vbus = of_get_named_gpio(np, "hisilicon,gpio-vbus", 0);
	if (priv->gpio_vbus == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (!gpio_is_valid(priv->gpio_vbus)) {
		dev_err(dev, "invalid gpio %d\n", priv->gpio_vbus);
		return -ENODEV;
	}

	priv->gpio_id = of_get_named_gpio(np, "hisilicon,gpio-id", 0);
	if (priv->gpio_id == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (!gpio_is_valid(priv->gpio_id)) {
		dev_err(dev, "invalid gpio %d\n", priv->gpio_id);
		return -ENODEV;
	}

	priv->reg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
					"hisilicon,peripheral-syscon");
	if (IS_ERR(priv->reg))
		priv->reg = NULL;

	ret = devm_gpio_request_one(dev, priv->gpio_vbus,
				    GPIOF_IN, "gpio_vbus");
	if (ret < 0) {
		dev_err(dev, "gpio request failed for gpio_vbus\n");
		return ret;
	}

	ret = devm_gpio_request_one(dev, priv->gpio_id, GPIOF_IN, "gpio_id");
	if (ret < 0) {
		dev_err(dev, "gpio request failed for gpio_id\n");
		return ret;
	}

	priv->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(priv->vcc)) {
		if (PTR_ERR(priv->vcc) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "No regulator found\n");
	} else {
		ret = regulator_enable(priv->vcc);
		if (ret) {
			dev_err(dev, "Failed to enable regulator\n");
			return -ENODEV;
		}
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		regulator_disable(priv->vcc);
		return PTR_ERR(priv->clk);
	}
	clk_prepare_enable(priv->clk);
	INIT_DELAYED_WORK(&priv->work, hi6220_detect_work);

	irq = gpio_to_irq(priv->gpio_vbus);
	ret = devm_request_irq(dev, gpio_to_irq(priv->gpio_vbus),
			       hiusb_gpio_intr, IRQF_NO_SUSPEND |
			       IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			       "vbus_gpio_intr", priv);
	if (ret) {
		dev_err(dev, "request gpio irq failed.\n");
		goto err_irq;
	}

	hi6220_phy_setup(priv, true);
	ret = usb_add_phy_dev(&priv->phy);
	if (ret) {
		dev_err(dev, "Can't register transceiver\n");
		goto err_irq;
	}
	schedule_delayed_work(&priv->work, 0);

	return 0;
err_irq:
	cancel_delayed_work_sync(&priv->work);
	clk_disable_unprepare(priv->clk);
	regulator_disable(priv->vcc);
	return ret;
}

static int hi6220_phy_remove(struct platform_device *pdev)
{
	struct hi6220_priv *priv = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&priv->work);
	hi6220_phy_setup(priv, false);
	clk_disable_unprepare(priv->clk);
	regulator_disable(priv->vcc);
	return 0;
}

static const struct of_device_id hi6220_phy_of_match[] = {
	{.compatible = "hisilicon,hi6220-usb-phy",},
	{ },
};
MODULE_DEVICE_TABLE(of, hi6220_phy_of_match);

static struct platform_driver hi6220_phy_driver = {
	.probe	= hi6220_phy_probe,
	.remove	= hi6220_phy_remove,
	.driver = {
		.name	= "hi6220-usb-phy",
		.of_match_table	= hi6220_phy_of_match,
	}
};
module_platform_driver(hi6220_phy_driver);

MODULE_DESCRIPTION("HISILICON HI6220 USB PHY driver");
MODULE_ALIAS("platform:hi6220-usb-phy");
MODULE_LICENSE("GPL");
