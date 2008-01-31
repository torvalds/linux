/*
 *  linux/arch/arm/mach-pxa/littleton.c
 *
 *  Support for the Marvell Littleton Development Platform.
 *
 *  Author:	Jason Chagas (largely modified code)
 *  Created:	Nov 20, 2006
 *  Copyright:	(C) Copyright 2006 Marvell International Ltd.
 *
 *  2007-11-22  modified to align with latest kernel
 *              eric miao <eric.miao@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/mfp-pxa300.h>
#include <asm/arch/gpio.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/ssp.h>
#include <asm/arch/littleton.h>

#include "generic.h"

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

/* Littleton MFP configurations */
static mfp_cfg_t littleton_mfp_cfg[] __initdata = {
	/* LCD */
	GPIO54_LCD_LDD_0,
	GPIO55_LCD_LDD_1,
	GPIO56_LCD_LDD_2,
	GPIO57_LCD_LDD_3,
	GPIO58_LCD_LDD_4,
	GPIO59_LCD_LDD_5,
	GPIO60_LCD_LDD_6,
	GPIO61_LCD_LDD_7,
	GPIO62_LCD_LDD_8,
	GPIO63_LCD_LDD_9,
	GPIO64_LCD_LDD_10,
	GPIO65_LCD_LDD_11,
	GPIO66_LCD_LDD_12,
	GPIO67_LCD_LDD_13,
	GPIO68_LCD_LDD_14,
	GPIO69_LCD_LDD_15,
	GPIO70_LCD_LDD_16,
	GPIO71_LCD_LDD_17,
	GPIO72_LCD_FCLK,
	GPIO73_LCD_LCLK,
	GPIO74_LCD_PCLK,
	GPIO75_LCD_BIAS,

	/* SSP2 */
	GPIO25_SSP2_SCLK,
	GPIO17_SSP2_FRM,
	GPIO27_SSP2_TXD,

	/* Debug Ethernet */
	GPIO90_GPIO,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (LITTLETON_ETH_PHYS + 0x300),
		.end	= (LITTLETON_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GPIO(mfp_to_gpio(MFP_PIN_GPIO90)),
		.end	= IRQ_GPIO(mfp_to_gpio(MFP_PIN_GPIO90)),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_FALLING,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULES)
/* use bit 30, 31 as the indicator of command parameter number */
#define CMD0(x)		((0x00000000) | ((x) << 9))
#define CMD1(x, x1)	((0x40000000) | ((x) << 9) | 0x100 | (x1))
#define CMD2(x, x1, x2)	((0x80000000) | ((x) << 18) | 0x20000 |\
			 ((x1) << 9) | 0x100 | (x2))

static uint32_t lcd_panel_reset[] = {
	CMD0(0x1), /* reset */
	CMD0(0x0), /* nop */
	CMD0(0x0), /* nop */
	CMD0(0x0), /* nop */
};

static uint32_t lcd_panel_on[] = {
	CMD0(0x29),		/* Display ON */
	CMD2(0xB8, 0xFF, 0xF9),	/* Output Control */
	CMD0(0x11),		/* Sleep out */
	CMD1(0xB0, 0x16),	/* Wake */
};

static uint32_t lcd_panel_off[] = {
	CMD0(0x28),		/* Display OFF */
	CMD2(0xB8, 0x80, 0x02),	/* Output Control */
	CMD0(0x10),		/* Sleep in */
	CMD1(0xB0, 0x00),	/* Deep stand by in */
};

static uint32_t lcd_vga_pass_through[] = {
	CMD1(0xB0, 0x16),
	CMD1(0xBC, 0x80),
	CMD1(0xE1, 0x00),
	CMD1(0x36, 0x50),
	CMD1(0x3B, 0x00),
};

static uint32_t lcd_qvga_pass_through[] = {
	CMD1(0xB0, 0x16),
	CMD1(0xBC, 0x81),
	CMD1(0xE1, 0x00),
	CMD1(0x36, 0x50),
	CMD1(0x3B, 0x22),
};

static uint32_t lcd_vga_transfer[] = {
	CMD1(0xcf, 0x02), 	/* Blanking period control (1) */
	CMD2(0xd0, 0x08, 0x04),	/* Blanking period control (2) */
	CMD1(0xd1, 0x01),	/* CKV timing control on/off */
	CMD2(0xd2, 0x14, 0x00),	/* CKV 1,2 timing control */
	CMD2(0xd3, 0x1a, 0x0f),	/* OEV timing control */
	CMD2(0xd4, 0x1f, 0xaf),	/* ASW timing control (1) */
	CMD1(0xd5, 0x14),	/* ASW timing control (2) */
	CMD0(0x21),		/* Invert for normally black display */
	CMD0(0x29),		/* Display on */
};

static uint32_t lcd_qvga_transfer[] = {
	CMD1(0xd6, 0x02),	/* Blanking period control (1) */
	CMD2(0xd7, 0x08, 0x04),	/* Blanking period control (2) */
	CMD1(0xd8, 0x01),	/* CKV timing control on/off */
	CMD2(0xd9, 0x00, 0x08),	/* CKV 1,2 timing control */
	CMD2(0xde, 0x05, 0x0a),	/* OEV timing control */
	CMD2(0xdf, 0x0a, 0x19),	/* ASW timing control (1) */
	CMD1(0xe0, 0x0a),	/* ASW timing control (2) */
	CMD0(0x21),		/* Invert for normally black display */
	CMD0(0x29),		/* Display on */
};

static uint32_t lcd_panel_config[] = {
	CMD2(0xb8, 0xff, 0xf9),	/* Output control */
	CMD0(0x11),		/* sleep out */
	CMD1(0xba, 0x01),	/* Display mode (1) */
	CMD1(0xbb, 0x00),	/* Display mode (2) */
	CMD1(0x3a, 0x60),	/* Display mode 18-bit RGB */
	CMD1(0xbf, 0x10),	/* Drive system change control */
	CMD1(0xb1, 0x56),	/* Booster operation setup */
	CMD1(0xb2, 0x33),	/* Booster mode setup */
	CMD1(0xb3, 0x11),	/* Booster frequency setup */
	CMD1(0xb4, 0x02),	/* Op amp/system clock */
	CMD1(0xb5, 0x35),	/* VCS voltage */
	CMD1(0xb6, 0x40),	/* VCOM voltage */
	CMD1(0xb7, 0x03),	/* External display signal */
	CMD1(0xbd, 0x00),	/* ASW slew rate */
	CMD1(0xbe, 0x00),	/* Dummy data for QuadData operation */
	CMD1(0xc0, 0x11),	/* Sleep out FR count (A) */
	CMD1(0xc1, 0x11),	/* Sleep out FR count (B) */
	CMD1(0xc2, 0x11),	/* Sleep out FR count (C) */
	CMD2(0xc3, 0x20, 0x40),	/* Sleep out FR count (D) */
	CMD2(0xc4, 0x60, 0xc0),	/* Sleep out FR count (E) */
	CMD2(0xc5, 0x10, 0x20),	/* Sleep out FR count (F) */
	CMD1(0xc6, 0xc0),	/* Sleep out FR count (G) */
	CMD2(0xc7, 0x33, 0x43),	/* Gamma 1 fine tuning (1) */
	CMD1(0xc8, 0x44),	/* Gamma 1 fine tuning (2) */
	CMD1(0xc9, 0x33),	/* Gamma 1 inclination adjustment */
	CMD1(0xca, 0x00),	/* Gamma 1 blue offset adjustment */
	CMD2(0xec, 0x01, 0xf0),	/* Horizontal clock cycles */
};

static void ssp_reconfig(struct ssp_dev *dev, int nparam)
{
	static int last_nparam = -1;

	/* check if it is necessary to re-config SSP */
	if (nparam == last_nparam)
		return;

	ssp_disable(dev);
	ssp_config(dev, (nparam == 2) ? 0x0010058a : 0x00100581, 0x18, 0, 0);

	last_nparam = nparam;
}

static void ssp_send_cmd(uint32_t *cmd, int num)
{
	static int ssp_initialized;
	static struct ssp_dev ssp2;

	int i;

	if (!ssp_initialized) {
		ssp_init(&ssp2, 2, SSP_NO_IRQ);
		ssp_initialized = 1;
	}

	clk_enable(ssp2.ssp->clk);
	for (i = 0; i < num; i++, cmd++) {
		ssp_reconfig(&ssp2, (*cmd >> 30) & 0x3);
		ssp_write_word(&ssp2, *cmd & 0x3fffffff);

		/* FIXME: ssp_flush() is mandatory here to work */
		ssp_flush(&ssp2);
	}
	clk_disable(ssp2.ssp->clk);
}

static void littleton_lcd_power(int on, struct fb_var_screeninfo *var)
{
	if (on) {
		ssp_send_cmd(ARRAY_AND_SIZE(lcd_panel_on));
		ssp_send_cmd(ARRAY_AND_SIZE(lcd_panel_reset));
		if (var->xres > 240) {
			/* VGA */
			ssp_send_cmd(ARRAY_AND_SIZE(lcd_vga_pass_through));
			ssp_send_cmd(ARRAY_AND_SIZE(lcd_panel_config));
			ssp_send_cmd(ARRAY_AND_SIZE(lcd_vga_transfer));
		} else {
			/* QVGA */
			ssp_send_cmd(ARRAY_AND_SIZE(lcd_qvga_pass_through));
			ssp_send_cmd(ARRAY_AND_SIZE(lcd_panel_config));
			ssp_send_cmd(ARRAY_AND_SIZE(lcd_qvga_transfer));
		}
	} else
		ssp_send_cmd(ARRAY_AND_SIZE(lcd_panel_off));
}

static struct pxafb_mode_info tpo_tdo24mtea1_modes[] = {
	[0] = {
		/* VGA */
		.pixclock	= 38250,
		.xres		= 480,
		.yres		= 640,
		.bpp		= 16,
		.hsync_len	= 8,
		.left_margin	= 8,
		.right_margin	= 24,
		.vsync_len	= 2,
		.upper_margin	= 2,
		.lower_margin	= 4,
		.sync		= 0,
	},
	[1] = {
		/* QVGA */
		.pixclock	= 153000,
		.xres		= 240,
		.yres		= 320,
		.bpp		= 16,
		.hsync_len	= 8,
		.left_margin	= 8,
		.right_margin	= 88,
		.vsync_len	= 2,
		.upper_margin	= 2,
		.lower_margin	= 2,
		.sync		= 0,
	},
};

static struct pxafb_mach_info littleton_lcd_info = {
	.modes			= tpo_tdo24mtea1_modes,
	.num_modes		= 2,
	.lccr0			= LCCR0_Act,
	.lccr3			= LCCR3_HSP | LCCR3_VSP,
	.pxafb_lcd_power	= littleton_lcd_power,
};

static void littleton_init_lcd(void)
{
	set_pxa_fb_info(&littleton_lcd_info);
}
#else
static inline void littleton_init_lcd(void) {};
#endif /* CONFIG_FB_PXA || CONFIG_FB_PXA_MODULES */

static void __init littleton_init(void)
{
	/* initialize MFP configurations */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(littleton_mfp_cfg));

	/*
	 * Note: we depend bootloader set the correct
	 * value to MSC register for SMC91x.
	 */
	platform_device_register(&smc91x_device);

	littleton_init_lcd();
}

MACHINE_START(LITTLETON, "Marvell Form Factor Development Platform (aka Littleton)")
	.phys_io	= 0x40000000,
	.boot_params	= 0xa0000100,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= littleton_init,
MACHINE_END
