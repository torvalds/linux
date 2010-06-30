/*
 * Soekris board support code
 *
 * Copyright (C) 2008-2009 Tower Technologies
 * Written by Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/geode.h>

static struct gpio_led net5501_leds[] = {
	{
		.name = "error",
		.gpio = 6,
		.default_trigger = "default-on",
	},
};

static struct gpio_led_platform_data net5501_leds_data = {
	.num_leds = ARRAY_SIZE(net5501_leds),
	.leds = net5501_leds,
};

static struct platform_device net5501_leds_dev = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &net5501_leds_data,
};

static void __init init_net5501(void)
{
	platform_device_register(&net5501_leds_dev);
}

struct soekris_board {
	u16	offset;
	char	*sig;
	u8	len;
	void	(*init)(void);
};

static struct soekris_board __initdata boards[] = {
	{ 0xb7b, "net5501", 7, init_net5501 },	/* net5501 v1.33/1.33c */
	{ 0xb1f, "net5501", 7, init_net5501 },	/* net5501 v1.32i */
};

static int __init soekris_init(void)
{
	int i;
	unsigned char *rombase, *bios;

	if (!is_geode())
		return 0;

	rombase = ioremap(0xffff0000, 0xffff);
	if (!rombase) {
		printk(KERN_INFO "Soekris net5501 LED driver failed to get rombase");
		return 0;
	}

	bios = rombase + 0x20;	/* null terminated */

	if (strncmp(bios, "comBIOS", 7))
		goto unmap;

	for (i = 0; i < ARRAY_SIZE(boards); i++) {
		unsigned char *model = rombase + boards[i].offset;

		if (strncmp(model, boards[i].sig, boards[i].len) == 0) {
			printk(KERN_INFO "Soekris %s: %s\n", model, bios);

			if (boards[i].init)
				boards[i].init();
			break;
		}
	}

unmap:
	iounmap(rombase);
	return 0;
}

arch_initcall(soekris_init);
