/*
 * gpio-vbus.c - simple GPIO VBUS sensing driver for B peripheral devices
 *
 * Copyright (c) 2008 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include <linux/regulator/consumer.h>

#include <linux/usb/gadget.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/usb/otg.h>


/*
 * A simple GPIO VBUS sensing driver for B peripheral only devices
 * with internal transceivers. It can control a D+ pullup GPIO and
 * a regulator to limit the current drawn from VBUS.
 *
 * Needs to be loaded before the UDC driver that will use it.
 */
struct gpio_vbus_data {
	struct otg_transceiver otg;
	struct device          *dev;
	struct regulator       *vbus_draw;
	int			vbus_draw_enabled;
	unsigned		mA;
	struct work_struct	work;
};


/*
 * This driver relies on "both edges" triggering.  VBUS has 100 msec to
 * stabilize, so the peripheral controller driver may need to cope with
 * some bouncing due to current surges (e.g. charging local capacitance)
 * and contact chatter.
 *
 * REVISIT in desperate straits, toggling between rising and falling
 * edges might be workable.
 */
#define VBUS_IRQ_FLAGS \
	( IRQF_SAMPLE_RANDOM | IRQF_SHARED \
	| IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING )


/* interface to regulator framework */
static void set_vbus_draw(struct gpio_vbus_data *gpio_vbus, unsigned mA)
{
	struct regulator *vbus_draw = gpio_vbus->vbus_draw;
	int enabled;

	if (!vbus_draw)
		return;

	enabled = gpio_vbus->vbus_draw_enabled;
	if (mA) {
		regulator_set_current_limit(vbus_draw, 0, 1000 * mA);
		if (!enabled) {
			regulator_enable(vbus_draw);
			gpio_vbus->vbus_draw_enabled = 1;
		}
	} else {
		if (enabled) {
			regulator_disable(vbus_draw);
			gpio_vbus->vbus_draw_enabled = 0;
		}
	}
	gpio_vbus->mA = mA;
}

static int is_vbus_powered(struct gpio_vbus_mach_info *pdata)
{
	int vbus;

	vbus = gpio_get_value(pdata->gpio_vbus);
	if (pdata->gpio_vbus_inverted)
		vbus = !vbus;

	return vbus;
}

static void gpio_vbus_work(struct work_struct *work)
{
	struct gpio_vbus_data *gpio_vbus =
		container_of(work, struct gpio_vbus_data, work);
	struct gpio_vbus_mach_info *pdata = gpio_vbus->dev->platform_data;
	int gpio;

	if (!gpio_vbus->otg.gadget)
		return;

	/* Peripheral controllers which manage the pullup themselves won't have
	 * gpio_pullup configured here.  If it's configured here, we'll do what
	 * isp1301_omap::b_peripheral() does and enable the pullup here... although
	 * that may complicate usb_gadget_{,dis}connect() support.
	 */
	gpio = pdata->gpio_pullup;
	if (is_vbus_powered(pdata)) {
		gpio_vbus->otg.state = OTG_STATE_B_PERIPHERAL;
		usb_gadget_vbus_connect(gpio_vbus->otg.gadget);

		/* drawing a "unit load" is *always* OK, except for OTG */
		set_vbus_draw(gpio_vbus, 100);

		/* optionally enable D+ pullup */
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, !pdata->gpio_pullup_inverted);
	} else {
		/* optionally disable D+ pullup */
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, pdata->gpio_pullup_inverted);

		set_vbus_draw(gpio_vbus, 0);

		usb_gadget_vbus_disconnect(gpio_vbus->otg.gadget);
		gpio_vbus->otg.state = OTG_STATE_B_IDLE;
	}
}

/* VBUS change IRQ handler */
static irqreturn_t gpio_vbus_irq(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct gpio_vbus_mach_info *pdata = pdev->dev.platform_data;
	struct gpio_vbus_data *gpio_vbus = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "VBUS %s (gadget: %s)\n",
		is_vbus_powered(pdata) ? "supplied" : "inactive",
		gpio_vbus->otg.gadget ? gpio_vbus->otg.gadget->name : "none");

	if (gpio_vbus->otg.gadget)
		schedule_work(&gpio_vbus->work);

	return IRQ_HANDLED;
}

/* OTG transceiver interface */

/* bind/unbind the peripheral controller */
static int gpio_vbus_set_peripheral(struct otg_transceiver *otg,
				struct usb_gadget *gadget)
{
	struct gpio_vbus_data *gpio_vbus;
	struct gpio_vbus_mach_info *pdata;
	struct platform_device *pdev;
	int gpio, irq;

	gpio_vbus = container_of(otg, struct gpio_vbus_data, otg);
	pdev = to_platform_device(gpio_vbus->dev);
	pdata = gpio_vbus->dev->platform_data;
	irq = gpio_to_irq(pdata->gpio_vbus);
	gpio = pdata->gpio_pullup;

	if (!gadget) {
		dev_dbg(&pdev->dev, "unregistering gadget '%s'\n",
			otg->gadget->name);

		/* optionally disable D+ pullup */
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, pdata->gpio_pullup_inverted);

		set_vbus_draw(gpio_vbus, 0);

		usb_gadget_vbus_disconnect(otg->gadget);
		otg->state = OTG_STATE_UNDEFINED;

		otg->gadget = NULL;
		return 0;
	}

	otg->gadget = gadget;
	dev_dbg(&pdev->dev, "registered gadget '%s'\n", gadget->name);

	/* initialize connection state */
	gpio_vbus_irq(irq, pdev);
	return 0;
}

/* effective for B devices, ignored for A-peripheral */
static int gpio_vbus_set_power(struct otg_transceiver *otg, unsigned mA)
{
	struct gpio_vbus_data *gpio_vbus;

	gpio_vbus = container_of(otg, struct gpio_vbus_data, otg);

	if (otg->state == OTG_STATE_B_PERIPHERAL)
		set_vbus_draw(gpio_vbus, mA);
	return 0;
}

/* for non-OTG B devices: set/clear transceiver suspend mode */
static int gpio_vbus_set_suspend(struct otg_transceiver *otg, int suspend)
{
	struct gpio_vbus_data *gpio_vbus;

	gpio_vbus = container_of(otg, struct gpio_vbus_data, otg);

	/* draw max 0 mA from vbus in suspend mode; or the previously
	 * recorded amount of current if not suspended
	 *
	 * NOTE: high powered configs (mA > 100) may draw up to 2.5 mA
	 * if they're wake-enabled ... we don't handle that yet.
	 */
	return gpio_vbus_set_power(otg, suspend ? 0 : gpio_vbus->mA);
}

/* platform driver interface */

static int __init gpio_vbus_probe(struct platform_device *pdev)
{
	struct gpio_vbus_mach_info *pdata = pdev->dev.platform_data;
	struct gpio_vbus_data *gpio_vbus;
	struct resource *res;
	int err, gpio, irq;

	if (!pdata || !gpio_is_valid(pdata->gpio_vbus))
		return -EINVAL;
	gpio = pdata->gpio_vbus;

	gpio_vbus = kzalloc(sizeof(struct gpio_vbus_data), GFP_KERNEL);
	if (!gpio_vbus)
		return -ENOMEM;

	platform_set_drvdata(pdev, gpio_vbus);
	gpio_vbus->dev = &pdev->dev;
	gpio_vbus->otg.label = "gpio-vbus";
	gpio_vbus->otg.state = OTG_STATE_UNDEFINED;
	gpio_vbus->otg.set_peripheral = gpio_vbus_set_peripheral;
	gpio_vbus->otg.set_power = gpio_vbus_set_power;
	gpio_vbus->otg.set_suspend = gpio_vbus_set_suspend;

	err = gpio_request(gpio, "vbus_detect");
	if (err) {
		dev_err(&pdev->dev, "can't request vbus gpio %d, err: %d\n",
			gpio, err);
		goto err_gpio;
	}
	gpio_direction_input(gpio);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res) {
		irq = res->start;
		res->flags &= IRQF_TRIGGER_MASK;
		res->flags |= IRQF_SAMPLE_RANDOM | IRQF_SHARED;
	} else
		irq = gpio_to_irq(gpio);

	/* if data line pullup is in use, initialize it to "not pulling up" */
	gpio = pdata->gpio_pullup;
	if (gpio_is_valid(gpio)) {
		err = gpio_request(gpio, "udc_pullup");
		if (err) {
			dev_err(&pdev->dev,
				"can't request pullup gpio %d, err: %d\n",
				gpio, err);
			gpio_free(pdata->gpio_vbus);
			goto err_gpio;
		}
		gpio_direction_output(gpio, pdata->gpio_pullup_inverted);
	}

	err = request_irq(irq, gpio_vbus_irq, VBUS_IRQ_FLAGS,
		"vbus_detect", pdev);
	if (err) {
		dev_err(&pdev->dev, "can't request irq %i, err: %d\n",
			irq, err);
		goto err_irq;
	}
	INIT_WORK(&gpio_vbus->work, gpio_vbus_work);

	gpio_vbus->vbus_draw = regulator_get(&pdev->dev, "vbus_draw");
	if (IS_ERR(gpio_vbus->vbus_draw)) {
		dev_dbg(&pdev->dev, "can't get vbus_draw regulator, err: %ld\n",
			PTR_ERR(gpio_vbus->vbus_draw));
		gpio_vbus->vbus_draw = NULL;
	}

	/* only active when a gadget is registered */
	err = otg_set_transceiver(&gpio_vbus->otg);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		goto err_otg;
	}

	return 0;
err_otg:
	free_irq(irq, &pdev->dev);
err_irq:
	if (gpio_is_valid(pdata->gpio_pullup))
		gpio_free(pdata->gpio_pullup);
	gpio_free(pdata->gpio_vbus);
err_gpio:
	platform_set_drvdata(pdev, NULL);
	kfree(gpio_vbus);
	return err;
}

static int __exit gpio_vbus_remove(struct platform_device *pdev)
{
	struct gpio_vbus_data *gpio_vbus = platform_get_drvdata(pdev);
	struct gpio_vbus_mach_info *pdata = pdev->dev.platform_data;
	int gpio = pdata->gpio_vbus;

	regulator_put(gpio_vbus->vbus_draw);

	otg_set_transceiver(NULL);

	free_irq(gpio_to_irq(gpio), &pdev->dev);
	if (gpio_is_valid(pdata->gpio_pullup))
		gpio_free(pdata->gpio_pullup);
	gpio_free(gpio);
	platform_set_drvdata(pdev, NULL);
	kfree(gpio_vbus);

	return 0;
}

/* NOTE:  the gpio-vbus device may *NOT* be hotplugged */

MODULE_ALIAS("platform:gpio-vbus");

static struct platform_driver gpio_vbus_driver = {
	.driver = {
		.name  = "gpio-vbus",
		.owner = THIS_MODULE,
	},
	.remove  = __exit_p(gpio_vbus_remove),
};

static int __init gpio_vbus_init(void)
{
	return platform_driver_probe(&gpio_vbus_driver, gpio_vbus_probe);
}
module_init(gpio_vbus_init);

static void __exit gpio_vbus_exit(void)
{
	platform_driver_unregister(&gpio_vbus_driver);
}
module_exit(gpio_vbus_exit);

MODULE_DESCRIPTION("simple GPIO controlled OTG transceiver driver");
MODULE_AUTHOR("Philipp Zabel");
MODULE_LICENSE("GPL");
