/*
 *  LEDs triggers for power supply class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 *
 *  You may use this code as per GPL version 2
 */

#include <linux/power_supply.h>

/* Battery specific LEDs triggers. */

static void power_supply_update_bat_leds(struct power_supply *psy)
{
	union power_supply_propval status;

	if (psy->get_property(psy, POWER_SUPPLY_PROP_STATUS, &status))
		return;

	dev_dbg(psy->dev, "%s %d\n", __FUNCTION__, status.intval);

	switch (status.intval) {
	case POWER_SUPPLY_STATUS_FULL:
		led_trigger_event(psy->charging_full_trig, LED_FULL);
		led_trigger_event(psy->charging_trig, LED_OFF);
		led_trigger_event(psy->full_trig, LED_FULL);
		break;
	case POWER_SUPPLY_STATUS_CHARGING:
		led_trigger_event(psy->charging_full_trig, LED_FULL);
		led_trigger_event(psy->charging_trig, LED_FULL);
		led_trigger_event(psy->full_trig, LED_OFF);
		break;
	default:
		led_trigger_event(psy->charging_full_trig, LED_OFF);
		led_trigger_event(psy->charging_trig, LED_OFF);
		led_trigger_event(psy->full_trig, LED_OFF);
		break;
	}

	return;
}

static int power_supply_create_bat_triggers(struct power_supply *psy)
{
	int rc = 0;

	psy->charging_full_trig_name = kmalloc(strlen(psy->name) +
				  sizeof("-charging-or-full"), GFP_KERNEL);
	if (!psy->charging_full_trig_name)
		goto charging_full_failed;

	psy->charging_trig_name = kmalloc(strlen(psy->name) +
					  sizeof("-charging"), GFP_KERNEL);
	if (!psy->charging_trig_name)
		goto charging_failed;

	psy->full_trig_name = kmalloc(strlen(psy->name) +
				      sizeof("-full"), GFP_KERNEL);
	if (!psy->full_trig_name)
		goto full_failed;

	strcpy(psy->charging_full_trig_name, psy->name);
	strcat(psy->charging_full_trig_name, "-charging-or-full");
	strcpy(psy->charging_trig_name, psy->name);
	strcat(psy->charging_trig_name, "-charging");
	strcpy(psy->full_trig_name, psy->name);
	strcat(psy->full_trig_name, "-full");

	led_trigger_register_simple(psy->charging_full_trig_name,
				    &psy->charging_full_trig);
	led_trigger_register_simple(psy->charging_trig_name,
				    &psy->charging_trig);
	led_trigger_register_simple(psy->full_trig_name,
				    &psy->full_trig);

	goto success;

full_failed:
	kfree(psy->charging_trig_name);
charging_failed:
	kfree(psy->charging_full_trig_name);
charging_full_failed:
	rc = -ENOMEM;
success:
	return rc;
}

static void power_supply_remove_bat_triggers(struct power_supply *psy)
{
	led_trigger_unregister_simple(psy->charging_full_trig);
	led_trigger_unregister_simple(psy->charging_trig);
	led_trigger_unregister_simple(psy->full_trig);
	kfree(psy->full_trig_name);
	kfree(psy->charging_trig_name);
	kfree(psy->charging_full_trig_name);
	return;
}

/* Generated power specific LEDs triggers. */

static void power_supply_update_gen_leds(struct power_supply *psy)
{
	union power_supply_propval online;

	if (psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &online))
		return;

	dev_dbg(psy->dev, "%s %d\n", __FUNCTION__, online.intval);

	if (online.intval)
		led_trigger_event(psy->online_trig, LED_FULL);
	else
		led_trigger_event(psy->online_trig, LED_OFF);

	return;
}

static int power_supply_create_gen_triggers(struct power_supply *psy)
{
	int rc = 0;

	psy->online_trig_name = kmalloc(strlen(psy->name) + sizeof("-online"),
					GFP_KERNEL);
	if (!psy->online_trig_name)
		goto online_failed;

	strcpy(psy->online_trig_name, psy->name);
	strcat(psy->online_trig_name, "-online");

	led_trigger_register_simple(psy->online_trig_name, &psy->online_trig);

	goto success;

online_failed:
	rc = -ENOMEM;
success:
	return rc;
}

static void power_supply_remove_gen_triggers(struct power_supply *psy)
{
	led_trigger_unregister_simple(psy->online_trig);
	kfree(psy->online_trig_name);
	return;
}

/* Choice what triggers to create&update. */

void power_supply_update_leds(struct power_supply *psy)
{
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY)
		power_supply_update_bat_leds(psy);
	else
		power_supply_update_gen_leds(psy);
	return;
}

int power_supply_create_triggers(struct power_supply *psy)
{
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY)
		return power_supply_create_bat_triggers(psy);
	return power_supply_create_gen_triggers(psy);
}

void power_supply_remove_triggers(struct power_supply *psy)
{
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY)
		power_supply_remove_bat_triggers(psy);
	else
		power_supply_remove_gen_triggers(psy);
	return;
}
