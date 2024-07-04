// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Compaq iPAQ H3600 handheld computer
 *
 * Copyright (c) 2000,1 Compaq Computer Corporation. (Author: Jamey Hicks)
 * Copyright (c) 2009 Dmitry Artamonow <mad_soft@inbox.ru>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio.h>

#include <video/sa1100fb.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/h3xxx.h>
#include <mach/irqs.h>

#include "generic.h"

static bool h3600_lcd_request(void)
{
	static bool h3600_lcd_ok;
	int rc;

	if (h3600_lcd_ok)
		return true;

	rc = gpio_request(H3XXX_EGPIO_LCD_ON, "LCD power");
	if (rc)
		goto out;
	rc = gpio_direction_output(H3XXX_EGPIO_LCD_ON, 0);
	if (rc)
		goto out_free_on;
	rc = gpio_request(H3600_EGPIO_LCD_PCI, "LCD control");
	if (rc)
		goto out_free_on;
	rc = gpio_direction_output(H3600_EGPIO_LCD_PCI, 0);
	if (rc)
		goto out_free_pci;
	rc = gpio_request(H3600_EGPIO_LCD_5V_ON, "LCD 5v");
	if (rc)
		goto out_free_pci;
	rc = gpio_direction_output(H3600_EGPIO_LCD_5V_ON, 0);
	if (rc)
		goto out_free_5v_on;
	rc = gpio_request(H3600_EGPIO_LVDD_ON, "LCD 9v/-6.5v");
	if (rc)
		goto out_free_5v_on;
	rc = gpio_direction_output(H3600_EGPIO_LVDD_ON, 0);
	if (rc)
		goto out_free_lvdd_on;

	goto out;

out_free_lvdd_on:
	gpio_free(H3600_EGPIO_LVDD_ON);
out_free_5v_on:
	gpio_free(H3600_EGPIO_LCD_5V_ON);
out_free_pci:
	gpio_free(H3600_EGPIO_LCD_PCI);
out_free_on:
	gpio_free(H3XXX_EGPIO_LCD_ON);
out:
	if (rc)
		pr_err("%s: can't request GPIOs\n", __func__);
	else
		h3600_lcd_ok = true;

	return h3600_lcd_ok;
}

static void h3600_lcd_power(int enable)
{
	if (!h3600_lcd_request())
		return;

	gpio_direction_output(H3XXX_EGPIO_LCD_ON, enable);
	gpio_direction_output(H3600_EGPIO_LCD_PCI, enable);
	gpio_direction_output(H3600_EGPIO_LCD_5V_ON, enable);
	gpio_direction_output(H3600_EGPIO_LVDD_ON, enable);
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

static void __init h3600_mach_init(void)
{
	h3xxx_mach_init();

	sa11x0_register_lcd(&h3600_lcd_info);
}

MACHINE_START(H3600, "Compaq iPAQ H3600")
	.atag_offset	= 0x100,
	.map_io		= h3600_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.init_time	= sa1100_timer_init,
	.init_machine	= h3600_mach_init,
	.init_late	= sa11x0_init_late,
	.restart	= sa11x0_restart,
MACHINE_END

