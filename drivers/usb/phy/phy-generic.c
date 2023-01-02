// SPDX-License-Identifier: GPL-2.0+
/*
 * NOP USB transceiver for all USB transceiver which are either built-in
 * into USB IP or which are mostly autonomous.
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Ajay Kumar Gupta <ajay.gupta@ti.com>
 *
 * Current status:
 *	This provides a "nop" transceiver for PHYs which are
 *	autonomous such as isp1504, isp1707, etc.
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
#include <linux/gpio/consumer.h>
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

static int nop_set_suspend(struct usb_phy *x, int suspend)
{
	struct usb_phy_generic *nop = dev_get_drvdata(x->dev);

	if (!IS_ERR(nop->clk)) {
		if (suspend)
			clk_disable_unprepare(nop->clk);
		else
			clk_prepare_enable(nop->clk);
	}

	return 0;
}

static void nop_reset(struct usb_phy_generic *nop)
{
	if (!nop->gpiod_reset)
		return;

	gpiod_set_value_cansleep(nop->gpiod_reset, 1);
	usleep_range(10000, 20000);
	gpiod_set_value_cansleep(nop->gpiod_reset, 0);
}

/* interface to regulator framework */
static void nop_set_vbus_draw(struct usb_phy_generic *nop, unsigned mA)
{
	struct regulator *vbus_draw = nop->vbus_draw;
	int enabled;
	int ret;

	if (!vbus_draw)
		return;

	enabled = nop->vbus_draw_enabled;
	if (mA) {
		regulator_set_current_limit(vbus_draw, 0, 1000 * mA);
		if (!enabled) {
			ret = regulator_enable(vbus_draw);
			if (ret < 0)
				return;
			nop->vbus_draw_enabled = 1;
		}
	} else {
		if (enabled) {
			ret = regulator_disable(vbus_draw);
			if (ret < 0)
				return;
			nop->vbus_draw_enabled = 0;
		}
	}
	nop->mA = mA;
}


static irqreturn_t nop_gpio_vbus_thread(int irq, void *data)
{
	struct usb_phy_generic *nop = data;
	struct usb_otg *otg = nop->phy.otg;
	int vbus, status;

	vbus = gpiod_get_value(nop->gpiod_vbus);
	if ((vbus ^ nop->vbus) == 0)
		return IRQ_HANDLED;
	nop->vbus = vbus;

	if (vbus) {
		status = USB_EVENT_VBUS;
		otg->state = OTG_STATE_B_PERIPHERAL;
		nop->phy.last_event = status;

		/* drawing a "unit load" is *always* OK, except for OTG */
		nop_set_vbus_draw(nop, 100);

		atomic_notifier_call_chain(&nop->phy.notifier, status,
					   otg->gadget);
	} else {
		nop_set_vbus_draw(nop, 0);

		status = USB_EVENT_NONE;
		otg->state = OTG_STATE_B_IDLE;
		nop->phy.last_event = status;

		atomic_notifier_call_chain(&nop->phy.notifier, status,
					   otg->gadget);
	}
	return IRQ_HANDLED;
}

int usb_gen_phy_init(struct usb_phy *phy)
{
	struct usb_phy_generic *nop = dev_get_drvdata(phy->dev);
	int ret;

	if (!IS_ERR(nop->vcc)) {
		if (regulator_enable(nop->vcc))
			dev_err(phy->dev, "Failed to enable power\n");
	}

	if (!IS_ERR(nop->clk)) {
		ret = clk_prepare_enable(nop->clk);
		if (ret)
			return ret;
	}

	nop_reset(nop);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_gen_phy_init);

void usb_gen_phy_shutdown(struct usb_phy *phy)
{
	struct usb_phy_generic *nop = dev_get_drvdata(phy->dev);

	gpiod_set_value_cansleep(nop->gpiod_reset, 1);

	if (!IS_ERR(nop->clk))
		clk_disable_unprepare(nop->clk);

	if (!IS_ERR(nop->vcc)) {
		if (regulator_disable(nop->vcc))
			dev_err(phy->dev, "Failed to disable power\n");
	}
}
EXPORT_SYMBOL_GPL(usb_gen_phy_shutdown);

static int nop_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -ENODEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -ENODEV;
	}

	otg->gadget = gadget;
	if (otg->state == OTG_STATE_B_PERIPHERAL)
		atomic_notifier_call_chain(&otg->usb_phy->notifier,
					   USB_EVENT_VBUS, otg->gadget);
	else
		otg->state = OTG_STATE_B_IDLE;
	return 0;
}

static int nop_set_host(struct usb_otg *otg, struct usb_bus *host)
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

int usb_phy_gen_create_phy(struct device *dev, struct usb_phy_generic *nop)
{
	enum usb_phy_type type = USB_PHY_TYPE_USB2;
	int err = 0;

	u32 clk_rate = 0;
	bool needs_clk = false;

	if (dev->of_node) {
		struct device_node *node = dev->of_node;

		if (of_property_read_u32(node, "clock-frequency", &clk_rate))
			clk_rate = 0;

		needs_clk = of_property_read_bool(node, "clocks");
	}
	nop->gpiod_reset = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_ASIS);
	err = PTR_ERR_OR_ZERO(nop->gpiod_reset);
	if (!err) {
		nop->gpiod_vbus = devm_gpiod_get_optional(dev,
						 "vbus-detect",
						 GPIOD_ASIS);
		err = PTR_ERR_OR_ZERO(nop->gpiod_vbus);
	}

	if (err)
		return dev_err_probe(dev, err,
				     "Error requesting RESET or VBUS GPIO\n");
	if (nop->gpiod_reset)
		gpiod_direction_output(nop->gpiod_reset, 1);

	nop->phy.otg = devm_kzalloc(dev, sizeof(*nop->phy.otg),
			GFP_KERNEL);
	if (!nop->phy.otg)
		return -ENOMEM;

	nop->clk = devm_clk_get(dev, "main_clk");
	if (IS_ERR(nop->clk)) {
		dev_dbg(dev, "Can't get phy clock: %ld\n",
					PTR_ERR(nop->clk));
		if (needs_clk)
			return PTR_ERR(nop->clk);
	}

	if (!IS_ERR(nop->clk) && clk_rate) {
		err = clk_set_rate(nop->clk, clk_rate);
		if (err) {
			dev_err(dev, "Error setting clock rate\n");
			return err;
		}
	}

	nop->vcc = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(nop->vcc) && PTR_ERR(nop->vcc) != -ENODEV)
		return dev_err_probe(dev, PTR_ERR(nop->vcc),
				     "could not get vcc regulator\n");

	nop->vbus_draw = devm_regulator_get_exclusive(dev, "vbus");
	if (PTR_ERR(nop->vbus_draw) == -ENODEV)
		nop->vbus_draw = NULL;
	if (IS_ERR(nop->vbus_draw))
		return dev_err_probe(dev, PTR_ERR(nop->vbus_draw),
				     "could not get vbus regulator\n");

	nop->dev		= dev;
	nop->phy.dev		= nop->dev;
	nop->phy.label		= "nop-xceiv";
	nop->phy.set_suspend	= nop_set_suspend;
	nop->phy.type		= type;

	nop->phy.otg->state		= OTG_STATE_UNDEFINED;
	nop->phy.otg->usb_phy		= &nop->phy;
	nop->phy.otg->set_host		= nop_set_host;
	nop->phy.otg->set_peripheral	= nop_set_peripheral;

	return 0;
}
EXPORT_SYMBOL_GPL(usb_phy_gen_create_phy);

static int usb_phy_generic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	struct usb_phy_generic	*nop;
	int err;

	nop = devm_kzalloc(dev, sizeof(*nop), GFP_KERNEL);
	if (!nop)
		return -ENOMEM;

	err = usb_phy_gen_create_phy(dev, nop);
	if (err)
		return err;
	if (nop->gpiod_vbus) {
		err = devm_request_threaded_irq(&pdev->dev,
						gpiod_to_irq(nop->gpiod_vbus),
						NULL, nop_gpio_vbus_thread,
						VBUS_IRQ_FLAGS, "vbus_detect",
						nop);
		if (err) {
			dev_err(&pdev->dev, "can't request irq %i, err: %d\n",
				gpiod_to_irq(nop->gpiod_vbus), err);
			return err;
		}
		nop->phy.otg->state = gpiod_get_value(nop->gpiod_vbus) ?
			OTG_STATE_B_PERIPHERAL : OTG_STATE_B_IDLE;
	}

	nop->phy.init		= usb_gen_phy_init;
	nop->phy.shutdown	= usb_gen_phy_shutdown;

	err = usb_add_phy_dev(&nop->phy);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, nop);

	device_set_wakeup_capable(&pdev->dev,
				  of_property_read_bool(dn, "wakeup-source"));

	return 0;
}

static int usb_phy_generic_remove(struct platform_device *pdev)
{
	struct usb_phy_generic *nop = platform_get_drvdata(pdev);

	usb_remove_phy(&nop->phy);

	return 0;
}

static const struct of_device_id nop_xceiv_dt_ids[] = {
	{ .compatible = "usb-nop-xceiv" },
	{ }
};

MODULE_DEVICE_TABLE(of, nop_xceiv_dt_ids);

static struct platform_driver usb_phy_generic_driver = {
	.probe		= usb_phy_generic_probe,
	.remove		= usb_phy_generic_remove,
	.driver		= {
		.name	= "usb_phy_generic",
		.of_match_table = nop_xceiv_dt_ids,
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
