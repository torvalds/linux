/*
 * linux/arch/unicore32/kernel/gpio.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
/* in FPGA, no GPIO support */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
/* FIXME: needed for gpio_set_value() - convert to use descriptors or hogs */
#include <linux/gpio.h>
#include <mach/hardware.h>

#ifdef CONFIG_LEDS
#include <linux/leds.h>
#include <linux/platform_device.h>

static const struct gpio_led puv3_gpio_leds[] = {
	{ .name = "cpuhealth", .gpio = GPO_CPU_HEALTH, .active_low = 0,
		.default_trigger = "heartbeat",	},
	{ .name = "hdd_led", .gpio = GPO_HDD_LED, .active_low = 1,
		.default_trigger = "disk-activity", },
};

static const struct gpio_led_platform_data puv3_gpio_led_data = {
	.num_leds =	ARRAY_SIZE(puv3_gpio_leds),
	.leds =		(void *) puv3_gpio_leds,
};

static struct platform_device puv3_gpio_gpio_leds = {
	.name =		"leds-gpio",
	.id =		-1,
	.dev = {
		.platform_data = (void *) &puv3_gpio_led_data,
	}
};

static int __init puv3_gpio_leds_init(void)
{
	platform_device_register(&puv3_gpio_gpio_leds);
	return 0;
}

device_initcall(puv3_gpio_leds_init);
#endif

static int puv3_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return !!(readl(GPIO_GPLR) & GPIO_GPIO(offset));
}

static void puv3_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value)
		writel(GPIO_GPIO(offset), GPIO_GPSR);
	else
		writel(GPIO_GPIO(offset), GPIO_GPCR);
}

static int puv3_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long flags;

	local_irq_save(flags);
	writel(readl(GPIO_GPDR) & ~GPIO_GPIO(offset), GPIO_GPDR);
	local_irq_restore(flags);
	return 0;
}

static int puv3_direction_output(struct gpio_chip *chip, unsigned offset,
		int value)
{
	unsigned long flags;

	local_irq_save(flags);
	puv3_gpio_set(chip, offset, value);
	writel(readl(GPIO_GPDR) | GPIO_GPIO(offset), GPIO_GPDR);
	local_irq_restore(flags);
	return 0;
}

static struct gpio_chip puv3_gpio_chip = {
	.label			= "gpio",
	.direction_input	= puv3_direction_input,
	.direction_output	= puv3_direction_output,
	.set			= puv3_gpio_set,
	.get			= puv3_gpio_get,
	.base			= 0,
	.ngpio			= GPIO_MAX + 1,
};

void __init puv3_init_gpio(void)
{
	writel(GPIO_DIR, GPIO_GPDR);
#if	defined(CONFIG_PUV3_NB0916) || defined(CONFIG_PUV3_SMW0919)	\
	|| defined(CONFIG_PUV3_DB0913)
	gpio_set_value(GPO_WIFI_EN, 1);
	gpio_set_value(GPO_HDD_LED, 1);
	gpio_set_value(GPO_VGA_EN, 1);
	gpio_set_value(GPO_LCD_EN, 1);
	gpio_set_value(GPO_CAM_PWR_EN, 0);
	gpio_set_value(GPO_LCD_VCC_EN, 1);
	gpio_set_value(GPO_SOFT_OFF, 1);
	gpio_set_value(GPO_BT_EN, 1);
	gpio_set_value(GPO_FAN_ON, 0);
	gpio_set_value(GPO_SPKR, 0);
	gpio_set_value(GPO_CPU_HEALTH, 1);
	gpio_set_value(GPO_LAN_SEL, 1);
/*
 * DO NOT modify the GPO_SET_V1 and GPO_SET_V2 in kernel
 *	gpio_set_value(GPO_SET_V1, 1);
 *	gpio_set_value(GPO_SET_V2, 1);
 */
#endif
	gpiochip_add_data(&puv3_gpio_chip, NULL);
}
