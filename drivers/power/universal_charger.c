/*
 * universal charger driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/extcon.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/rk_usbbc.h>
#include <linux/property.h>
#include <linux/workqueue.h>

enum charger_t {
	USB_TYPE_UNKNOWN_CHARGER,
	USB_TYPE_NONE_CHARGER,
	USB_TYPE_USB_CHARGER,
	USB_TYPE_AC_CHARGER,
	USB_TYPE_CDP_CHARGER,
	DC_TYPE_DC_CHARGER,
	DC_TYPE_NONE_CHARGER,
};

struct universal_charger {
	struct device *dev;
	struct power_supply *usb_psy;
	struct workqueue_struct *usb_charger_wq;
	struct delayed_work usb_work;
	struct workqueue_struct *dc_charger_wq;
	struct delayed_work dc_work;
	struct delayed_work discnt_work;
	struct notifier_block cable_cg_nb;
	struct notifier_block cable_discnt_nb;
	unsigned int bc_event;
	enum charger_t usb_charger;
	enum charger_t dc_charger;
	bool extcon;
	struct extcon_dev *cable_edev;
	struct gpio_desc *dc_det_pin;
	bool support_dc_det;
};

static void universal_cg_bc_evt_worker(struct work_struct *work)
{
	struct universal_charger *cg =
		container_of(work, struct universal_charger, usb_work.work);
	struct extcon_dev *edev = cg->cable_edev;
	enum charger_t charger = USB_TYPE_UNKNOWN_CHARGER;
	const char *event[5] = {"UN", "NONE", "USB", "AC", "CDP1.5A"};

	/* Determine cable/charger type */
	if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_SDP) > 0)
		charger = USB_TYPE_USB_CHARGER;
	else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_DCP) > 0)
		charger = USB_TYPE_AC_CHARGER;
	else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_CDP) > 0)
		charger = USB_TYPE_CDP_CHARGER;
	else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_DCP) == 0)
		charger = USB_TYPE_NONE_CHARGER;
	else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_CDP) == 0)
		charger = USB_TYPE_NONE_CHARGER;

	if (charger != USB_TYPE_UNKNOWN_CHARGER) {
		dev_info(cg->dev, "receive usb notifier event: %s...\n",
			 event[charger]);
		cg->usb_charger = charger;
	}
}

static int universal_cg_charger_evt_notifier(struct notifier_block *nb,
					     unsigned long event,
					     void *ptr)
{
	struct universal_charger *cg =
		container_of(nb, struct universal_charger, cable_cg_nb);

	queue_delayed_work(cg->usb_charger_wq, &cg->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static void universal_cg_discnt_evt_worker(struct work_struct *work)
{
	struct universal_charger *cg = container_of(work,
			struct universal_charger, discnt_work.work);

	if (extcon_get_cable_state_(cg->cable_edev, EXTCON_USB) == 0) {
		dev_info(cg->dev, "receive usb notifier event: DISCNT...\n");
		cg->usb_charger = USB_TYPE_NONE_CHARGER;
	}
}

static int universal_cg_discnt_evt_notfier(struct notifier_block *nb,
					   unsigned long event, void *ptr)
{
	struct universal_charger *cg =
		container_of(nb, struct universal_charger, cable_discnt_nb);

	queue_delayed_work(cg->usb_charger_wq, &cg->discnt_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int universal_cg_init_usb(struct universal_charger *cg)
{
	struct device *dev = cg->dev;
	int ret;
	struct extcon_dev *edev;

	if (!cg->extcon)
		return 0;

	edev = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR(edev)) {
		if (PTR_ERR(edev) != -EPROBE_DEFER)
			dev_err(dev, "Invalid or missing extcon\n");
		return PTR_ERR(edev);
	}

	cg->usb_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"universal-usb-wq");
	if (!cg->usb_charger_wq)
		return -ENOMEM;

	cg->usb_charger = USB_TYPE_NONE_CHARGER;

	/* Register chargers  */
	INIT_DELAYED_WORK(&cg->usb_work, universal_cg_bc_evt_worker);
	cg->cable_cg_nb.notifier_call = universal_cg_charger_evt_notifier;
	ret = devm_extcon_register_notifier(dev, edev, EXTCON_CHG_USB_SDP,
					    &cg->cable_cg_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for SDP\n");
		goto __fail;
	}

	ret = devm_extcon_register_notifier(dev, edev, EXTCON_CHG_USB_DCP,
					    &cg->cable_cg_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for DCP\n");
		goto __fail;
	}

	ret = devm_extcon_register_notifier(dev, edev, EXTCON_CHG_USB_CDP,
					    &cg->cable_cg_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for CDP\n");
		goto __fail;
	}

	/* Register discnt usb */
	INIT_DELAYED_WORK(&cg->discnt_work, universal_cg_discnt_evt_worker);
	cg->cable_discnt_nb.notifier_call = universal_cg_discnt_evt_notfier;
	ret = devm_extcon_register_notifier(dev, edev, EXTCON_USB,
					    &cg->cable_discnt_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for HOST\n");
		goto __fail;
	}

	cg->cable_edev = edev;
	schedule_delayed_work(&cg->usb_work, 0);
	dev_info(cg->dev, "register extcon evt notifier\n");

	return 0;

__fail:
	destroy_workqueue(cg->usb_charger_wq);
	return ret;
}

static enum power_supply_property universal_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
};

static int universal_cg_usb_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct universal_charger *cg = power_supply_get_drvdata(psy);
	int online = 0;
	int ret = 0;

	if (cg->usb_charger != USB_TYPE_UNKNOWN_CHARGER &&
	    cg->usb_charger != USB_TYPE_NONE_CHARGER)
		online = 1;
	if (cg->dc_charger != DC_TYPE_NONE_CHARGER)
		online = 1;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = online;

		dev_dbg(cg->dev, "report online: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (online)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;

		dev_dbg(cg->dev, "report prop: %d\n", val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct power_supply_desc universal_usb_desc = {
	.name		= "universal_usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= universal_usb_props,
	.num_properties	= ARRAY_SIZE(universal_usb_props),
	.get_property	= universal_cg_usb_get_property,
};

static int universal_cg_init_power_supply(struct universal_charger *cg)
{
	struct power_supply_config psy_cfg = { .drv_data = cg, };

	cg->usb_psy = devm_power_supply_register(cg->dev, &universal_usb_desc,
						 &psy_cfg);
	if (IS_ERR(cg->usb_psy)) {
		dev_err(cg->dev, "register usb power supply fail\n");
		return PTR_ERR(cg->usb_psy);
	}

	return 0;
}

#ifdef CONFIG_OF
static int universal_charger_parse_dt(struct universal_charger *cg)
{
	struct device *dev = cg->dev;

	cg->dc_det_pin = devm_gpiod_get_optional(dev, "dc-det",
						    GPIOD_IN);
	if (!IS_ERR_OR_NULL(cg->dc_det_pin)) {
		cg->support_dc_det = true;
	} else {
		dev_err(dev, "invalid dc det gpio!\n");
		cg->support_dc_det = false;
	}

	return 0;
}
#else
static int universal_charger_parse_dt(struct universal_charger *cg)
{
	return -ENODEV;
}
#endif

static enum
charger_t universal_charger_get_dc_state(struct universal_charger *cg)
{
	return (gpiod_get_value(cg->dc_det_pin)) ?
		DC_TYPE_DC_CHARGER : DC_TYPE_NONE_CHARGER;
}

static void universal_charger_dc_det_worker(struct work_struct *work)
{
	enum charger_t charger;
	struct universal_charger *cg = container_of(work,
			struct universal_charger, dc_work.work);

	charger = universal_charger_get_dc_state(cg);
	if (charger == DC_TYPE_DC_CHARGER)
		cg->dc_charger = charger;
	else
		cg->dc_charger = DC_TYPE_NONE_CHARGER;
}

static irqreturn_t universal_charger_dc_det_isr(int irq, void *charger)
{
	struct universal_charger *cg = (struct universal_charger *)charger;

	queue_delayed_work(cg->dc_charger_wq, &cg->dc_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static int universal_charger_init_dc(struct universal_charger *cg)
{
	int ret;
	unsigned long irq_flags;
	unsigned int dc_det_irq;

	if (!cg->support_dc_det)
		return 0;

	cg->dc_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"universal-dc-wq");
	if (!cg->dc_charger_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&cg->dc_work, universal_charger_dc_det_worker);
	cg->dc_charger = DC_TYPE_NONE_CHARGER;

	if (gpiod_get_value(cg->dc_det_pin))
		cg->dc_charger = DC_TYPE_DC_CHARGER;
	else
		cg->dc_charger = DC_TYPE_NONE_CHARGER;

	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	dc_det_irq = gpiod_to_irq(cg->dc_det_pin);
	ret = devm_request_irq(cg->dev, dc_det_irq,
			       universal_charger_dc_det_isr,
			       irq_flags, "universal_dc_det", cg);
	if (ret != 0) {
		destroy_workqueue(cg->dc_charger_wq);
		dev_err(cg->dev, "universal_dc_det_irq request failed!\n");
		return ret;
	}

	enable_irq_wake(dc_det_irq);

	return 0;
}

static int universal_charger_probe(struct platform_device *pdev)
{
	struct universal_charger *cg;
	int ret;

	cg = devm_kzalloc(&pdev->dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return -ENOMEM;

	cg->dev = &pdev->dev;
	universal_charger_parse_dt(cg);
	ret = universal_charger_init_dc(cg);
	if (ret) {
		dev_err(cg->dev, "init dc failed!\n");
		return ret;
	}
	cg->extcon = device_property_read_bool(cg->dev, "extcon");
	ret = universal_cg_init_usb(cg);
	if (ret) {
		dev_err(cg->dev, "init usb failed!\n");
		goto __init_usb_fail;
	}

	ret = universal_cg_init_power_supply(cg);
	if (ret) {
		dev_err(cg->dev, "init power supply fail!\n");
		goto ___init_psy_fail;
	}

	dev_info(cg->dev, "driver registered\n");

	return 0;

___init_psy_fail:
	if (cg->usb_charger_wq)
		destroy_workqueue(cg->usb_charger_wq);
__init_usb_fail:
	if (cg->dc_charger_wq)
		destroy_workqueue(cg->dc_charger_wq);

	return ret;
}

static int universal_charger_remove(struct platform_device *pdev)
{
	struct universal_charger *cg = platform_get_drvdata(pdev);

	if (cg->usb_charger_wq)
		destroy_workqueue(cg->usb_charger_wq);
	if (cg->dc_charger_wq)
		destroy_workqueue(cg->dc_charger_wq);

	return 0;
}

static const struct of_device_id universal_charger_match[] = {
	{
		.compatible = "universal-charger",
	},
	{},
};

static struct platform_driver universal_charger_driver = {
	.probe = universal_charger_probe,
	.remove = universal_charger_remove,
	.driver = {
		.name	= "universal-charger",
		.of_match_table = universal_charger_match,
	},
};

module_platform_driver(universal_charger_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:universal-charger");
MODULE_AUTHOR("chen Shunqing<csq@rock-chips.com>");
