/*
 * LED support code, ripped out of arch/arm/kernel/time.c
 *
 *  Copyright (C) 1994-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/export.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/syscore_ops.h>
#include <linux/string.h>

#include <asm/leds.h>

static void dummy_leds_event(led_event_t evt)
{
}

void (*leds_event)(led_event_t) = dummy_leds_event;

struct leds_evt_name {
	const char	name[8];
	int		on;
	int		off;
};

static const struct leds_evt_name evt_names[] = {
	{ "amber", led_amber_on, led_amber_off },
	{ "blue",  led_blue_on,  led_blue_off  },
	{ "green", led_green_on, led_green_off },
	{ "red",   led_red_on,   led_red_off   },
};

static ssize_t leds_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int ret = -EINVAL, len = strcspn(buf, " ");

	if (len > 0 && buf[len] == '\0')
		len--;

	if (strncmp(buf, "claim", len) == 0) {
		leds_event(led_claim);
		ret = size;
	} else if (strncmp(buf, "release", len) == 0) {
		leds_event(led_release);
		ret = size;
	} else {
		int i;

		for (i = 0; i < ARRAY_SIZE(evt_names); i++) {
			if (strlen(evt_names[i].name) != len ||
			    strncmp(buf, evt_names[i].name, len) != 0)
				continue;
			if (strncmp(buf+len, " on", 3) == 0) {
				leds_event(evt_names[i].on);
				ret = size;
			} else if (strncmp(buf+len, " off", 4) == 0) {
				leds_event(evt_names[i].off);
				ret = size;
			}
			break;
		}
	}
	return ret;
}

static DEVICE_ATTR(event, 0200, NULL, leds_store);

static struct bus_type leds_subsys = {
	.name		= "leds",
	.dev_name	= "leds",
};

static struct device leds_device = {
	.id		= 0,
	.bus		= &leds_subsys,
};

static int leds_suspend(void)
{
	leds_event(led_stop);
	return 0;
}

static void leds_resume(void)
{
	leds_event(led_start);
}

static void leds_shutdown(void)
{
	leds_event(led_halted);
}

static struct syscore_ops leds_syscore_ops = {
	.shutdown	= leds_shutdown,
	.suspend	= leds_suspend,
	.resume		= leds_resume,
};

static int leds_idle_notifier(struct notifier_block *nb, unsigned long val,
                                void *data)
{
	switch (val) {
	case IDLE_START:
		leds_event(led_idle_start);
		break;
	case IDLE_END:
		leds_event(led_idle_end);
		break;
	}

	return 0;
}

static struct notifier_block leds_idle_nb = {
	.notifier_call = leds_idle_notifier,
};

static int __init leds_init(void)
{
	int ret;
	ret = subsys_system_register(&leds_subsys, NULL);
	if (ret == 0)
		ret = device_register(&leds_device);
	if (ret == 0)
		ret = device_create_file(&leds_device, &dev_attr_event);
	if (ret == 0) {
		register_syscore_ops(&leds_syscore_ops);
		idle_notifier_register(&leds_idle_nb);
	}

	return ret;
}

device_initcall(leds_init);

EXPORT_SYMBOL(leds_event);
