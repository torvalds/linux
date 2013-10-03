/*
 * Palmas USB transceiver driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * Based on twl6030_usb.c
 *
 * Author: Hema HK <hemahk@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mfd/palmas.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static const char *palmas_extcon_cable[] = {
	[0] = "USB",
	[1] = "USB-HOST",
	NULL,
};

static const int mutually_exclusive[] = {0x3, 0x0};

static void palmas_usb_wakeup(struct palmas *palmas, int enable)
{
	if (enable)
		palmas_write(palmas, PALMAS_USB_OTG_BASE, PALMAS_USB_WAKEUP,
			PALMAS_USB_WAKEUP_ID_WK_UP_COMP);
	else
		palmas_write(palmas, PALMAS_USB_OTG_BASE, PALMAS_USB_WAKEUP, 0);
}

static irqreturn_t palmas_vbus_irq_handler(int irq, void *_palmas_usb)
{
	struct palmas_usb *palmas_usb = _palmas_usb;
	unsigned int vbus_line_state;

	palmas_read(palmas_usb->palmas, PALMAS_INTERRUPT_BASE,
		PALMAS_INT3_LINE_STATE, &vbus_line_state);

	if (vbus_line_state & PALMAS_INT3_LINE_STATE_VBUS) {
		if (palmas_usb->linkstat != PALMAS_USB_STATE_VBUS) {
			palmas_usb->linkstat = PALMAS_USB_STATE_VBUS;
			extcon_set_cable_state(&palmas_usb->edev, "USB", true);
			dev_info(palmas_usb->dev, "USB cable is attached\n");
		} else {
			dev_dbg(palmas_usb->dev,
				"Spurious connect event detected\n");
		}
	} else if (!(vbus_line_state & PALMAS_INT3_LINE_STATE_VBUS)) {
		if (palmas_usb->linkstat == PALMAS_USB_STATE_VBUS) {
			palmas_usb->linkstat = PALMAS_USB_STATE_DISCONNECT;
			extcon_set_cable_state(&palmas_usb->edev, "USB", false);
			dev_info(palmas_usb->dev, "USB cable is detached\n");
		} else {
			dev_dbg(palmas_usb->dev,
				"Spurious disconnect event detected\n");
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t palmas_id_irq_handler(int irq, void *_palmas_usb)
{
	unsigned int set;
	struct palmas_usb *palmas_usb = _palmas_usb;

	palmas_read(palmas_usb->palmas, PALMAS_USB_OTG_BASE,
		PALMAS_USB_ID_INT_LATCH_SET, &set);

	if (set & PALMAS_USB_ID_INT_SRC_ID_GND) {
		palmas_write(palmas_usb->palmas, PALMAS_USB_OTG_BASE,
			PALMAS_USB_ID_INT_LATCH_CLR,
			PALMAS_USB_ID_INT_EN_HI_CLR_ID_GND);
		palmas_usb->linkstat = PALMAS_USB_STATE_ID;
		extcon_set_cable_state(&palmas_usb->edev, "USB-HOST", true);
		dev_info(palmas_usb->dev, "USB-HOST cable is attached\n");
	} else if (set & PALMAS_USB_ID_INT_SRC_ID_FLOAT) {
		palmas_write(palmas_usb->palmas, PALMAS_USB_OTG_BASE,
			PALMAS_USB_ID_INT_LATCH_CLR,
			PALMAS_USB_ID_INT_EN_HI_CLR_ID_FLOAT);
		palmas_usb->linkstat = PALMAS_USB_STATE_DISCONNECT;
		extcon_set_cable_state(&palmas_usb->edev, "USB-HOST", false);
		dev_info(palmas_usb->dev, "USB-HOST cable is detached\n");
	} else if ((palmas_usb->linkstat == PALMAS_USB_STATE_ID) &&
				(!(set & PALMAS_USB_ID_INT_SRC_ID_GND))) {
		palmas_usb->linkstat = PALMAS_USB_STATE_DISCONNECT;
		extcon_set_cable_state(&palmas_usb->edev, "USB-HOST", false);
		dev_info(palmas_usb->dev, "USB-HOST cable is detached\n");
	}

	return IRQ_HANDLED;
}

static void palmas_enable_irq(struct palmas_usb *palmas_usb)
{
	palmas_write(palmas_usb->palmas, PALMAS_USB_OTG_BASE,
		PALMAS_USB_VBUS_CTRL_SET,
		PALMAS_USB_VBUS_CTRL_SET_VBUS_ACT_COMP);

	palmas_write(palmas_usb->palmas, PALMAS_USB_OTG_BASE,
		PALMAS_USB_ID_CTRL_SET, PALMAS_USB_ID_CTRL_SET_ID_ACT_COMP);

	palmas_write(palmas_usb->palmas, PALMAS_USB_OTG_BASE,
		PALMAS_USB_ID_INT_EN_HI_SET,
		PALMAS_USB_ID_INT_EN_HI_SET_ID_GND |
		PALMAS_USB_ID_INT_EN_HI_SET_ID_FLOAT);

	if (palmas_usb->enable_vbus_detection)
		palmas_vbus_irq_handler(palmas_usb->vbus_irq, palmas_usb);

	/* cold plug for host mode needs this delay */
	if (palmas_usb->enable_id_detection) {
		msleep(30);
		palmas_id_irq_handler(palmas_usb->id_irq, palmas_usb);
	}
}

static int palmas_usb_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_usb_platform_data	*pdata = pdev->dev.platform_data;
	struct device_node *node = pdev->dev.of_node;
	struct palmas_usb *palmas_usb;
	int status;

	palmas_usb = devm_kzalloc(&pdev->dev, sizeof(*palmas_usb), GFP_KERNEL);
	if (!palmas_usb)
		return -ENOMEM;

	if (node && !pdata) {
		palmas_usb->wakeup = of_property_read_bool(node, "ti,wakeup");
		palmas_usb->enable_id_detection = of_property_read_bool(node,
						"ti,enable-id-detection");
		palmas_usb->enable_vbus_detection = of_property_read_bool(node,
						"ti,enable-vbus-detection");
	} else {
		palmas_usb->wakeup = true;
		palmas_usb->enable_id_detection = true;
		palmas_usb->enable_vbus_detection = true;

		if (pdata)
			palmas_usb->wakeup = pdata->wakeup;
	}

	palmas->usb = palmas_usb;
	palmas_usb->palmas = palmas;

	palmas_usb->dev	 = &pdev->dev;

	palmas_usb->id_otg_irq = regmap_irq_get_virq(palmas->irq_data,
						PALMAS_ID_OTG_IRQ);
	palmas_usb->id_irq = regmap_irq_get_virq(palmas->irq_data,
						PALMAS_ID_IRQ);
	palmas_usb->vbus_otg_irq = regmap_irq_get_virq(palmas->irq_data,
						PALMAS_VBUS_OTG_IRQ);
	palmas_usb->vbus_irq = regmap_irq_get_virq(palmas->irq_data,
						PALMAS_VBUS_IRQ);

	palmas_usb_wakeup(palmas, palmas_usb->wakeup);

	platform_set_drvdata(pdev, palmas_usb);

	palmas_usb->edev.supported_cable = palmas_extcon_cable;
	palmas_usb->edev.mutually_exclusive = mutually_exclusive;

	status = extcon_dev_register(&palmas_usb->edev, palmas_usb->dev);
	if (status) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		return status;
	}

	if (palmas_usb->enable_id_detection) {
		status = devm_request_threaded_irq(palmas_usb->dev,
				palmas_usb->id_irq,
				NULL, palmas_id_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				IRQF_ONESHOT | IRQF_EARLY_RESUME,
				"palmas_usb_id", palmas_usb);
		if (status < 0) {
			dev_err(&pdev->dev, "can't get IRQ %d, err %d\n",
					palmas_usb->id_irq, status);
			goto fail_extcon;
		}
	}

	if (palmas_usb->enable_vbus_detection) {
		status = devm_request_threaded_irq(palmas_usb->dev,
				palmas_usb->vbus_irq, NULL,
				palmas_vbus_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				IRQF_ONESHOT | IRQF_EARLY_RESUME,
				"palmas_usb_vbus", palmas_usb);
		if (status < 0) {
			dev_err(&pdev->dev, "can't get IRQ %d, err %d\n",
					palmas_usb->vbus_irq, status);
			goto fail_extcon;
		}
	}

	palmas_enable_irq(palmas_usb);
	device_set_wakeup_capable(&pdev->dev, true);
	return 0;

fail_extcon:
	extcon_dev_unregister(&palmas_usb->edev);

	return status;
}

static int palmas_usb_remove(struct platform_device *pdev)
{
	struct palmas_usb *palmas_usb = platform_get_drvdata(pdev);

	extcon_dev_unregister(&palmas_usb->edev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int palmas_usb_suspend(struct device *dev)
{
	struct palmas_usb *palmas_usb = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (palmas_usb->enable_vbus_detection)
			enable_irq_wake(palmas_usb->vbus_irq);
		if (palmas_usb->enable_id_detection)
			enable_irq_wake(palmas_usb->id_irq);
	}
	return 0;
}

static int palmas_usb_resume(struct device *dev)
{
	struct palmas_usb *palmas_usb = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (palmas_usb->enable_vbus_detection)
			disable_irq_wake(palmas_usb->vbus_irq);
		if (palmas_usb->enable_id_detection)
			disable_irq_wake(palmas_usb->id_irq);
	}
	return 0;
};
#endif

static const struct dev_pm_ops palmas_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(palmas_usb_suspend,
				palmas_usb_resume)
};

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-usb", },
	{ .compatible = "ti,twl6035-usb", },
	{ /* end */ }
};

static struct platform_driver palmas_usb_driver = {
	.probe = palmas_usb_probe,
	.remove = palmas_usb_remove,
	.driver = {
		.name = "palmas-usb",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
		.pm = &palmas_pm_ops,
	},
};

module_platform_driver(palmas_usb_driver);

MODULE_ALIAS("platform:palmas-usb");
MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas USB transceiver driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
