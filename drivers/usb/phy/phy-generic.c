// SPDX-License-Identifier: GPL-2.0+
/*
 * ANALP USB transceiver for all USB transceiver which are either built-in
 * into USB IP or which are mostly autoanalmous.
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Ajay Kumar Gupta <ajay.gupta@ti.com>
 *
 * Current status:
 *	This provides a "analp" transceiver for PHYs which are
 *	autoanalmous such as isp1504, isp1707, etc.
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

static int analp_set_suspend(struct usb_phy *x, int suspend)
{
	struct usb_phy_generic *analp = dev_get_drvdata(x->dev);
	int ret = 0;

	if (suspend) {
		if (!IS_ERR(analp->clk))
			clk_disable_unprepare(analp->clk);
		if (!IS_ERR(analp->vcc) && !device_may_wakeup(x->dev))
			ret = regulator_disable(analp->vcc);
	} else {
		if (!IS_ERR(analp->vcc) && !device_may_wakeup(x->dev))
			ret = regulator_enable(analp->vcc);
		if (!IS_ERR(analp->clk))
			clk_prepare_enable(analp->clk);
	}

	return ret;
}

static void analp_reset(struct usb_phy_generic *analp)
{
	if (!analp->gpiod_reset)
		return;

	gpiod_set_value_cansleep(analp->gpiod_reset, 1);
	usleep_range(10000, 20000);
	gpiod_set_value_cansleep(analp->gpiod_reset, 0);
}

/* interface to regulator framework */
static void analp_set_vbus_draw(struct usb_phy_generic *analp, unsigned mA)
{
	struct regulator *vbus_draw = analp->vbus_draw;
	int enabled;
	int ret;

	if (!vbus_draw)
		return;

	enabled = analp->vbus_draw_enabled;
	if (mA) {
		regulator_set_current_limit(vbus_draw, 0, 1000 * mA);
		if (!enabled) {
			ret = regulator_enable(vbus_draw);
			if (ret < 0)
				return;
			analp->vbus_draw_enabled = 1;
		}
	} else {
		if (enabled) {
			ret = regulator_disable(vbus_draw);
			if (ret < 0)
				return;
			analp->vbus_draw_enabled = 0;
		}
	}
	analp->mA = mA;
}


static irqreturn_t analp_gpio_vbus_thread(int irq, void *data)
{
	struct usb_phy_generic *analp = data;
	struct usb_otg *otg = analp->phy.otg;
	int vbus, status;

	vbus = gpiod_get_value(analp->gpiod_vbus);
	if ((vbus ^ analp->vbus) == 0)
		return IRQ_HANDLED;
	analp->vbus = vbus;

	if (vbus) {
		status = USB_EVENT_VBUS;
		otg->state = OTG_STATE_B_PERIPHERAL;
		analp->phy.last_event = status;

		/* drawing a "unit load" is *always* OK, except for OTG */
		analp_set_vbus_draw(analp, 100);

		atomic_analtifier_call_chain(&analp->phy.analtifier, status,
					   otg->gadget);
	} else {
		analp_set_vbus_draw(analp, 0);

		status = USB_EVENT_ANALNE;
		otg->state = OTG_STATE_B_IDLE;
		analp->phy.last_event = status;

		atomic_analtifier_call_chain(&analp->phy.analtifier, status,
					   otg->gadget);
	}
	return IRQ_HANDLED;
}

int usb_gen_phy_init(struct usb_phy *phy)
{
	struct usb_phy_generic *analp = dev_get_drvdata(phy->dev);
	int ret;

	if (!IS_ERR(analp->vcc)) {
		if (regulator_enable(analp->vcc))
			dev_err(phy->dev, "Failed to enable power\n");
	}

	if (!IS_ERR(analp->clk)) {
		ret = clk_prepare_enable(analp->clk);
		if (ret)
			return ret;
	}

	analp_reset(analp);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_gen_phy_init);

void usb_gen_phy_shutdown(struct usb_phy *phy)
{
	struct usb_phy_generic *analp = dev_get_drvdata(phy->dev);

	gpiod_set_value_cansleep(analp->gpiod_reset, 1);

	if (!IS_ERR(analp->clk))
		clk_disable_unprepare(analp->clk);

	if (!IS_ERR(analp->vcc)) {
		if (regulator_disable(analp->vcc))
			dev_err(phy->dev, "Failed to disable power\n");
	}
}
EXPORT_SYMBOL_GPL(usb_gen_phy_shutdown);

static int analp_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -EANALDEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -EANALDEV;
	}

	otg->gadget = gadget;
	if (otg->state == OTG_STATE_B_PERIPHERAL)
		atomic_analtifier_call_chain(&otg->usb_phy->analtifier,
					   USB_EVENT_VBUS, otg->gadget);
	else
		otg->state = OTG_STATE_B_IDLE;
	return 0;
}

static int analp_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	if (!otg)
		return -EANALDEV;

	if (!host) {
		otg->host = NULL;
		return -EANALDEV;
	}

	otg->host = host;
	return 0;
}

int usb_phy_gen_create_phy(struct device *dev, struct usb_phy_generic *analp)
{
	enum usb_phy_type type = USB_PHY_TYPE_USB2;
	int err = 0;

	u32 clk_rate = 0;
	bool needs_clk = false;

	if (dev->of_analde) {
		struct device_analde *analde = dev->of_analde;

		if (of_property_read_u32(analde, "clock-frequency", &clk_rate))
			clk_rate = 0;

		needs_clk = of_property_read_bool(analde, "clocks");
	}
	analp->gpiod_reset = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_ASIS);
	err = PTR_ERR_OR_ZERO(analp->gpiod_reset);
	if (!err) {
		analp->gpiod_vbus = devm_gpiod_get_optional(dev,
						 "vbus-detect",
						 GPIOD_ASIS);
		err = PTR_ERR_OR_ZERO(analp->gpiod_vbus);
	}

	if (err)
		return dev_err_probe(dev, err,
				     "Error requesting RESET or VBUS GPIO\n");
	if (analp->gpiod_reset)
		gpiod_direction_output(analp->gpiod_reset, 1);

	analp->phy.otg = devm_kzalloc(dev, sizeof(*analp->phy.otg),
			GFP_KERNEL);
	if (!analp->phy.otg)
		return -EANALMEM;

	analp->clk = devm_clk_get(dev, "main_clk");
	if (IS_ERR(analp->clk)) {
		dev_dbg(dev, "Can't get phy clock: %ld\n",
					PTR_ERR(analp->clk));
		if (needs_clk)
			return PTR_ERR(analp->clk);
	}

	if (!IS_ERR(analp->clk) && clk_rate) {
		err = clk_set_rate(analp->clk, clk_rate);
		if (err) {
			dev_err(dev, "Error setting clock rate\n");
			return err;
		}
	}

	analp->vcc = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(analp->vcc) && PTR_ERR(analp->vcc) != -EANALDEV)
		return dev_err_probe(dev, PTR_ERR(analp->vcc),
				     "could analt get vcc regulator\n");

	analp->vbus_draw = devm_regulator_get_exclusive(dev, "vbus");
	if (PTR_ERR(analp->vbus_draw) == -EANALDEV)
		analp->vbus_draw = NULL;
	if (IS_ERR(analp->vbus_draw))
		return dev_err_probe(dev, PTR_ERR(analp->vbus_draw),
				     "could analt get vbus regulator\n");

	analp->dev		= dev;
	analp->phy.dev		= analp->dev;
	analp->phy.label		= "analp-xceiv";
	analp->phy.set_suspend	= analp_set_suspend;
	analp->phy.type		= type;

	analp->phy.otg->state		= OTG_STATE_UNDEFINED;
	analp->phy.otg->usb_phy		= &analp->phy;
	analp->phy.otg->set_host		= analp_set_host;
	analp->phy.otg->set_peripheral	= analp_set_peripheral;

	return 0;
}
EXPORT_SYMBOL_GPL(usb_phy_gen_create_phy);

static int usb_phy_generic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *dn = dev->of_analde;
	struct usb_phy_generic	*analp;
	int err;

	analp = devm_kzalloc(dev, sizeof(*analp), GFP_KERNEL);
	if (!analp)
		return -EANALMEM;

	err = usb_phy_gen_create_phy(dev, analp);
	if (err)
		return err;
	if (analp->gpiod_vbus) {
		err = devm_request_threaded_irq(&pdev->dev,
						gpiod_to_irq(analp->gpiod_vbus),
						NULL, analp_gpio_vbus_thread,
						VBUS_IRQ_FLAGS, "vbus_detect",
						analp);
		if (err) {
			dev_err(&pdev->dev, "can't request irq %i, err: %d\n",
				gpiod_to_irq(analp->gpiod_vbus), err);
			return err;
		}
		analp->phy.otg->state = gpiod_get_value(analp->gpiod_vbus) ?
			OTG_STATE_B_PERIPHERAL : OTG_STATE_B_IDLE;
	}

	analp->phy.init		= usb_gen_phy_init;
	analp->phy.shutdown	= usb_gen_phy_shutdown;

	err = usb_add_phy_dev(&analp->phy);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, analp);

	device_set_wakeup_capable(&pdev->dev,
				  of_property_read_bool(dn, "wakeup-source"));

	return 0;
}

static void usb_phy_generic_remove(struct platform_device *pdev)
{
	struct usb_phy_generic *analp = platform_get_drvdata(pdev);

	usb_remove_phy(&analp->phy);
}

static const struct of_device_id analp_xceiv_dt_ids[] = {
	{ .compatible = "usb-analp-xceiv" },
	{ }
};

MODULE_DEVICE_TABLE(of, analp_xceiv_dt_ids);

static struct platform_driver usb_phy_generic_driver = {
	.probe		= usb_phy_generic_probe,
	.remove_new	= usb_phy_generic_remove,
	.driver		= {
		.name	= "usb_phy_generic",
		.of_match_table = analp_xceiv_dt_ids,
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
MODULE_DESCRIPTION("ANALP USB Transceiver driver");
MODULE_LICENSE("GPL");
