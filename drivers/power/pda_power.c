/*
 * Common power driver for PDAs and phones with one or two external
 * power supplies (AC/USB) connected to main and backup batteries,
 * and optional builtin charger.
 *
 * Copyright © 2007 Anton Vorontsov <cbou@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/pda_power.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

static inline unsigned int get_irq_flags(struct resource *res)
{
	unsigned int flags = IRQF_DISABLED | IRQF_SHARED;

	flags |= res->flags & IRQF_TRIGGER_MASK;

	return flags;
}

static struct device *dev;
static struct pda_power_pdata *pdata;
static struct resource *ac_irq, *usb_irq;
static struct timer_list charger_timer;
static struct timer_list supply_timer;

static int pda_power_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = pdata->is_ac_online ?
				      pdata->is_ac_online() : 0;
		else
			val->intval = pdata->is_usb_online ?
				      pdata->is_usb_online() : 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property pda_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *pda_power_supplied_to[] = {
	"main-battery",
	"backup-battery",
};

static struct power_supply pda_power_supplies[] = {
	{
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to = pda_power_supplied_to,
		.num_supplicants = ARRAY_SIZE(pda_power_supplied_to),
		.properties = pda_power_props,
		.num_properties = ARRAY_SIZE(pda_power_props),
		.get_property = pda_power_get_property,
	},
	{
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.supplied_to = pda_power_supplied_to,
		.num_supplicants = ARRAY_SIZE(pda_power_supplied_to),
		.properties = pda_power_props,
		.num_properties = ARRAY_SIZE(pda_power_props),
		.get_property = pda_power_get_property,
	},
};

static void update_charger(void)
{
	if (!pdata->set_charge)
		return;

	if (pdata->is_ac_online && pdata->is_ac_online()) {
		dev_dbg(dev, "charger on (AC)\n");
		pdata->set_charge(PDA_POWER_CHARGE_AC);
	} else if (pdata->is_usb_online && pdata->is_usb_online()) {
		dev_dbg(dev, "charger on (USB)\n");
		pdata->set_charge(PDA_POWER_CHARGE_USB);
	} else {
		dev_dbg(dev, "charger off\n");
		pdata->set_charge(0);
	}
}

static void supply_timer_func(unsigned long power_supply_ptr)
{
	void *power_supply = (void *)power_supply_ptr;

	power_supply_changed(power_supply);
}

static void charger_timer_func(unsigned long power_supply_ptr)
{
	update_charger();

	/* Okay, charger set. Now wait a bit before notifying supplicants,
	 * charge power should stabilize. */
	supply_timer.data = power_supply_ptr;
	mod_timer(&supply_timer,
		  jiffies + msecs_to_jiffies(pdata->wait_for_charger));
}

static irqreturn_t power_changed_isr(int irq, void *power_supply)
{
	/* Wait a bit before reading ac/usb line status and setting charger,
	 * because ac/usb status readings may lag from irq. */
	charger_timer.data = (unsigned long)power_supply;
	mod_timer(&charger_timer,
		  jiffies + msecs_to_jiffies(pdata->wait_for_status));
	return IRQ_HANDLED;
}

static int pda_power_probe(struct platform_device *pdev)
{
	int ret = 0;

	dev = &pdev->dev;

	if (pdev->id != -1) {
		dev_err(dev, "it's meaningless to register several "
			"pda_powers; use id = -1\n");
		ret = -EINVAL;
		goto wrongid;
	}

	pdata = pdev->dev.platform_data;

	update_charger();

	if (!pdata->wait_for_status)
		pdata->wait_for_status = 500;

	if (!pdata->wait_for_charger)
		pdata->wait_for_charger = 500;

	setup_timer(&charger_timer, charger_timer_func, 0);
	setup_timer(&supply_timer, supply_timer_func, 0);

	ac_irq = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "ac");
	usb_irq = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "usb");
	if (!ac_irq && !usb_irq) {
		dev_err(dev, "no ac/usb irq specified\n");
		ret = -ENODEV;
		goto noirqs;
	}

	if (pdata->supplied_to) {
		pda_power_supplies[0].supplied_to = pdata->supplied_to;
		pda_power_supplies[1].supplied_to = pdata->supplied_to;
		pda_power_supplies[0].num_supplicants = pdata->num_supplicants;
		pda_power_supplies[1].num_supplicants = pdata->num_supplicants;
	}

	ret = power_supply_register(&pdev->dev, &pda_power_supplies[0]);
	if (ret) {
		dev_err(dev, "failed to register %s power supply\n",
			pda_power_supplies[0].name);
		goto supply0_failed;
	}

	ret = power_supply_register(&pdev->dev, &pda_power_supplies[1]);
	if (ret) {
		dev_err(dev, "failed to register %s power supply\n",
			pda_power_supplies[1].name);
		goto supply1_failed;
	}

	if (ac_irq) {
		ret = request_irq(ac_irq->start, power_changed_isr,
				  get_irq_flags(ac_irq), ac_irq->name,
				  &pda_power_supplies[0]);
		if (ret) {
			dev_err(dev, "request ac irq failed\n");
			goto ac_irq_failed;
		}
	}

	if (usb_irq) {
		ret = request_irq(usb_irq->start, power_changed_isr,
				  get_irq_flags(usb_irq), usb_irq->name,
				  &pda_power_supplies[1]);
		if (ret) {
			dev_err(dev, "request usb irq failed\n");
			goto usb_irq_failed;
		}
	}

	goto success;

usb_irq_failed:
	if (ac_irq)
		free_irq(ac_irq->start, &pda_power_supplies[0]);
ac_irq_failed:
	power_supply_unregister(&pda_power_supplies[1]);
supply1_failed:
	power_supply_unregister(&pda_power_supplies[0]);
supply0_failed:
noirqs:
wrongid:
success:
	return ret;
}

static int pda_power_remove(struct platform_device *pdev)
{
	if (usb_irq)
		free_irq(usb_irq->start, &pda_power_supplies[1]);
	if (ac_irq)
		free_irq(ac_irq->start, &pda_power_supplies[0]);
	del_timer_sync(&charger_timer);
	del_timer_sync(&supply_timer);
	power_supply_unregister(&pda_power_supplies[1]);
	power_supply_unregister(&pda_power_supplies[0]);
	return 0;
}

static struct platform_driver pda_power_pdrv = {
	.driver = {
		.name = "pda-power",
	},
	.probe = pda_power_probe,
	.remove = pda_power_remove,
};

static int __init pda_power_init(void)
{
	return platform_driver_register(&pda_power_pdrv);
}

static void __exit pda_power_exit(void)
{
	platform_driver_unregister(&pda_power_pdrv);
}

module_init(pda_power_init);
module_exit(pda_power_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anton Vorontsov <cbou@mail.ru>");
