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
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fb.h>

#include <video/w100fb.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/mfp-pxa25x.h>
#include <mach/hardware.h>
#include <mach/eseries-gpio.h>
#include <mach/udc.h>

#include "generic.h"
#include "eseries.h"

/* ------------------------ e800 LCD definitions ------------------------- */

static struct w100_gen_regs e800_lcd_regs = {
	.lcd_format =            0x00008003,
	.lcdd_cntl1 =            0x02a00000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x000ff2a3,
	.genlcd_cntl2 =          0x000002a3,
	.genlcd_cntl3 =          0x000102aa,
};

static struct w100_mode e800_lcd_mode[2] = {
	[0] = {
		.xres            = 480,
		.yres            = 640,
		.left_margin     = 52,
		.right_margin    = 148,
		.upper_margin    = 2,
		.lower_margin    = 6,
		.crtc_ss         = 0x80350034,
		.crtc_ls         = 0x802b0026,
		.crtc_gs         = 0x80160016,
		.crtc_vpos_gs    = 0x00020003,
		.crtc_rev        = 0x0040001d,
		.crtc_dclk       = 0xe0000000,
		.crtc_gclk       = 0x82a50049,
		.crtc_goe        = 0x80ee001c,
		.crtc_ps1_active = 0x00000000,
		.pll_freq        = 128,
		.pixclk_divider         = 4,
		.pixclk_divider_rotated = 6,
		.pixclk_src     = CLK_SRC_PLL,
		.sysclk_divider  = 0,
		.sysclk_src     = CLK_SRC_PLL,
	},
	[1] = {
		.xres            = 240,
		.yres            = 320,
		.left_margin     = 15,
		.right_margin    = 88,
		.upper_margin    = 0,
		.lower_margin    = 7,
		.crtc_ss         = 0xd010000f,
		.crtc_ls         = 0x80070003,
		.crtc_gs         = 0x80000000,
		.crtc_vpos_gs    = 0x01460147,
		.crtc_rev        = 0x00400003,
		.crtc_dclk       = 0xa1700030,
		.crtc_gclk       = 0x814b0008,
		.crtc_goe        = 0x80cc0015,
		.crtc_ps1_active = 0x00000000,
		.pll_freq        = 100,
		.pixclk_divider         = 6, /* Wince uses 14 which gives a */
		.pixclk_divider_rotated = 6, /* 7MHz Pclk. We use a 14MHz one */
		.pixclk_src     = CLK_SRC_PLL,
		.sysclk_divider  = 0,
		.sysclk_src     = CLK_SRC_PLL,
	}
};


static struct w100_gpio_regs e800_w100_gpio_info = {
	.init_data1 = 0xc13fc019,
	.gpio_dir1  = 0x3e40df7f,
	.gpio_oe1   = 0x003c3000,
	.init_data2 = 0x00000000,
	.gpio_dir2  = 0x00000000,
	.gpio_oe2   = 0x00000000,
};

static struct w100_mem_info e800_w100_mem_info = {
	.ext_cntl        = 0x09640011,
	.sdram_mode_reg  = 0x00600021,
	.ext_timing_cntl = 0x10001545,
	.io_cntl         = 0x7ddd7333,
	.size            = 0x1fffff,
};

static void e800_tg_change(struct w100fb_par *par)
{
	unsigned long tmp;

	tmp = w100fb_gpio_read(W100_GPIO_PORT_A);
	if (par->mode->xres == 480)
		tmp |= 0x100;
	else
		tmp &= ~0x100;
	w100fb_gpio_write(W100_GPIO_PORT_A, tmp);
}

static struct w100_tg_info e800_tg_info = {
	.change = e800_tg_change,
};

static struct w100fb_mach_info e800_fb_info = {
	.modelist   = e800_lcd_mode,
	.num_modes  = 2,
	.regs       = &e800_lcd_regs,
	.gpio       = &e800_w100_gpio_info,
	.mem        = &e800_w100_mem_info,
	.tg         = &e800_tg_info,
	.xtal_freq  = 16000000,
};

static struct resource e800_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device e800_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e800_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e800_fb_resources),
	.resource       = e800_fb_resources,
};

/* --------------------------- UDC definitions --------------------------- */

static struct pxa2xx_udc_mach_info e800_udc_mach_info = {
	.gpio_vbus   = GPIO_E800_USB_DISC,
	.gpio_pullup = GPIO_E800_USB_PULLUP,
	.gpio_pullup_inverted = 1
};

/* ----------------------------------------------------------------------- */

static struct platform_device *devices[] __initdata = {
	&e800_fb_device,
};

static void __init e800_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_udc_info(&e800_udc_mach_info);
}

MACHINE_START(E800, "Toshiba e800")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e800_init,
	.timer		= &pxa_timer,
MACHINE_END

