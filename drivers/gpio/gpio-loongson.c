/*
 *  Loongson-2F/3A/3B GPIO Support
 *
 *  Copyright (c) 2008 Richard Liu,  STMicroelectronics	 <richard.liu@st.com>
 *  Copyright (c) 2008-2010 Arnaud Patard <apatard@mandriva.com>
 *  Copyright (c) 2013 Hongbing Hu <huhb@lemote.com>
 *  Copyright (c) 2014 Huacai Chen <chenhc@lemote.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <asm/types.h>
#include <loongson.h>
#include <linux/gpio.h>

#define STLS2F_N_GPIO		4
#define STLS3A_N_GPIO		16

#ifdef CONFIG_CPU_LOONGSON3
#define LOONGSON_N_GPIO	STLS3A_N_GPIO
#else
#define LOONGSON_N_GPIO	STLS2F_N_GPIO
#endif

#define LOONGSON_GPIO_IN_OFFSET	16

static DEFINE_SPINLOCK(gpio_lock);

static int loongson_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	u32 temp;
	u32 mask;

	spin_lock(&gpio_lock);
	mask = 1 << gpio;
	temp = LOONGSON_GPIOIE;
	temp |= mask;
	LOONGSON_GPIOIE = temp;
	spin_unlock(&gpio_lock);

	return 0;
}

static int loongson_gpio_direction_output(struct gpio_chip *chip,
		unsigned gpio, int level)
{
	u32 temp;
	u32 mask;

	gpio_set_value(gpio, level);
	spin_lock(&gpio_lock);
	mask = 1 << gpio;
	temp = LOONGSON_GPIOIE;
	temp &= (~mask);
	LOONGSON_GPIOIE = temp;
	spin_unlock(&gpio_lock);

	return 0;
}

static int loongson_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	u32 val;
	u32 mask;

	mask = 1 << (gpio + LOONGSON_GPIO_IN_OFFSET);
	spin_lock(&gpio_lock);
	val = LOONGSON_GPIODATA;
	spin_unlock(&gpio_lock);

	return (val & mask) != 0;
}

static void loongson_gpio_set_value(struct gpio_chip *chip,
		unsigned gpio, int value)
{
	u32 val;
	u32 mask;

	mask = 1 << gpio;

	spin_lock(&gpio_lock);
	val = LOONGSON_GPIODATA;
	if (value)
		val |= mask;
	else
		val &= (~mask);
	LOONGSON_GPIODATA = val;
	spin_unlock(&gpio_lock);
}

static struct gpio_chip loongson_chip = {
	.label                  = "Loongson-gpio-chip",
	.direction_input        = loongson_gpio_direction_input,
	.get                    = loongson_gpio_get_value,
	.direction_output       = loongson_gpio_direction_output,
	.set                    = loongson_gpio_set_value,
	.base			= 0,
	.ngpio                  = LOONGSON_N_GPIO,
	.can_sleep		= false,
};

static int __init loongson_gpio_setup(void)
{
	return gpiochip_add_data(&loongson_chip, NULL);
}
postcore_initcall(loongson_gpio_setup);
