// SPDX-License-Identifier: GPL-2.0-only
/*
 * Camera Flash and Torch On/Off Trigger
 *
 * based on ledtrig-ide-disk.c
 *
 * Copyright 2013 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>

DEFINE_LED_TRIGGER(ledtrig_flash);
DEFINE_LED_TRIGGER(ledtrig_torch);

void ledtrig_flash_ctrl(bool on)
{
	enum led_brightness brt = on ? LED_FULL : LED_OFF;

	led_trigger_event(ledtrig_flash, brt);
}
EXPORT_SYMBOL_GPL(ledtrig_flash_ctrl);

void ledtrig_torch_ctrl(bool on)
{
	enum led_brightness brt = on ? LED_FULL : LED_OFF;

	led_trigger_event(ledtrig_torch, brt);
}
EXPORT_SYMBOL_GPL(ledtrig_torch_ctrl);

static int __init ledtrig_camera_init(void)
{
	led_trigger_register_simple("flash", &ledtrig_flash);
	led_trigger_register_simple("torch", &ledtrig_torch);
	return 0;
}
module_init(ledtrig_camera_init);

static void __exit ledtrig_camera_exit(void)
{
	led_trigger_unregister_simple(ledtrig_torch);
	led_trigger_unregister_simple(ledtrig_flash);
}
module_exit(ledtrig_camera_exit);

MODULE_DESCRIPTION("LED Trigger for Camera Flash/Torch Control");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL v2");
