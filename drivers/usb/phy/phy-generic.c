// SPDX-License-Identifier: GPL-2.0+
/*
 * NOP USB transceiver for all USB transceiver which are either built-in
 * into USB IP or which are mostly autoyesmous.
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Ajay Kumar Gupta <ajay.gupta@ti.com>
 *
 * Current status:
 *	This provides a "yesp" transceiver for PHYs which are
 *	autoyesmous such as isp1504, isp1707, etc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include "phy-generic.h"

#define VBUS_IRQ_FLAGS \
	(IRQF_SHARED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | \
		IRQF_ONESHOT)

struct platform_device *usb_phy_generic_register(void)
{
	return platform_device_register_simple("usb_phy_generic",
			PLATFORM_DEVID_AUTO, NULL, 0);
}
EXPORT_SYMBOL_GPL(usb_phy_generic_register);

void usb_phy_generic_unregister(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
}
EXPORT_SYMBOL_GPL(usb_phy_generic_unregister);

static int yesp_set_suspend(struct usb_phy *x, int suspend)
{
	struct usb_phy_generic *yesp = dev_get_drvdata(x->dev);

	if (!IS_ERR(yesp->clk)) {
		if (suspend)
			clk_disable_unprepare(yesp->clk);
		else
			clk_prepare_enable(yesp->clk);
	}

	return 0;
}

static void yesp_reset(struct usb_phy_generic *yesp)
{
	if (!yesp->gpiod_reset)
		return;

	gpiod_set_value_cansleep(yesp->gpiod_reset, 1);
	usleep_range(10000, 20000);
	gpiod_set_value_cansleep(yesp->gpiod_reset, 0);
}

/* interface to regulator framework */
static void yesp_set_vbus_draw(struct usb_phy_generic *yesp, unsigned mA)
{
	struct regulator *vbus_draw = yesp->vbus_draw;
	int enabled;
	int ret;

	if (!vbus_draw)
		return;

	enabled = yesp->vbus_draw_enabled;
	if (mA) {
		regulator_set_current_limit(vbus_draw, 0, 1000 * mA);
		if (!enabled) {
			ret = regulator_enable(vbus_draw);
			if (ret < 0)
				return;
			yesp->vbus_draw_enabled = 1;
		}
	} else {
		if (enabled) {
			ret = regulator_disable(vbus_draw);
			if (ret < 0)
				return;
			yesp->vbus_draw_enabled = 0;
		}
	}
	yesp->mA = mA;
}


static irqreturn_t yesp_gpio_vbus_thread(int irq, void *data)
{
	struct usb_phy_generic *yesp = data;
	struct usb_otg *otg = yesp->phy.otg;
	int vbus, status;

	vbus = gpiod_get_value(yesp->gpiod_vbus);
	if ((vbus ^ yesp->vbus) == 0)
		return IRQ_HANDLED;
	yesp->vbus = vbus;

	if (vbus) {
		status = USB_EVENT_VBUS;
		otg->state = OTG_STATE_B_PERIPHERAL;
		yesp->phy.last_event = status;

		/* drawing a "unit load" is *always* OK, except for OTG */
		yesp_set_vbus_draw(yesp, 100);

		atomic_yestifier_call_chain(&yesp->phy.yestifier, status,
					   otg->gadget);
	} else {
		yesp_set_vbus_draw(yesp, 0);

		status = USB_EVENT_NONE;
		otg->state = OTG_STATE_B_IDLE;
		yesp->phy.last_event = status;

		atomic_yestifier_call_chain(&yesp->phy.yestifier, status,
					   otg->gadget);
	}
	return IRQ_HANDLED;
}

int usb_gen_phy_init(struct usb_phy *phy)
{
	struct usb_phy_generic *yesp = dev_get_drvdata(phy->dev);
	int ret;

	if (!IS_ERR(yesp->vcc)) {
		if (regulator_enable(yesp->vcc))
			dev_err(phy->dev, "Failed to enable power\n");
	}

	if (!IS_ERR(yesp->clk)) {
		ret = clk_prepare_enable(yesp->clk);
		if (ret)
			return ret;
	}

	yesp_reset(yesp);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_gen_phy_init);

void usb_gen_phy_shutdown(struct usb_phy *phy)
{
	struct usb_phy_generic *yesp = dev_get_drvdata(phy->dev);

	gpiod_set_value_cansleep(yesp->gpiod_reset, 1);

	if (!IS_ERR(yesp->clk))
		clk_disable_unprepare(yesp->clk);

	if (!IS_ERR(yesp->vcc)) {
		if (regulator_disable(yesp->vcc))
			dev_err(phy->dev, "Failed to disable power\n");
	}
}
EXPORT_SYMBOL_GPL(usb_gen_phy_shutdown);

static int yesp_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -ENODEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -ENODEV;
	}

	otg->gadget = gadget;
	if (otg->state == OTG_STATE_B_PERIPHERAL)
		atomic_yestifier_call_chain(&otg->usb_phy->yestifier,
					   USB_EVENT_VBUS, otg->gadget);
	else
		otg->state = OTG_STATE_B_IDLE;
	return 0;
}

static int yesp_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	if (!otg)
		return -ENODEV;

	if (!host) {
		otg->host = NULL;
		return -ENODEV;
	}

	otg->host = host;
	return 0;
}

int usb_phy_gen_create_phy(struct device *dev, struct usb_phy_generic *yesp,
		struct usb_phy_generic_platform_data *pdata)
{
	enum usb_phy_type type = USB_PHY_TYPE_USB2;
	int err = 0;

	u32 clk_rate = 0;
	bool needs_vcc = false, needs_clk = false;

	if (dev->of_yesde) {
		struct device_yesde *yesde = dev->of_yesde;

		if (of_property_read_u32(yesde, "clock-frequency", &clk_rate))
			clk_rate = 0;

		needs_vcc = of_property_read_bool(yesde, "vcc-supply");
		needs_clk = of_property_read_bool(yesde, "clocks");
		yesp->gpiod_reset = devm_gpiod_get_optional(dev, "reset",
							   GPIOD_ASIS);
		err = PTR_ERR_OR_ZERO(yesp->gpiod_reset);
		if (!err) {
			yesp->gpiod_vbus = devm_gpiod_get_optional(dev,
							 "vbus-detect",
							 GPIOD_ASIS);
			err = PTR_ERR_OR_ZERO(yesp->gpiod_vbus);
		}
	} else if (pdata) {
		type = pdata->type;
		clk_rate = pdata->clk_rate;
		needs_vcc = pdata->needs_vcc;
		if (gpio_is_valid(pdata->gpio_reset)) {
			err = devm_gpio_request_one(dev, pdata->gpio_reset,
						    GPIOF_ACTIVE_LOW,
						    dev_name(dev));
			if (!err)
				yesp->gpiod_reset =
					gpio_to_desc(pdata->gpio_reset);
		}
		yesp->gpiod_vbus = pdata->gpiod_vbus;
	}

	if (err == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (err) {
		dev_err(dev, "Error requesting RESET or VBUS GPIO\n");
		return err;
	}
	if (yesp->gpiod_reset)
		gpiod_direction_output(yesp->gpiod_reset, 1);

	yesp->phy.otg = devm_kzalloc(dev, sizeof(*yesp->phy.otg),
			GFP_KERNEL);
	if (!yesp->phy.otg)
		return -ENOMEM;

	yesp->clk = devm_clk_get(dev, "main_clk");
	if (IS_ERR(yesp->clk)) {
		dev_dbg(dev, "Can't get phy clock: %ld\n",
					PTR_ERR(yesp->clk));
		if (needs_clk)
			return PTR_ERR(yesp->clk);
	}

	if (!IS_ERR(yesp->clk) && clk_rate) {
		err = clk_set_rate(yesp->clk, clk_rate);
		if (err) {
			dev_err(dev, "Error setting clock rate\n");
			return err;
		}
	}

	yesp->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(yesp->vcc)) {
		dev_dbg(dev, "Error getting vcc regulator: %ld\n",
					PTR_ERR(yesp->vcc));
		if (needs_vcc)
			return -EPROBE_DEFER;
	}

	yesp->dev		= dev;
	yesp->phy.dev		= yesp->dev;
	yesp->phy.label		= "yesp-xceiv";
	yesp->phy.set_suspend	= yesp_set_suspend;
	yesp->phy.type		= type;

	yesp->phy.otg->state		= OTG_STATE_UNDEFINED;
	yesp->phy.otg->usb_phy		= &yesp->phy;
	yesp->phy.otg->set_host		= yesp_set_host;
	yesp->phy.otg->set_peripheral	= yesp_set_peripheral;

	return 0;
}
EXPORT_SYMBOL_GPL(usb_phy_gen_create_phy);

static int usb_phy_generic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_phy_generic	*yesp;
	int err;

	yesp = devm_kzalloc(dev, sizeof(*yesp), GFP_KERNEL);
	if (!yesp)
		return -ENOMEM;

	err = usb_phy_gen_create_phy(dev, yesp, dev_get_platdata(&pdev->dev));
	if (err)
		return err;
	if (yesp->gpiod_vbus) {
		err = devm_request_threaded_irq(&pdev->dev,
						gpiod_to_irq(yesp->gpiod_vbus),
						NULL, yesp_gpio_vbus_thread,
						VBUS_IRQ_FLAGS, "vbus_detect",
						yesp);
		if (err) {
			dev_err(&pdev->dev, "can't request irq %i, err: %d\n",
				gpiod_to_irq(yesp->gpiod_vbus), err);
			return err;
		}
		yesp->phy.otg->state = gpiod_get_value(yesp->gpiod_vbus) ?
			OTG_STATE_B_PERIPHERAL : OTG_STATE_B_IDLE;
	}

	yesp->phy.init		= usb_gen_phy_init;
	yesp->phy.shutdown	= usb_gen_phy_shutdown;

	err = usb_add_phy_dev(&yesp->phy);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, yesp);

	return 0;
}

static int usb_phy_generic_remove(struct platform_device *pdev)
{
	struct usb_phy_generic *yesp = platform_get_drvdata(pdev);

	usb_remove_phy(&yesp->phy);

	return 0;
}

static const struct of_device_id yesp_xceiv_dt_ids[] = {
	{ .compatible = "usb-yesp-xceiv" },
	{ }
};

MODULE_DEVICE_TABLE(of, yesp_xceiv_dt_ids);

static struct platform_driver usb_phy_generic_driver = {
	.probe		= usb_phy_generic_probe,
	.remove		= usb_phy_generic_remove,
	.driver		= {
		.name	= "usb_phy_generic",
		.of_match_table = yesp_xceiv_dt_ids,
	},
};

static int __init usb_phy_generic_init(void)
{
	return platform_driver_register(&usb_phy_generic_driver);
}
subsys_initcall(usb_phy_generic_init);

static void __exit usb_phy_generic_exit(void)
{
	platform_driver_unregister(&usb_phy_generic_driver);
}
module_exit(usb_phy_generic_exit);

MODULE_ALIAS("platform:usb_phy_generic");
MODULE_AUTHOR("Texas Instruments Inc");
MODULE_DESCRIPTION("NOP USB Transceiver driver");
MODULE_LICENSE("GPL");
