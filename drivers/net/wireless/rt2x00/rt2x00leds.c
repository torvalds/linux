/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 led specific routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

void rt2x00leds_led_quality(struct rt2x00_dev *rt2x00dev, int rssi)
{
	if (!rt2x00dev->trigger_qual.registered)
		return;

	/*
	 * Led handling requires a positive value for the rssi,
	 * to do that correctly we need to add the correction.
	 */
	rssi += rt2x00dev->rssi_offset;

	/*
	 * Get the rssi level, this is used to convert the rssi
	 * to a LED value inside the range LED_OFF - LED_FULL.
	 */
	if (rssi <= 30)
		rssi = 0;
	else if (rssi <= 39)
		rssi = 1;
	else if (rssi <= 49)
		rssi = 2;
	else if (rssi <= 53)
		rssi = 3;
	else if (rssi <= 63)
		rssi = 4;
	else
		rssi = 5;

	/*
	 * Note that we must _not_ send LED_OFF since the driver
	 * is going to calculate the value and might use it in a
	 * division.
	 */
	led_trigger_event(&rt2x00dev->trigger_qual.trigger,
			  ((LED_FULL / 6) * rssi) + 1);
}

static int rt2x00leds_register_trigger(struct rt2x00_dev *rt2x00dev,
				       struct rt2x00_trigger *trigger,
				       const char *name)
{
	int retval;

	trigger->trigger.name = name;
	retval = led_trigger_register(&trigger->trigger);
	if (retval) {
		ERROR(rt2x00dev, "Failed to register led trigger.\n");
		return retval;
	}

	trigger->registered = 1;

	return 0;
}

static int rt2x00leds_register_led(struct rt2x00_dev *rt2x00dev,
				   struct rt2x00_led *led,
				   enum led_type type,
				   const char *name, char *trigger)
{
	struct device *device = wiphy_dev(rt2x00dev->hw->wiphy);
	int retval;

	led->led_dev.name = name;
	led->led_dev.brightness_set = rt2x00dev->ops->lib->led_brightness;
	led->led_dev.default_trigger = trigger;

	retval = led_classdev_register(device, &led->led_dev);
	if (retval) {
		ERROR(rt2x00dev, "Failed to register led handler.\n");
		return retval;
	}

	led->rt2x00dev = rt2x00dev;
	led->type = type;
	led->registered = 1;

	return 0;
}

void rt2x00leds_register(struct rt2x00_dev *rt2x00dev)
{
	char *trigger;
	char dev_name[16];
	char name[32];
	int retval;

	if (!rt2x00dev->ops->lib->led_brightness)
		return;

	snprintf(dev_name, sizeof(dev_name), "%s-%s",
		 rt2x00dev->ops->name, wiphy_name(rt2x00dev->hw->wiphy));

	if (rt2x00dev->led_flags & LED_SUPPORT_RADIO) {
		trigger = ieee80211_get_radio_led_name(rt2x00dev->hw);
		snprintf(name, sizeof(name), "%s:radio", dev_name);

		retval = rt2x00leds_register_led(rt2x00dev,
						 &rt2x00dev->led_radio,
						 LED_TYPE_RADIO,
						 name, trigger);
		if (retval)
			goto exit_fail;
	}

	if (rt2x00dev->led_flags & LED_SUPPORT_ASSOC) {
		trigger = ieee80211_get_assoc_led_name(rt2x00dev->hw);
		snprintf(name, sizeof(name), "%s:assoc", dev_name);

		retval = rt2x00leds_register_led(rt2x00dev,
						 &rt2x00dev->led_assoc,
						 LED_TYPE_ASSOC,
						 name, trigger);
		if (retval)
			goto exit_fail;
	}

	if (rt2x00dev->led_flags & LED_SUPPORT_QUALITY) {
		snprintf(name, sizeof(name), "%s:quality", dev_name);

		retval = rt2x00leds_register_trigger(rt2x00dev,
						     &rt2x00dev->trigger_qual,
						     name);

		retval = rt2x00leds_register_led(rt2x00dev,
						 &rt2x00dev->led_qual,
						 LED_TYPE_QUALITY,
						 name, name);
		if (retval)
			goto exit_fail;
	}

	return;

exit_fail:
	rt2x00leds_unregister(rt2x00dev);
}

static void rt2x00leds_unregister_trigger(struct rt2x00_trigger *trigger)
{
	if (!trigger->registered)
		return;

	led_trigger_unregister(&trigger->trigger);
	trigger->registered = 0;
}

static void rt2x00leds_unregister_led(struct rt2x00_led *led)
{
	if (!led->registered)
		return;

	led_classdev_unregister(&led->led_dev);

	led->led_dev.brightness_set(&led->led_dev, LED_OFF);
	led->registered = 0;
}

void rt2x00leds_unregister(struct rt2x00_dev *rt2x00dev)
{
	rt2x00leds_unregister_trigger(&rt2x00dev->trigger_qual);
	rt2x00leds_unregister_led(&rt2x00dev->led_qual);
	rt2x00leds_unregister_led(&rt2x00dev->led_assoc);
	rt2x00leds_unregister_led(&rt2x00dev->led_radio);
}

void rt2x00leds_suspend(struct rt2x00_dev *rt2x00dev)
{
	if (rt2x00dev->led_qual.registered)
		led_classdev_suspend(&rt2x00dev->led_qual.led_dev);
	if (rt2x00dev->led_assoc.registered)
		led_classdev_suspend(&rt2x00dev->led_assoc.led_dev);
	if (rt2x00dev->led_radio.registered)
		led_classdev_suspend(&rt2x00dev->led_radio.led_dev);
}

void rt2x00leds_resume(struct rt2x00_dev *rt2x00dev)
{
	if (rt2x00dev->led_radio.registered)
		led_classdev_resume(&rt2x00dev->led_radio.led_dev);
	if (rt2x00dev->led_assoc.registered)
		led_classdev_resume(&rt2x00dev->led_assoc.led_dev);
	if (rt2x00dev->led_qual.registered)
		led_classdev_resume(&rt2x00dev->led_qual.led_dev);
}
