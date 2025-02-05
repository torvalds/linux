// SPDX-License-Identifier: GPL-2.0-only
/*
 *  LEDs triggers for power supply class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include "power_supply.h"

/* Battery specific LEDs triggers. */

struct power_supply_led_trigger {
	struct led_trigger trig;
	struct power_supply *psy;
};

#define trigger_to_psy_trigger(trigger) \
	container_of(trigger, struct power_supply_led_trigger, trig)

static int power_supply_led_trigger_activate(struct led_classdev *led_cdev)
{
	struct power_supply_led_trigger *psy_trig =
		trigger_to_psy_trigger(led_cdev->trigger);

	/* Sync current power-supply state to LED being activated */
	power_supply_update_leds(psy_trig->psy);
	return 0;
}

static int power_supply_register_led_trigger(struct power_supply *psy,
					     const char *name_template,
					     struct led_trigger **tp, int *err)
{
	struct power_supply_led_trigger *psy_trig;
	int ret = -ENOMEM;

	/* Bail on previous errors */
	if (err && *err)
		return *err;

	psy_trig = kzalloc(sizeof(*psy_trig), GFP_KERNEL);
	if (!psy_trig)
		goto err_free_trigger;

	psy_trig->trig.name = kasprintf(GFP_KERNEL, name_template, psy->desc->name);
	if (!psy_trig->trig.name)
		goto err_free_trigger;

	psy_trig->trig.activate = power_supply_led_trigger_activate;
	psy_trig->psy = psy;

	ret = led_trigger_register(&psy_trig->trig);
	if (ret)
		goto err_free_name;

	*tp = &psy_trig->trig;
	return 0;

err_free_name:
	kfree(psy_trig->trig.name);
err_free_trigger:
	kfree(psy_trig);
	if (err)
		*err = ret;

	return ret;
}

static void power_supply_unregister_led_trigger(struct led_trigger *trig)
{
	struct power_supply_led_trigger *psy_trig;

	if (!trig)
		return;

	psy_trig = trigger_to_psy_trigger(trig);
	led_trigger_unregister(&psy_trig->trig);
	kfree(psy_trig->trig.name);
	kfree(psy_trig);
}

static void power_supply_update_bat_leds(struct power_supply *psy)
{
	union power_supply_propval status;
	unsigned int intensity_green[3] = { 0, 255, 0 };
	unsigned int intensity_orange[3] = { 255, 128, 0 };

	if (power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS, &status))
		return;

	dev_dbg(&psy->dev, "%s %d\n", __func__, status.intval);

	switch (status.intval) {
	case POWER_SUPPLY_STATUS_FULL:
		led_trigger_event(psy->trig, LED_FULL);
		led_trigger_event(psy->charging_trig, LED_OFF);
		led_trigger_event(psy->full_trig, LED_FULL);
		/* Going from blink to LED on requires a LED_OFF event to stop blink */
		led_trigger_event(psy->charging_blink_full_solid_trig, LED_OFF);
		led_trigger_event(psy->charging_blink_full_solid_trig, LED_FULL);
		led_mc_trigger_event(psy->charging_orange_full_green_trig,
				     intensity_green,
				     ARRAY_SIZE(intensity_green),
				     LED_FULL);
		break;
	case POWER_SUPPLY_STATUS_CHARGING:
		led_trigger_event(psy->trig, LED_FULL);
		led_trigger_event(psy->charging_trig, LED_FULL);
		led_trigger_event(psy->full_trig, LED_OFF);
		led_trigger_blink(psy->charging_blink_full_solid_trig, 0, 0);
		led_mc_trigger_event(psy->charging_orange_full_green_trig,
				     intensity_orange,
				     ARRAY_SIZE(intensity_orange),
				     LED_FULL);
		break;
	default:
		led_trigger_event(psy->trig, LED_OFF);
		led_trigger_event(psy->charging_trig, LED_OFF);
		led_trigger_event(psy->full_trig, LED_OFF);
		led_trigger_event(psy->charging_blink_full_solid_trig,
			LED_OFF);
		led_trigger_event(psy->charging_orange_full_green_trig,
			LED_OFF);
		break;
	}
}

static void power_supply_remove_bat_triggers(struct power_supply *psy)
{
	power_supply_unregister_led_trigger(psy->trig);
	power_supply_unregister_led_trigger(psy->charging_trig);
	power_supply_unregister_led_trigger(psy->full_trig);
	power_supply_unregister_led_trigger(psy->charging_blink_full_solid_trig);
	power_supply_unregister_led_trigger(psy->charging_orange_full_green_trig);
}

static int power_supply_create_bat_triggers(struct power_supply *psy)
{
	int err = 0;

	power_supply_register_led_trigger(psy, "%s-charging-or-full",
					  &psy->trig, &err);
	power_supply_register_led_trigger(psy, "%s-charging",
					  &psy->charging_trig, &err);
	power_supply_register_led_trigger(psy, "%s-full",
					  &psy->full_trig, &err);
	power_supply_register_led_trigger(psy, "%s-charging-blink-full-solid",
					  &psy->charging_blink_full_solid_trig, &err);
	power_supply_register_led_trigger(psy, "%s-charging-orange-full-green",
					  &psy->charging_orange_full_green_trig, &err);
	if (err)
		power_supply_remove_bat_triggers(psy);

	return err;
}

/* Generated power specific LEDs triggers. */

static void power_supply_update_gen_leds(struct power_supply *psy)
{
	union power_supply_propval online;

	if (power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &online))
		return;

	dev_dbg(&psy->dev, "%s %d\n", __func__, online.intval);

	if (online.intval)
		led_trigger_event(psy->trig, LED_FULL);
	else
		led_trigger_event(psy->trig, LED_OFF);
}

static int power_supply_create_gen_triggers(struct power_supply *psy)
{
	return power_supply_register_led_trigger(psy, "%s-online", &psy->trig, NULL);
}

static void power_supply_remove_gen_triggers(struct power_supply *psy)
{
	power_supply_unregister_led_trigger(psy->trig);
}

/* Choice what triggers to create&update. */

void power_supply_update_leds(struct power_supply *psy)
{
	if (psy->desc->type == POWER_SUPPLY_TYPE_BATTERY)
		power_supply_update_bat_leds(psy);
	else
		power_supply_update_gen_leds(psy);
}

int power_supply_create_triggers(struct power_supply *psy)
{
	if (psy->desc->type == POWER_SUPPLY_TYPE_BATTERY)
		return power_supply_create_bat_triggers(psy);
	return power_supply_create_gen_triggers(psy);
}

void power_supply_remove_triggers(struct power_supply *psy)
{
	if (psy->desc->type == POWER_SUPPLY_TYPE_BATTERY)
		power_supply_remove_bat_triggers(psy);
	else
		power_supply_remove_gen_triggers(psy);
}
