/*
 *  ezx.c - Common code for the EZX platform.
 *
 *  Copyright (C) 2005-2006 Harald Welte <laforge@openezx.org>,
 *		  2007-2008 Daniel Ribeiro <drwyrm@gmail.com>,
 *		  2007-2008 Stefan Schmidt <stefan@datenfreihafen.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/pwm_backlight.h>

#include <asm/setup.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/i2c.h>

#include <mach/mfp-pxa27x.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "devices.h"
#include "generic.h"

static struct platform_pwm_backlight_data ezx_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 1023,
	.dft_brightness	= 1023,
	.pwm_period_ns	= 78770,
};

static struct platform_device ezx_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent	= &pxa27x_device_pwm0.dev,
		.platform_data = &ezx_backlight_data,
	},
};

static struct pxafb_mode_info mode_ezx_old = {
	.pixclock		= 150000,
	.xres			= 240,
	.yres			= 320,
	.bpp			= 16,
	.hsync_len		= 10,
	.left_margin		= 20,
	.right_margin		= 10,
	.vsync_len		= 2,
	.upper_margin		= 3,
	.lower_margin		= 2,
	.sync			= 0,
};

static struct pxafb_mach_info ezx_fb_info_1 = {
	.modes		= &mode_ezx_old,
	.num_modes	= 1,
	.lcd_conn	= LCD_COLOR_TFT_16BPP,
};

static struct pxafb_mode_info mode_72r89803y01 = {
	.pixclock		= 192308,
	.xres			= 240,
	.yres			= 320,
	.bpp			= 32,
	.depth			= 18,
	.hsync_len		= 10,
	.left_margin		= 20,
	.right_margin		= 10,
	.vsync_len		= 2,
	.upper_margin		= 3,
	.lower_margin		= 2,
	.sync			= 0,
};

static struct pxafb_mach_info ezx_fb_info_2 = {
	.modes		= &mode_72r89803y01,
	.num_modes	= 1,
	.lcd_conn	= LCD_COLOR_TFT_18BPP,
};

static struct platform_device *devices[] __initdata = {
	&ezx_backlight_device,
};

static unsigned long ezx_pin_config[] __initdata = {
	/* PWM backlight */
	GPIO16_PWM0_OUT,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* For A780 support (connected with Neptune GSM chip) */
	GPIO30_USB_P3_2,	/* ICL_TXENB */
	GPIO31_USB_P3_6,	/* ICL_VPOUT */
	GPIO90_USB_P3_5,	/* ICL_VPIN */
	GPIO91_USB_P3_1,	/* ICL_XRXD */
	GPIO56_USB_P3_4,	/* ICL_VMOUT */
	GPIO113_USB_P3_3,	/* /ICL_VMIN */
};

static void __init ezx_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa_set_i2c_info(NULL);
	if (machine_is_ezx_a780() || machine_is_ezx_e680())
		set_pxa_fb_info(&ezx_fb_info_1);
	else
		set_pxa_fb_info(&ezx_fb_info_2);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init ezx_fixup(struct machine_desc *desc, struct tag *tags,
		char **cmdline, struct meminfo *mi)
{
	/* We have two ram chips. First one with 32MB at 0xA0000000 and a second
	 * 16MB one at 0xAC000000
	 */
	mi->nr_banks = 2;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	mi->bank[0].size = (32*1024*1024);
	mi->bank[1].start = 0xac000000;
	mi->bank[1].node = 1;
	mi->bank[1].size = (16*1024*1024);
}

#ifdef CONFIG_MACH_EZX_A780
MACHINE_START(EZX_A780, "Motorola EZX A780")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup			= ezx_fixup,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = &ezx_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_E680
MACHINE_START(EZX_E680, "Motorola EZX E680")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup			= ezx_fixup,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = &ezx_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_A1200
MACHINE_START(EZX_A1200, "Motorola EZX A1200")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup			= ezx_fixup,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = &ezx_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_A910
MACHINE_START(EZX_A910, "Motorola EZX A910")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup			= ezx_fixup,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = &ezx_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_E6
MACHINE_START(EZX_E6, "Motorola EZX E6")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup			= ezx_fixup,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = &ezx_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_E2
MACHINE_START(EZX_E2, "Motorola EZX E2")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup			= ezx_fixup,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = &ezx_init,
MACHINE_END
#endif
