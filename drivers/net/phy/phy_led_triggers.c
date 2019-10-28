/* Copyright (C) 2016 National Instruments Corp.
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
#include <linux/leds.h>
#include <linux/phy.h>
#include <linux/phy_led_triggers.h>
#include <linux/netdevice.h>

static struct phy_led_trigger *phy_speed_to_led_trigger(struct phy_device *phy,
							unsigned int speed)
{
	unsigned int i;

	for (i = 0; i < phy->phy_num_led_triggers; i++) {
		if (phy->phy_led_triggers[i].speed == speed)
			return &phy->phy_led_triggers[i];
	}
	return NULL;
}

static void phy_led_trigger_no_link(struct phy_device *phy)
{
	if (phy->last_triggered) {
		led_trigger_event(&phy->last_triggered->trigger, LED_OFF);
		led_trigger_event(&phy->led_link_trigger->trigger, LED_OFF);
		phy->last_triggered = NULL;
	}
}

void phy_led_trigger_change_speed(struct phy_device *phy)
{
	struct phy_led_trigger *plt;

	if (!phy->link)
		return phy_led_trigger_no_link(phy);

	if (phy->speed == 0)
		return;

	plt = phy_speed_to_led_trigger(phy, phy->speed);
	if (!plt) {
		netdev_alert(phy->attached_dev,
			     "No phy led trigger registered for speed(%d)\n",
			     phy->speed);
		return phy_led_trigger_no_link(phy);
	}

	if (plt != phy->last_triggered) {
		if (!phy->last_triggered)
			led_trigger_event(&phy->led_link_trigger->trigger,
					  LED_FULL);
		else
			led_trigger_event(&phy->last_triggered->trigger, LED_OFF);

		led_trigger_event(&plt->trigger, LED_FULL);
		phy->last_triggered = plt;
	}
}
EXPORT_SYMBOL_GPL(phy_led_trigger_change_speed);

static void phy_led_trigger_format_name(struct phy_device *phy, char *buf,
					size_t size, char *suffix)
{
	snprintf(buf, size, PHY_ID_FMT ":%s",
		 phy->mdio.bus->id, phy->mdio.addr, suffix);
}

static int phy_led_trigger_register(struct phy_device *phy,
				    struct phy_led_trigger *plt,
				    unsigned int speed)
{
	char name_suffix[PHY_LED_TRIGGER_SPEED_SUFFIX_SIZE];

	plt->speed = speed;

	if (speed < SPEED_1000)
		snprintf(name_suffix, sizeof(name_suffix), "%dMbps", speed);
	else if (speed == SPEED_2500)
		snprintf(name_suffix, sizeof(name_suffix), "2.5Gbps");
	else
		snprintf(name_suffix, sizeof(name_suffix), "%dGbps",
			 DIV_ROUND_CLOSEST(speed, 1000));

	phy_led_trigger_format_name(phy, plt->name, sizeof(plt->name),
				    name_suffix);
	plt->trigger.name = plt->name;

	return led_trigger_register(&plt->trigger);
}

static void phy_led_trigger_unregister(struct phy_led_trigger *plt)
{
	led_trigger_unregister(&plt->trigger);
}

int phy_led_triggers_register(struct phy_device *phy)
{
	int i, err;
	unsigned int speeds[50];

	phy->phy_num_led_triggers = phy_supported_speeds(phy, speeds,
							 ARRAY_SIZE(speeds));
	if (!phy->phy_num_led_triggers)
		return 0;

	phy->led_link_trigger = devm_kzalloc(&phy->mdio.dev,
					     sizeof(*phy->led_link_trigger),
					     GFP_KERNEL);
	if (!phy->led_link_trigger) {
		err = -ENOMEM;
		goto out_clear;
	}

	phy_led_trigger_format_name(phy, phy->led_link_trigger->name,
				    sizeof(phy->led_link_trigger->name),
				    "link");
	phy->led_link_trigger->trigger.name = phy->led_link_trigger->name;

	err = led_trigger_register(&phy->led_link_trigger->trigger);
	if (err)
		goto out_free_link;

	phy->phy_led_triggers = devm_kcalloc(&phy->mdio.dev,
					    phy->phy_num_led_triggers,
					    sizeof(struct phy_led_trigger),
					    GFP_KERNEL);
	if (!phy->phy_led_triggers) {
		err = -ENOMEM;
		goto out_unreg_link;
	}

	for (i = 0; i < phy->phy_num_led_triggers; i++) {
		err = phy_led_trigger_register(phy, &phy->phy_led_triggers[i],
					       speeds[i]);
		if (err)
			goto out_unreg;
	}

	phy->last_triggered = NULL;
	phy_led_trigger_change_speed(phy);

	return 0;
out_unreg:
	while (i--)
		phy_led_trigger_unregister(&phy->phy_led_triggers[i]);
	devm_kfree(&phy->mdio.dev, phy->phy_led_triggers);
out_unreg_link:
	phy_led_trigger_unregister(phy->led_link_trigger);
out_free_link:
	devm_kfree(&phy->mdio.dev, phy->led_link_trigger);
	phy->led_link_trigger = NULL;
out_clear:
	phy->phy_num_led_triggers = 0;
	return err;
}
EXPORT_SYMBOL_GPL(phy_led_triggers_register);

void phy_led_triggers_unregister(struct phy_device *phy)
{
	int i;

	for (i = 0; i < phy->phy_num_led_triggers; i++)
		phy_led_trigger_unregister(&phy->phy_led_triggers[i]);

	if (phy->led_link_trigger)
		phy_led_trigger_unregister(phy->led_link_trigger);
}
EXPORT_SYMBOL_GPL(phy_led_triggers_unregister);
