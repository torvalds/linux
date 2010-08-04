/*
 * linux/arch/arm/mach-msm/gpio.c
 *
 * Copyright (C) 2005 HP Labs
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2009 Pavel Machek <pavel@ucw.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include "board-trout.h"

struct msm_gpio_chip {
	struct gpio_chip	chip;
	void __iomem		*reg;	/* Base of register bank */
	u8			shadow;
};

#define to_msm_gpio_chip(c) container_of(c, struct msm_gpio_chip, chip)

static int msm_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	struct msm_gpio_chip *msm_gpio = to_msm_gpio_chip(chip);
	unsigned mask = 1 << offset;

	return !!(readb(msm_gpio->reg) & mask);
}

static void msm_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct msm_gpio_chip *msm_gpio = to_msm_gpio_chip(chip);
	unsigned mask = 1 << offset;

	if (val)
		msm_gpio->shadow |= mask;
	else
		msm_gpio->shadow &= ~mask;

	writeb(msm_gpio->shadow, msm_gpio->reg);
}

static int msm_gpiolib_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	msm_gpiolib_set(chip, offset, 0);
	return 0;
}

static int msm_gpiolib_direction_output(struct gpio_chip *chip,
					 unsigned offset, int val)
{
	msm_gpiolib_set(chip, offset, val);
	return 0;
}

#define TROUT_GPIO_BANK(name, reg_num, base_gpio, shadow_val)		\
	{								\
		.chip = {						\
			.label		  = name,			\
			.direction_input  = msm_gpiolib_direction_input,\
			.direction_output = msm_gpiolib_direction_output, \
			.get		  = msm_gpiolib_get,		\
			.set		  = msm_gpiolib_set,		\
			.base		  = base_gpio,			\
			.ngpio		  = 8,				\
		},							\
		.reg = (void *) reg_num + TROUT_CPLD_BASE,		\
		.shadow = shadow_val,					\
	}

static struct msm_gpio_chip msm_gpio_banks[] = {
#if defined(CONFIG_MSM_DEBUG_UART1)
	/* H2W pins <-> UART1 */
	TROUT_GPIO_BANK("MISC2", 0x00,   TROUT_GPIO_MISC2_BASE, 0x40),
#else
	/* H2W pins <-> UART3, Bluetooth <-> UART1 */
	TROUT_GPIO_BANK("MISC2", 0x00,   TROUT_GPIO_MISC2_BASE, 0x80),
#endif
	/* I2C pull */
	TROUT_GPIO_BANK("MISC3", 0x02,   TROUT_GPIO_MISC3_BASE, 0x04),
	TROUT_GPIO_BANK("MISC4", 0x04,   TROUT_GPIO_MISC4_BASE, 0),
	/* mmdi 32k en */
	TROUT_GPIO_BANK("MISC5", 0x06,   TROUT_GPIO_MISC5_BASE, 0x04),
	TROUT_GPIO_BANK("INT2", 0x08,    TROUT_GPIO_INT2_BASE,  0),
	TROUT_GPIO_BANK("MISC1", 0x0a,   TROUT_GPIO_MISC1_BASE, 0),
	TROUT_GPIO_BANK("VIRTUAL", 0x12, TROUT_GPIO_VIRTUAL_BASE, 0),
};

/*
 * Called from the processor-specific init to enable GPIO pin support.
 */
int __init trout_init_gpio(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msm_gpio_banks); i++)
		gpiochip_add(&msm_gpio_banks[i].chip);

	return 0;
}

postcore_initcall(trout_init_gpio);

