// SPDX-License-Identifier: GPL-2.0
/*
 * LED Triggers for USB Activity
 *
 * Copyright 2014 Michal Sojka <sojka@merica.cz>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/usb.h>
#include "common.h"

#define BLINK_DELAY 30

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
	led_trigger_blink_oneshot(trig, BLINK_DELAY, BLINK_DELAY, 0);
}
EXPORT_SYMBOL_GPL(usb_led_activity);


void __init ledtrig_usb_init(void)
{
	led_trigger_register_simple("usb-gadget", &ledtrig_usb_gadget);
	led_trigger_register_simple("usb-host", &ledtrig_usb_host);
}

void __exit ledtrig_usb_exit(void)
{
	led_trigger_unregister_simple(ledtrig_usb_gadget);
	led_trigger_unregister_simple(ledtrig_usb_host);
}
