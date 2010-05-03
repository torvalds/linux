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
#include <linux/mfd/tc6393xb.h>

#include <video/w100fb.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/pxa25x.h>
#include <mach/eseries-gpio.h>
#include <mach/udc.h>
#include <mach/irda.h>
#include <mach/irqs.h>
#include <mach/audio.h>

#include "generic.h"
#include "eseries.h"
#include "clock.h"

/* ---------------------- E750 LCD definitions -------------------- */

static struct w100_gen_regs e750_lcd_regs = {
	.lcd_format =            0x00008003,
	.lcdd_cntl1 =            0x00000000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x00fff003,
	.genlcd_cntl2 =          0x003c0f03,
	.genlcd_cntl3 =          0x000143aa,
};

static struct w100_mode e750_lcd_mode = {
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 21,
	.right_margin    = 22,
	.upper_margin    = 5,
	.lower_margin    = 4,
	.crtc_ss         = 0x80150014,
	.crtc_ls         = 0x8014000d,
	.crtc_gs         = 0xc1000005,
	.crtc_vpos_gs    = 0x00020147,
	.crtc_rev        = 0x0040010a,
	.crtc_dclk       = 0xa1700030,
	.crtc_gclk       = 0x80cc0015,
	.crtc_goe        = 0x80cc0015,
	.crtc_ps1_active = 0x61060017,
	.pll_freq        = 57,
	.pixclk_divider         = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_XTAL,
	.sysclk_divider  = 1,
	.sysclk_src     = CLK_SRC_PLL,
};

static struct w100_gpio_regs e750_w100_gpio_info = {
	.init_data1 = 0x01192f1b,
	.gpio_dir1  = 0xd5ffdeff,
	.gpio_oe1   = 0x000020bf,
	.init_data2 = 0x010f010f,
	.gpio_dir2  = 0xffffffff,
	.gpio_oe2   = 0x000001cf,
};

static struct w100fb_mach_info e750_fb_info = {
	.modelist   = &e750_lcd_mode,
	.num_modes  = 1,
	.regs       = &e750_lcd_regs,
	.gpio       = &e750_w100_gpio_info,
	.xtal_freq  = 14318000,
	.xtal_dbl   = 1,
};

static struct resource e750_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device e750_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e750_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e750_fb_resources),
	.resource       = e750_fb_resources,
};

/* -------------------- e750 MFP parameters -------------------- */

static unsigned long e750_pin_config[] __initdata = {
	/* Chip selects */
	GPIO15_nCS_1,   /* CS1 - Flash */
	GPIO79_nCS_3,   /* CS3 - IMAGEON */
	GPIO80_nCS_4,   /* CS4 - TMIO */

	/* Clocks */
	GPIO11_3_6MHz,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,

	/* TMIO controller */
	GPIO19_GPIO, /* t7l66xb #PCLR */
	GPIO45_GPIO, /* t7l66xb #SUSPEND (NOT BTUART!) */

	/* UDC */
	GPIO13_GPIO,
	GPIO3_GPIO,

	/* IrDA */
	GPIO38_GPIO | MFP_LPM_DRIVE_HIGH,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* Audio power control */
	GPIO4_GPIO,  /* Headphone amp power */
	GPIO7_GPIO,  /* Speaker amp power */
	GPIO37_GPIO, /* Headphone detect */

	/* PC Card */
	GPIO8_GPIO,   /* CD0 */
	GPIO44_GPIO,  /* CD1 */
	GPIO11_GPIO,  /* IRQ0 */
	GPIO6_GPIO,   /* IRQ1 */
	GPIO27_GPIO,  /* RST0 */
	GPIO24_GPIO,  /* RST1 */
	GPIO20_GPIO,  /* PWR0 */
	GPIO23_GPIO,  /* PWR1 */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* wakeup */
	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,
};

/* ----------------- e750 tc6393xb parameters ------------------ */

static struct tc6393xb_platform_data e750_tc6393xb_info = {
	.irq_base       = IRQ_BOARD_START,
	.scr_pll2cr     = 0x0cc1,
	.scr_gper       = 0,
	.gpio_base      = -1,
	.suspend        = &eseries_tmio_suspend,
	.resume         = &eseries_tmio_resume,
	.enable         = &eseries_tmio_enable,
	.disable        = &eseries_tmio_disable,
};

static struct platform_device e750_tc6393xb_device = {
	.name           = "tc6393xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e750_tc6393xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

/* ------------------------------------------------------------- */

static struct platform_device *devices[] __initdata = {
	&e750_fb_device,
	&e750_tc6393xb_device,
};

static void __init e750_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e750_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	clk_add_alias("CLK_CK3P6MI", e750_tc6393xb_device.name,
			"GPIO11_CLK", NULL),
	eseries_get_tmio_gpios();
	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_udc_info(&e7xx_udc_mach_info);
	pxa_set_ac97_info(NULL);
	pxa_set_ficp_info(&e7xx_ficp_platform_data);
}

MACHINE_START(E750, "Toshiba e750")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e750_init,
	.timer		= &pxa_timer,
MACHINE_END

