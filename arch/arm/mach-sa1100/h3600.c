/*
 * Support for Compaq iPAQ H3600 handheld computer
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
static void h3600_lcd_power(int enable)
{
	if (gpio_request(H3XXX_EGPIO_LCD_ON, "LCD power")) {
		pr_err("%s: can't request H3XXX_EGPIO_LCD_ON\n", __func__);
		goto err1;
	}
	if (gpio_request(H3600_EGPIO_LCD_PCI, "LCD control")) {
		pr_err("%s: can't request H3XXX_EGPIO_LCD_PCI\n", __func__);
		goto err2;
	}
	if (gpio_request(H3600_EGPIO_LCD_5V_ON, "LCD 5v")) {
		pr_err("%s: can't request H3XXX_EGPIO_LCD_5V_ON\n", __func__);
		goto err3;
	}
	if (gpio_request(H3600_EGPIO_LVDD_ON, "LCD 9v/-6.5v")) {
		pr_err("%s: can't request H3600_EGPIO_LVDD_ON\n", __func__);
		goto err4;
	}

	gpio_direction_output(H3XXX_EGPIO_LCD_ON, enable);
	gpio_direction_output(H3600_EGPIO_LCD_PCI, enable);
	gpio_direction_output(H3600_EGPIO_LCD_5V_ON, enable);
	gpio_direction_output(H3600_EGPIO_LVDD_ON, enable);

	gpio_free(H3600_EGPIO_LVDD_ON);
err4:	gpio_free(H3600_EGPIO_LCD_5V_ON);
err3:	gpio_free(H3600_EGPIO_LCD_PCI);
err2:	gpio_free(H3XXX_EGPIO_LCD_ON);
err1:	return;
}

static const struct sa1100fb_rgb h3600_rgb_16 = {
	.red	= { .offset = 12, .length = 4, },
	.green	= { .offset = 7,  .length = 4, },
	.blue	= { .offset = 1,  .length = 4, },
	.transp	= { .offset = 0,  .length = 0, },
};

static struct sa1100fb_mach_info h3600_lcd_info = {
	.pixclock	= 174757, 	.bpp		= 16,
	.xres		= 320,		.yres		= 240,

	.hsync_len	= 3,		.vsync_len	= 3,
	.left_margin	= 12,		.upper_margin	= 10,
	.right_margin	= 17,		.lower_margin	= 1,

	.cmap_static	= 1,

	.lccr0		= LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixRsEdg | LCCR3_ACBsDiv(2),

	.rgb[RGB_16] = &h3600_rgb_16,

	.lcd_power = h3600_lcd_power,
};


static void __init h3600_map_io(void)
{
	h3xxx_map_io();
}

/*
 * This turns the IRDA power on or off on the Compaq H3600
 */
static int h3600_irda_set_power(struct device *dev, unsigned int state)
{
	gpio_set_value(H3600_EGPIO_IR_ON, state);
	return 0;
}

static void h3600_irda_set_speed(struct device *dev, unsigned int speed)
{
	gpio_set_value(H3600_EGPIO_IR_FSEL, !(speed < 4000000));
}

static int h3600_irda_startup(struct device *dev)
{
	int err = gpio_request(H3600_EGPIO_IR_ON, "IrDA power");
	if (err)
		goto err1;
	err = gpio_direction_output(H3600_EGPIO_IR_ON, 0);
	if (err)
		goto err2;
	err = gpio_request(H3600_EGPIO_IR_FSEL, "IrDA fsel");
	if (err)
		goto err2;
	err = gpio_direction_output(H3600_EGPIO_IR_FSEL, 0);
	if (err)
		goto err3;
	return 0;

err3:	gpio_free(H3600_EGPIO_IR_FSEL);
err2:	gpio_free(H3600_EGPIO_IR_ON);
err1:	return err;
}

static void h3600_irda_shutdown(struct device *dev)
{
	gpio_free(H3600_EGPIO_IR_ON);
	gpio_free(H3600_EGPIO_IR_FSEL);
}

static struct irda_platform_data h3600_irda_data = {
	.set_power	= h3600_irda_set_power,
	.set_speed	= h3600_irda_set_speed,
	.startup	= h3600_irda_startup,
	.shutdown	= h3600_irda_shutdown,
};

static struct gpio_default_state h3600_default_gpio[] = {
	{ H3XXX_GPIO_COM_DCD,	GPIO_MODE_IN,	"COM DCD" },
	{ H3XXX_GPIO_COM_CTS,	GPIO_MODE_IN,	"COM CTS" },
	{ H3XXX_GPIO_COM_RTS,	GPIO_MODE_OUT0,	"COM RTS" },
};

static void __init h3600_mach_init(void)
{
	h3xxx_init_gpio(h3600_default_gpio, ARRAY_SIZE(h3600_default_gpio));
	h3xxx_mach_init();

	sa11x0_register_lcd(&h3600_lcd_info);
	sa11x0_register_irda(&h3600_irda_data);
}

MACHINE_START(H3600, "Compaq iPAQ H3600")
	.atag_offset	= 0x100,
	.map_io		= h3600_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= h3600_mach_init,
	.restart	= sa11x0_restart,
MACHINE_END

