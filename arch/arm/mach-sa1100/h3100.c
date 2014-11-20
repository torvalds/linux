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

#include <video/sa1100fb.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irda.h>

#include <mach/h3xxx.h>
#include <mach/irqs.h>

#include "generic.h"

/*
 * helper for sa1100fb
 */
static struct gpio h3100_lcd_gpio[] = {
	{ H3100_GPIO_LCD_3V_ON, GPIOF_OUT_INIT_LOW, "LCD 3V" },
	{ H3XXX_EGPIO_LCD_ON, GPIOF_OUT_INIT_LOW, "LCD ON" },
};

static bool h3100_lcd_request(void)
{
	static bool h3100_lcd_ok;
	int rc;

	if (h3100_lcd_ok)
		return true;

	rc = gpio_request_array(h3100_lcd_gpio, ARRAY_SIZE(h3100_lcd_gpio));
	if (rc)
		pr_err("%s: can't request GPIOs\n", __func__);
	else
		h3100_lcd_ok = true;

	return h3100_lcd_ok;
}

static void h3100_lcd_power(int enable)
{
	if (!h3100_lcd_request())
		return;

	gpio_set_value(H3100_GPIO_LCD_3V_ON, enable);
	gpio_set_value(H3XXX_EGPIO_LCD_ON, enable);
}

static struct sa1100fb_mach_info h3100_lcd_info = {
	.pixclock	= 406977, 	.bpp		= 4,
	.xres		= 320,		.yres		= 240,

	.hsync_len	= 26,		.vsync_len	= 41,
	.left_margin	= 4,		.upper_margin	= 0,
	.right_margin	= 4,		.lower_margin	= 0,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.cmap_greyscale	= 1,
	.cmap_inverse	= 1,

	.lccr0		= LCCR0_Mono | LCCR0_4PixMono | LCCR0_Sngl | LCCR0_Pas,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixRsEdg | LCCR3_ACBsDiv(2),

	.lcd_power = h3100_lcd_power,
};

static void __init h3100_map_io(void)
{
	h3xxx_map_io();

	/* Older bootldrs put GPIO2-9 in alternate mode on the
	   assumption that they are used for video */
	GAFR &= ~0x000001fb;
}

/*
 * This turns the IRDA power on or off on the Compaq H3100
 */
static struct gpio h3100_irda_gpio[] = {
	{ H3100_GPIO_IR_ON,	GPIOF_OUT_INIT_LOW, "IrDA power" },
	{ H3100_GPIO_IR_FSEL,	GPIOF_OUT_INIT_LOW, "IrDA fsel" },
};

static int h3100_irda_set_power(struct device *dev, unsigned int state)
{
	gpio_set_value(H3100_GPIO_IR_ON, state);
	return 0;
}

static void h3100_irda_set_speed(struct device *dev, unsigned int speed)
{
	gpio_set_value(H3100_GPIO_IR_FSEL, !(speed < 4000000));
}

static int h3100_irda_startup(struct device *dev)
{
	return gpio_request_array(h3100_irda_gpio, sizeof(h3100_irda_gpio));
}

static void h3100_irda_shutdown(struct device *dev)
{
	return gpio_free_array(h3100_irda_gpio, sizeof(h3100_irda_gpio));
}

static struct irda_platform_data h3100_irda_data = {
	.set_power	= h3100_irda_set_power,
	.set_speed	= h3100_irda_set_speed,
	.startup	= h3100_irda_startup,
	.shutdown	= h3100_irda_shutdown,
};

static void __init h3100_mach_init(void)
{
	h3xxx_mach_init();

	sa11x0_register_lcd(&h3100_lcd_info);
	sa11x0_register_irda(&h3100_irda_data);
}

MACHINE_START(H3100, "Compaq iPAQ H3100")
	.atag_offset	= 0x100,
	.map_io		= h3100_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.init_time	= sa1100_timer_init,
	.init_machine	= h3100_mach_init,
	.init_late	= sa11x0_init_late,
	.restart	= sa11x0_restart,
MACHINE_END

