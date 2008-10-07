/*
 * Hardware definitions for the Toshiba eseries PDAs
 *
 * Copyright (c) 2003 Ian Molton <spyro@f2s.com>
 *
 * This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/pxa-regs.h>
#include <mach/mfp-pxa25x.h>
#include <mach/hardware.h>

#include <mach/pxafb.h>
#include <mach/udc.h>

#include "generic.h"
#include "eseries.h"

/* ------------------------ E400 LCD definitions ------------------------ */

static struct pxafb_mode_info e400_pxafb_mode_info = {
	.pixclock       = 140703,
	.xres           = 240,
	.yres           = 320,
	.bpp            = 16,
	.hsync_len      = 4,
	.left_margin    = 28,
	.right_margin   = 8,
	.vsync_len      = 3,
	.upper_margin   = 5,
	.lower_margin   = 6,
	.sync           = 0,
};

static struct pxafb_mach_info e400_pxafb_mach_info = {
	.modes          = &e400_pxafb_mode_info,
	.num_modes      = 1,
	.lccr0          = LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3          = 0,
	.pxafb_backlight_power  = NULL,
};

/* ------------------------ E400 MFP config ----------------------------- */

static unsigned long e400_pin_config[] __initdata = {
	/* Chip selects */
	GPIO15_nCS_1,   /* CS1 - Flash */
	GPIO80_nCS_4,   /* CS4 - TMIO */

	/* Clocks */
	GPIO12_32KHz,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_GPIO, /* Used by TMIO for #SUSPEND */

	/* wakeup */
	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,
};

/* ---------------------------------------------------------------------- */

static void __init e400_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e400_pin_config));
	set_pxa_fb_info(&e400_pxafb_mach_info);
	pxa_set_udc_info(&e7xx_udc_mach_info);
}

MACHINE_START(E400, "Toshiba e400")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e400_init,
	.timer		= &pxa_timer,
MACHINE_END

