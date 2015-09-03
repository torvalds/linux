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
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/power_supply.h>
#include <linux/pda_power.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/usb/otg.h>

static inline unsigned int get_irq_flags(struct resource *res)
{
	return IRQF_SHARED | (res->flags & IRQF_TRIGGER_MASK);
}

static struct device *dev;
static struct pda_power_pdata *pdata;
static struct resource *ac_irq, *usb_irq;
static struct timer_list charger_timer;
static struct timer_list supply_timer;
static struct timer_list polling_timer;
static int polling;
static struct power_supply *pda_psy_ac, *pda_psy_usb;

#if IS_ENABLED(CONFIG_USB_PHY)
static struct usb_phy *transceiver;
static struct notifier_block otg_nb;
#endif

static struct regulator *ac_draw;

enum {
	PDA_PSY_OFFLINE = 0,
	PDA_PSY_ONLINE = 1,
	PDA_PSY_TO_CHANGE,
};
static int new_ac_status = -1;
static int new_usb_status = -1;
static int ac_status = -1;
static int usb_status = -1;

static int pda_power_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->desc->type == POWER_SUPPLY_TYPE_MAINS)
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

static const struct power_supply_desc pda_psy_ac_desc = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = pda_power_props,
	.num_properties = ARRAY_SIZE(pda_power_props),
	.get_property = pda_power_get_property,
};

static const struct power_supply_desc pda_psy_usb_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = pda_power_props,
	.num_properties = ARRAY_SIZE(pda_power_props),
	.get_property = pda_power_get_property,
};

static void update_status(void)
{
	if (pdata->is_ac_online)
		new_ac_status = !!pdata->is_ac_online();

	if (pdata->is_usb_online)
		new_usb_status = !!pdata->is_usb_online();
}

static void update_charger(void)
{
	static int regulator_enabled;
	int max_uA = pdata->ac_max_uA;

	if (pdata->set_charge) {
		if (new_ac_status > 0) {
			dev_dbg(dev, "charger on (AC)\n");
			pdata->set_charge(PDA_POWER_CHARGE_AC);
		} else if (new_usb_status > 0) {
			dev_dbg(dev, "charger on (USB)\n");
			pdata->set_charge(PDA_POWER_CHARGE_USB);
		} else {
			dev_dbg(dev, "charger off\n");
			pdata->set_charge(0);
		}
	} else if (ac_draw) {
		if (new_ac_status > 0) {
			regulator_set_current_limit(ac_draw, max_uA, max_uA);
			if (!regulator_enabled) {
				dev_dbg(dev, "charger on (AC)\n");
				WARN_ON(regulator_enable(ac_draw));
				regulator_enabled = 1;
			}
		} else {
			if (regulator_enabled) {
				dev_dbg(dev, "charger off\n");
				WARN_ON(regulator_disable(ac_draw));
				regulator_enabled = 0;
			}
		}
	}
}

static void supply_timer_func(unsigned long unused)
{
	if (ac_status == PDA_PSY_TO_CHANGE) {
		ac_status = new_ac_status;
		power_supply_changed(pda_psy_ac);
	}

	if (usb_status == PDA_PSY_TO_CHANGE) {
		usb_status = new_usb_status;
		power_supply_changed(pda_psy_usb);
	}
}

static void psy_changed(void)
{
	update_charger();

	/*
	 * Okay, charger set. Now wait a bit before notifying supplicants,
	 * charge power should stabilize.
	 */
	mod_timer(&supply_timer,
		  jiffies + msecs_to_jiffies(pdata->wait_for_charger));
}

static void charger_timer_func(unsigned long unused)
{
	update_status();
	psy_changed();
}

static irqreturn_t power_changed_isr(int irq, void *power_supply)
{
	if (power_supply == pda_psy_ac)
		ac_status = PDA_PSY_TO_CHANGE;
	else if (power_supply == pda_psy_usb)
		usb_status = PDA_PSY_TO_CHANGE;
	else
		return IRQ_NONE;

	/*
	 * Wait a bit before reading ac/usb line status and setting charger,
	 * because ac/usb status readings may lag from irq.
	 */
	mod_timer(&charger_timer,
		  jiffies + msecs_to_jiffies(pdata->wait_for_status));

	return IRQ_HANDLED;
}

static void polling_timer_func(unsigned long unused)
{
	int changed = 0;

	dev_dbg(dev, "polling...\n");

	update_status();

	if (!ac_irq && new_ac_status != ac_status) {
		ac_status = PDA_PSY_TO_CHANGE;
		changed = 1;
	}

	if (!usb_irq && new_usb_status != usb_status) {
		usb_status = PDA_PSY_TO_CHANGE;
		changed = 1;
	}

	if (changed)
		psy_changed();

	mod_timer(&polling_timer,
		  jiffies + msecs_to_jiffies(pdata->polling_interval));
}

#if IS_ENABLED(CONFIG_USB_PHY)
static int otg_is_usb_online(void)
{
	return (transceiver->last_event == USB_EVENT_VBUS ||
		transceiver->last_event == USB_EVENT_ENUMERATED);
}

static int otg_is_ac_online(void)
{
	return (transceiver->last_event == USB_EVENT_CHARGER);
}

static int otg_handle_notification(struct notifier_block *nb,
		unsigned long event, void *unused)
{
	switch (event) {
	case USB_EVENT_CHARGER:
		ac_status = PDA_PSY_TO_CHANGE;
		break;
	case USB_EVENT_VBUS:
	case USB_EVENT_ENUMERATED:
		usb_status = PDA_PSY_TO_CHANGE;
		break;
	case USB_EVENT_NONE:
		ac_status = PDA_PSY_TO_CHANGE;
		usb_status = PDA_PSY_TO_CHANGE;
		break;
	default:
		return NOTIFY_OK;
	}

	/*
	 * Wait a bit before reading ac/usb line status and setting charger,
	 * because ac/usb status readings may lag from irq.
	 */
	mod_timer(&charger_timer,
		  jiffies + msecs_to_jiffies(pdata->wait_for_status));

	return NOTIFY_OK;
}
#endif

static int pda_power_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	int ret = 0;

	dev = &pdev->dev;

	if (pdev->id != -1) {
		dev_err(dev, "it's meaningless to register several "
			"pda_powers; use id = -1\n");
		ret = -EINVAL;
		goto wrongid;
	}

	pdata = pdev->dev.platform_data;

	if (pdata->init) {
		ret = pdata->init(dev);
		if (ret < 0)
			goto init_failed;
	}

	ac_draw = regulator_get(dev, "ac_draw");
	if (IS_ERR(ac_draw)) {
		dev_dbg(dev, "couldn't get ac_draw regulator\n");
		ac_draw = NULL;
	}

	update_status();
	update_charger();

	if (!pdata->wait_for_status)
		pdata->wait_for_status = 500;

	if (!pdata->wait_for_charger)
		pdata->wait_for_charger = 500;

	if (!pdata->polling_interval)
		pdata->polling_interval = 2000;

	if (!pdata->ac_max_uA)
		pdata->ac_max_uA = 500000;

	setup_timer(&charger_timer, charger_timer_func, 0);
	setup_timer(&supply_timer, supply_timer_func, 0);

	ac_irq = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "ac");
	usb_irq = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "usb");

	if (pdata->supplied_to) {
		psy_cfg.supplied_to = pdata->supplied_to;
		psy_cfg.num_supplicants = pdata->num_supplicants;
	} else {
		psy_cfg.supplied_to = pda_power_supplied_to;
		psy_cfg.num_supplicants = ARRAY_SIZE(pda_power_supplied_to);
	}

#if IS_ENABLED(CONFIG_USB_PHY)
	transceiver = usb_get_phy(USB_PHY_TYPE_USB2);
	if (!IS_ERR_OR_NULL(transceiver)) {
		if (!pdata->is_usb_online)
			pdata->is_usb_online = otg_is_usb_online;
		if (!pdata->is_ac_online)
			pdata->is_ac_online = otg_is_ac_online;
	}
#endif

	if (pdata->is_ac_online) {
		pda_psy_ac = power_supply_register(&pdev->dev,
						   &pda_psy_ac_desc, &psy_cfg);
		if (IS_ERR(pda_psy_ac)) {
			dev_err(dev, "failed to register %s power supply\n",
				pda_psy_ac_desc.name);
			ret = PTR_ERR(pda_psy_ac);
			goto ac_supply_failed;
		}

		if (ac_irq) {
			ret = request_irq(ac_irq->start, power_changed_isr,
					  get_irq_flags(ac_irq), ac_irq->name,
					  pda_psy_ac);
			if (ret) {
				dev_err(dev, "request ac irq failed\n");
				goto ac_irq_failed;
			}
		} else {
			polling = 1;
		}
	}

	if (pdata->is_usb_online) {
		pda_psy_usb = power_supply_register(&pdev->dev,
						    &pda_psy_usb_desc,
						    &psy_cfg);
		if (IS_ERR(pda_psy_usb)) {
			dev_err(dev, "failed to register %s power supply\n",
				pda_psy_usb_desc.name);
			ret = PTR_ERR(pda_psy_usb);
			goto usb_supply_failed;
		}

		if (usb_irq) {
			ret = request_irq(usb_irq->start, power_changed_isr,
					  get_irq_flags(usb_irq),
					  usb_irq->name, pda_psy_usb);
			if (ret) {
				dev_err(dev, "request usb irq failed\n");
				goto usb_irq_failed;
			}
		} else {
			polling = 1;
		}
	}

#if IS_ENABLED(CONFIG_USB_PHY)
	if (!IS_ERR_OR_NULL(transceiver) && pdata->use_otg_notifier) {
		otg_nb.notifier_call = otg_handle_notification;
		ret = usb_register_notifier(transceiver, &otg_nb);
		if (ret) {
			dev_err(dev, "failure to register otg notifier\n");
			goto otg_reg_notifier_failed;
		}
		polling = 0;
	}
#endif

	if (polling) {
		dev_dbg(dev, "will poll for status\n");
		setup_timer(&polling_timer, polling_timer_func, 0);
		mod_timer(&polling_timer,
			  jiffies + msecs_to_jiffies(pdata->polling_interval));
	}

	if (ac_irq || usb_irq)
		device_init_wakeup(&pdev->dev, 1);

	return 0;

#if IS_ENABLED(CONFIG_USB_PHY)
otg_reg_notifier_failed:
	if (pdata->is_usb_online && usb_irq)
		free_irq(usb_irq->start, pda_psy_usb);
#endif
usb_irq_failed:
	if (pdata->is_usb_online)
		power_supply_unregister(pda_psy_usb);
usb_supply_failed:
	if (pdata->is_ac_online && ac_irq)
		free_irq(ac_irq->start, pda_psy_ac);
#if IS_ENABLED(CONFIG_USB_PHY)
	if (!IS_ERR_OR_NULL(transceiver))
		usb_put_phy(transceiver);
#endif
ac_irq_failed:
	if (pdata->is_ac_online)
		power_supply_unregister(pda_psy_ac);
ac_supply_failed:
	if (ac_draw) {
		regulator_put(ac_draw);
		ac_draw = NULL;
	}
	if (pdata->exit)
		pdata->exit(dev);
init_failed:
wrongid:
	return ret;
}

static int pda_power_remove(struct platform_device *pdev)
{
	if (pdata->is_usb_online && usb_irq)
		free_irq(usb_irq->start, pda_psy_usb);
	if (pdata->is_ac_online && ac_irq)
		free_irq(ac_irq->start, pda_psy_ac);

	if (polling)
		del_timer_sync(&polling_timer);
	del_timer_sync(&charger_timer);
	del_timer_sync(&supply_timer);

	if (pdata->is_usb_online)
		power_supply_unregister(pda_psy_usb);
	if (pdata->is_ac_online)
		power_supply_unregister(pda_psy_ac);
#if IS_ENABLED(CONFIG_USB_PHY)
	if (!IS_ERR_OR_NULL(transceiver))
		usb_put_phy(transceiver);
#endif
	if (ac_draw) {
		regulator_put(ac_draw);
		ac_draw = NULL;
	}
	if (pdata->exit)
		pdata->exit(dev);

	return 0;
}

#ifdef CONFIG_PM
static int ac_wakeup_enabled;
static int usb_wakeup_enabled;

static int pda_power_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (pdata->suspend) {
		int ret = pdata->suspend(state);

		if (ret)
			return ret;
	}

	if (device_may_wakeup(&pdev->dev)) {
		if (ac_irq)
			ac_wakeup_enabled = !enable_irq_wake(ac_irq->start);
		if (usb_irq)
			usb_wakeup_enabled = !enable_irq_wake(usb_irq->start);
	}

	return 0;
}

static int pda_power_resume(struct platform_device *pdev)
{
	if (device_may_wakeup(&pdev->dev)) {
		if (usb_irq && usb_wakeup_enabled)
			disable_irq_wake(usb_irq->start);
		if (ac_irq && ac_wakeup_enabled)
			disable_irq_wake(ac_irq->start);
	}

	if (pdata->resume)
		return pdata->resume();

	return 0;
}
#else
#define pda_power_suspend NULL
#define pda_power_resume NULL
#endif /* CONFIG_PM */

static struct platform_driver pda_power_pdrv = {
	.driver = {
		.name = "pda-power",
	},
	.probe = pda_power_probe,
	.remove = pda_power_remove,
	.suspend = pda_power_suspend,
	.resume = pda_power_resume,
};

module_platform_driver(pda_power_pdrv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anton Vorontsov <cbou@mail.ru>");
MODULE_ALIAS("platform:pda-power");
