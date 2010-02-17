/*
 * Support for Compaq iPAQ H3100 handheld computer
 *
 * Copyright (c) 2000,1 Compaq Computer Corporation. (Author: Jamey Hicks)
 * Copyright (c) 2009 Dmitry Artamonow <mad_soft@inbox.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irda.h>

#include <mach/h3xxx.h>

#include "generic.h"

/*
 * helper for sa1100fb
 */
static void h3100_lcd_power(int enable)
{
	if (!gpio_request(H3XXX_EGPIO_LCD_ON, "LCD ON")) {
		gpio_set_value(H3100_GPIO_LCD_3V_ON, enable);
		gpio_direction_output(H3XXX_EGPIO_LCD_ON, enable);
		gpio_free(H3XXX_EGPIO_LCD_ON);
	} else {
		pr_err("%s: can't request H3XXX_EGPIO_LCD_ON\n", __func__);
	}
}


static void __init h3100_map_io(void)
{
	h3xxx_map_io();

	sa1100fb_lcd_power = h3100_lcd_power;

	/* Older bootldrs put GPIO2-9 in alternate mode on the
	   assumption that they are used for video */
	GAFR &= ~0x000001fb;
}

/*
 * This turns the IRDA power on or off on the Compaq H3100
 */
static int h3100_irda_set_power(struct device *dev, unsigned int state)
{
	gpio_set_value(H3100_GPIO_IR_ON, state);
	return 0;
}

static void h3100_irda_set_speed(struct device *dev, unsigned int speed)
{
	gpio_set_value(H3100_GPIO_IR_FSEL, !(speed < 4000000));
}

static struct irda_platform_data h3100_irda_data = {
	.set_power	= h3100_irda_set_power,
	.set_speed	= h3100_irda_set_speed,
};

static struct gpio_default_state h3100_default_gpio[] = {
	{ H3100_GPIO_IR_ON,	GPIO_MODE_OUT0, "IrDA power" },
	{ H3100_GPIO_IR_FSEL,	GPIO_MODE_OUT0, "IrDA fsel" },
	{ H3XXX_GPIO_COM_DCD,	GPIO_MODE_IN,	"COM DCD" },
	{ H3XXX_GPIO_COM_CTS,	GPIO_MODE_IN,	"COM CTS" },
	{ H3XXX_GPIO_COM_RTS,	GPIO_MODE_OUT0,	"COM RTS" },
	{ H3100_GPIO_LCD_3V_ON,	GPIO_MODE_OUT0,	"LCD 3v" },
};

static void __init h3100_mach_init(void)
{
	h3xxx_init_gpio(h3100_default_gpio, ARRAY_SIZE(h3100_default_gpio));
	h3xxx_mach_init();
	sa11x0_register_irda(&h3100_irda_data);
}

MACHINE_START(H3100, "Compaq iPAQ H3100")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= h3100_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= h3100_mach_init,
MACHINE_END

