/*
 * LED Triggers for USB Activity
 *
 * Copyright 2014 Michal Sojka <sojka@merica.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/usb.h>

#define BLINK_DELAY 30

static unsigned long usb_blink_delay = BLINK_DELAY;

DEFINE_LED_TRIGGER(ledtrig_usb_gadget);
DEFINE_LED_TRIGGER(ledtrig_usb_host);

void usb_led_activity(enum usb_led_event ev)
{
	struct led_trigger *trig = NULL;

	switch (ev) {
	case USB_LED_EVENT_GADGET:
		trig = ledtrig_usb_gadget;
		break;
	case USB_LED_EVENT_HOST:
		trig = ledtrig_usb_host;
		break;
	}
	/* led_trigger_blink_oneshot() handles trig == NULL gracefully */
	led_trigger_blink_oneshot(trig, &usb_blink_delay, &usb_blink_delay, 0);
}
EXPORT_SYMBOL_GPL(usb_led_activity);


static int __init ledtrig_usb_init(void)
{
	led_trigger_register_simple("usb-gadget", &ledtrig_usb_gadget);
	led_trigger_register_simple("usb-host", &ledtrig_usb_host);
	return 0;
}

static void __exit ledtrig_usb_exit(void)
{
	led_trigger_unregister_simple(ledtrig_usb_gadget);
	led_trigger_unregister_simple(ledtrig_usb_host);
}

module_init(ledtrig_usb_init);
module_exit(ledtrig_usb_exit);
