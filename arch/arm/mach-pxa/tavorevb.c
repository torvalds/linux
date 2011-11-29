/*
 *  linux/arch/arm/mach-pxa/tavorevb.c
 *
 *  Support for the Marvell PXA930 Evaluation Board
 *
 *  Copyright (C) 2007-2008 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/smc91x.h>
#include <linux/pwm_backlight.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa930.h>
#include <mach/pxafb.h>
#include <plat/pxa27x_keypad.h>

#include "devices.h"
#include "generic.h"

/* Tavor EVB MFP configurations */
static mfp_cfg_t tavorevb_mfp_cfg[] __initdata = {
	/* Ethernet */
	DF_nCS1_nCS3,
	GPIO47_GPIO,

	/* LCD */
	GPIO23_LCD_DD0,
	GPIO24_LCD_DD1,
	GPIO25_LCD_DD2,
	GPIO26_LCD_DD3,
	GPIO27_LCD_DD4,
	GPIO28_LCD_DD5,
	GPIO29_LCD_DD6,
	GPIO44_LCD_DD7,
	GPIO21_LCD_CS,
	GPIO22_LCD_CS2,

	GPIO17_LCD_FCLK_RD,
	GPIO18_LCD_LCLK_A0,
	GPIO19_LCD_PCLK_WR,

	/* LCD Backlight */
	GPIO43_PWM3,	/* primary backlight */
	GPIO32_PWM0,	/* secondary backlight */

	/* Keypad */
	GPIO0_KP_MKIN_0,
	GPIO2_KP_MKIN_1,
	GPIO4_KP_MKIN_2,
	GPIO6_KP_MKIN_3,
	GPIO8_KP_MKIN_4,
	GPIO10_KP_MKIN_5,
	GPIO12_KP_MKIN_6,
	GPIO1_KP_MKOUT_0,
	GPIO3_KP_MKOUT_1,
	GPIO5_KP_MKOUT_2,
	GPIO7_KP_MKOUT_3,
	GPIO9_KP_MKOUT_4,
	GPIO11_KP_MKOUT_5,
	GPIO13_KP_MKOUT_6,

	GPIO14_KP_DKIN_2,
	GPIO15_KP_DKIN_3,
};

#define TAVOREVB_ETH_PHYS	(0x14000000)

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (TAVOREVB_ETH_PHYS + 0x300),
		.end	= (TAVOREVB_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO47)),
		.end	= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO47)),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct smc91x_platdata tavorevb_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT | SMC91X_USE_DMA,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &tavorevb_smc91x_info,
	},
};

#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int tavorevb_matrix_key_map[] = {
	/* KEY(row, col, key_code) */
	KEY(0, 4, KEY_A), KEY(0, 5, KEY_B), KEY(0, 6, KEY_C),
	KEY(1, 4, KEY_E), KEY(1, 5, KEY_F), KEY(1, 6, KEY_G),
	KEY(2, 4, KEY_I), KEY(2, 5, KEY_J), KEY(2, 6, KEY_K),
	KEY(3, 4, KEY_M), KEY(3, 5, KEY_N), KEY(3, 6, KEY_O),
	KEY(4, 5, KEY_R), KEY(4, 6, KEY_S),
	KEY(5, 4, KEY_U), KEY(5, 4, KEY_V), KEY(5, 6, KEY_W),

	KEY(6, 4, KEY_Y), KEY(6, 5, KEY_Z),

	KEY(0, 3, KEY_0), KEY(2, 0, KEY_1), KEY(2, 1, KEY_2), KEY(2, 2, KEY_3),
	KEY(2, 3, KEY_4), KEY(1, 0, KEY_5), KEY(1, 1, KEY_6), KEY(1, 2, KEY_7),
	KEY(1, 3, KEY_8), KEY(0, 2, KEY_9),

	KEY(6, 6, KEY_SPACE),
	KEY(0, 0, KEY_KPASTERISK), 	/* * */
	KEY(0, 1, KEY_KPDOT), 		/* # */

	KEY(4, 1, KEY_UP),
	KEY(4, 3, KEY_DOWN),
	KEY(4, 0, KEY_LEFT),
	KEY(4, 2, KEY_RIGHT),
	KEY(6, 0, KEY_HOME),
	KEY(3, 2, KEY_END),
	KEY(6, 1, KEY_DELETE),
	KEY(5, 2, KEY_BACK),
	KEY(6, 3, KEY_CAPSLOCK),	/* KEY_LEFTSHIFT), */

	KEY(4, 4, KEY_ENTER),		/* scroll push */
	KEY(6, 2, KEY_ENTER),		/* keypad action */

	KEY(3, 1, KEY_SEND),
	KEY(5, 3, KEY_RECORD),
	KEY(5, 0, KEY_VOLUMEUP),
	KEY(5, 1, KEY_VOLUMEDOWN),

	KEY(3, 0, KEY_F22),	/* soft1 */
	KEY(3, 3, KEY_F23),	/* soft2 */
};

static struct pxa27x_keypad_platform_data tavorevb_keypad_info = {
	.matrix_key_rows	= 7,
	.matrix_key_cols	= 7,
	.matrix_key_map		= tavorevb_matrix_key_map,
	.matrix_key_map_size	= ARRAY_SIZE(tavorevb_matrix_key_map),
	.debounce_interval	= 30,
};

static void __init tavorevb_init_keypad(void)
{
	pxa_set_keypad_info(&tavorevb_keypad_info);
}
#else
static inline void tavorevb_init_keypad(void) {}
#endif /* CONFIG_KEYBOARD_PXA27x || CONFIG_KEYBOARD_PXA27x_MODULE */

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct platform_pwm_backlight_data tavorevb_backlight_data[] = {
	[0] = {
		/* primary backlight */
		.pwm_id		= 2,
		.max_brightness	= 100,
		.dft_brightness	= 100,
		.pwm_period_ns	= 100000,
	},
	[1] = {
		/* secondary backlight */
		.pwm_id		= 0,
		.max_brightness	= 100,
		.dft_brightness	= 100,
		.pwm_period_ns	= 100000,
	},
};

static struct platform_device tavorevb_backlight_devices[] = {
	[0] = {
		.name		= "pwm-backlight",
		.id		= 0,
		.dev		= {
			.platform_data = &tavorevb_backlight_data[0],
		},
	},
	[1] = {
		.name		= "pwm-backlight",
		.id		= 1,
		.dev		= {
			.platform_data = &tavorevb_backlight_data[1],
		},
	},
};

static uint16_t panel_init[] = {
	/* DSTB OUT */
	SMART_CMD(0x00),
	SMART_CMD_NOOP,
	SMART_DELAY(1),

	SMART_CMD(0x00),
	SMART_CMD_NOOP,
	SMART_DELAY(1),

	SMART_CMD(0x00),
	SMART_CMD_NOOP,
	SMART_DELAY(1),

	/* STB OUT */
	SMART_CMD(0x00),
	SMART_CMD(0x1D),
	SMART_DAT(0x00),
	SMART_DAT(0x05),
	SMART_DELAY(1),

	/* P-ON Init sequence */
	SMART_CMD(0x00), /* OSC ON */
	SMART_CMD(0x00),
	SMART_DAT(0x00),
	SMART_DAT(0x01),
	SMART_CMD(0x00),
	SMART_CMD(0x01), /* SOURCE DRIVER SHIFT DIRECTION and display RAM setting */
	SMART_DAT(0x01),
	SMART_DAT(0x27),
	SMART_CMD(0x00),
	SMART_CMD(0x02), /* LINE INV */
	SMART_DAT(0x02),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x03), /* IF mode(1) */
	SMART_DAT(0x01), /* 8bit smart mode(8-8),high speed write mode */
	SMART_DAT(0x30),
	SMART_CMD(0x07),
	SMART_CMD(0x00), /* RAM Write Mode */
	SMART_DAT(0x00),
	SMART_DAT(0x03),
	SMART_CMD(0x00),

	/* DISPLAY Setting,  262K, fixed(NO scroll), no split screen */
	SMART_CMD(0x07),
	SMART_DAT(0x40), /* 16/18/19 BPP */
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x08), /* BP, FP Seting, BP=2H, FP=3H */
	SMART_DAT(0x03),
	SMART_DAT(0x02),
	SMART_CMD(0x00),
	SMART_CMD(0x0C), /* IF mode(2), using internal clock & MPU */
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x0D), /* Frame setting, 1Min. Frequence, 16CLK */
	SMART_DAT(0x00),
	SMART_DAT(0x10),
	SMART_CMD(0x00),
	SMART_CMD(0x12), /* Timing(1),ASW W=4CLK, ASW ST=1CLK */
	SMART_DAT(0x03),
	SMART_DAT(0x02),
	SMART_CMD(0x00),
	SMART_CMD(0x13), /* Timing(2),OEV ST=0.5CLK, OEV ED=1CLK */
	SMART_DAT(0x01),
	SMART_DAT(0x02),
	SMART_CMD(0x00),
	SMART_CMD(0x14), /* Timing(3), ASW HOLD=0.5CLK */
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x15), /* Timing(4), CKV ST=0CLK, CKV ED=1CLK */
	SMART_DAT(0x20),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x1C),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x03),
	SMART_CMD(0x00),
	SMART_DAT(0x04),
	SMART_DAT(0x03),
	SMART_CMD(0x03),
	SMART_CMD(0x01),
	SMART_DAT(0x03),
	SMART_DAT(0x04),
	SMART_CMD(0x03),
	SMART_CMD(0x02),
	SMART_DAT(0x04),
	SMART_DAT(0x03),
	SMART_CMD(0x03),
	SMART_CMD(0x03),
	SMART_DAT(0x03),
	SMART_DAT(0x03),
	SMART_CMD(0x03),
	SMART_CMD(0x04),
	SMART_DAT(0x01),
	SMART_DAT(0x01),
	SMART_CMD(0x03),
	SMART_CMD(0x05),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x04),
	SMART_CMD(0x02),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x04),
	SMART_CMD(0x03),
	SMART_DAT(0x01),
	SMART_DAT(0x3F),
	SMART_DELAY(0),

	/* DISP RAM setting: 240*320 */
	SMART_CMD(0x04), /* HADDR, START 0 */
	SMART_CMD(0x06),
	SMART_DAT(0x00),
	SMART_DAT(0x00), /* x1,3 */
	SMART_CMD(0x04), /* HADDR,  END   4 */
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0xEF), /* x2, 7 */
	SMART_CMD(0x04), /* VADDR, START 8 */
	SMART_CMD(0x08),
	SMART_DAT(0x00), /* y1, 10 */
	SMART_DAT(0x00), /* y1, 11 */
	SMART_CMD(0x04), /* VADDR, END 12 */
	SMART_CMD(0x09),
	SMART_DAT(0x01), /* y2, 14 */
	SMART_DAT(0x3F), /* y2, 15 */
	SMART_CMD(0x02), /* RAM ADDR SETTING 16 */
	SMART_CMD(0x00),
	SMART_DAT(0x00),
	SMART_DAT(0x00), /* x1, 19 */
	SMART_CMD(0x02), /* RAM ADDR SETTING 20 */
	SMART_CMD(0x01),
	SMART_DAT(0x00), /* y1, 22 */
	SMART_DAT(0x00), /* y1, 23 */
};

static uint16_t panel_on[] = {
	/* Power-IC ON */
	SMART_CMD(0x01),
	SMART_CMD(0x02),
	SMART_DAT(0x07),
	SMART_DAT(0x7D),
	SMART_CMD(0x01),
	SMART_CMD(0x03),
	SMART_DAT(0x00),
	SMART_DAT(0x05),
	SMART_CMD(0x01),
	SMART_CMD(0x04),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x01),
	SMART_CMD(0x05),
	SMART_DAT(0x00),
	SMART_DAT(0x15),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0xC0),
	SMART_DAT(0x10),
	SMART_DELAY(30),

	/* DISP ON */
	SMART_CMD(0x01),
	SMART_CMD(0x01),
	SMART_DAT(0x00),
	SMART_DAT(0x01),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0xFF),
	SMART_DAT(0xFE),
	SMART_DELAY(150),
};

static uint16_t panel_off[] = {
	SMART_CMD(0x00),
	SMART_CMD(0x1E),
	SMART_DAT(0x00),
	SMART_DAT(0x0A),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0xFF),
	SMART_DAT(0xEE),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0xF8),
	SMART_DAT(0x12),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0xE8),
	SMART_DAT(0x11),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0xC0),
	SMART_DAT(0x11),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0x40),
	SMART_DAT(0x11),
	SMART_CMD(0x01),
	SMART_CMD(0x00),
	SMART_DAT(0x00),
	SMART_DAT(0x10),
};

static uint16_t update_framedata[] = {
	/* write ram */
	SMART_CMD(0x02),
	SMART_CMD(0x02),

	/* write frame data */
	SMART_CMD_WRITE_FRAME,
};

static void ltm020d550_lcd_power(int on, struct fb_var_screeninfo *var)
{
	struct fb_info *info = container_of(var, struct fb_info, var);

	if (on) {
		pxafb_smart_queue(info, ARRAY_AND_SIZE(panel_init));
		pxafb_smart_queue(info, ARRAY_AND_SIZE(panel_on));
	} else {
		pxafb_smart_queue(info, ARRAY_AND_SIZE(panel_off));
	}

	if (pxafb_smart_flush(info))
		pr_err("%s: timed out\n", __func__);
}

static void ltm020d550_update(struct fb_info *info)
{
	pxafb_smart_queue(info, ARRAY_AND_SIZE(update_framedata));
	pxafb_smart_flush(info);
}

static struct pxafb_mode_info toshiba_ltm020d550_modes[] = {
	[0] = {
		.xres			= 240,
		.yres			= 320,
		.bpp			= 16,
		.a0csrd_set_hld		= 30,
		.a0cswr_set_hld		= 30,
		.wr_pulse_width		= 30,
		.rd_pulse_width 	= 170,
		.op_hold_time 		= 30,
		.cmd_inh_time		= 60,

		/* L_LCLK_A0 and L_LCLK_RD active low */
		.sync			= FB_SYNC_HOR_HIGH_ACT |
					  FB_SYNC_VERT_HIGH_ACT,
	},
};

static struct pxafb_mach_info tavorevb_lcd_info = {
	.modes			= toshiba_ltm020d550_modes,
	.num_modes		= 1,
	.lcd_conn		= LCD_SMART_PANEL_8BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_lcd_power	= ltm020d550_lcd_power,
	.smart_update		= ltm020d550_update,
};

static void __init tavorevb_init_lcd(void)
{
	platform_device_register(&tavorevb_backlight_devices[0]);
	platform_device_register(&tavorevb_backlight_devices[1]);
	pxa_set_fb_info(NULL, &tavorevb_lcd_info);
}
#else
static inline void tavorevb_init_lcd(void) {}
#endif /* CONFIG_FB_PXA || CONFIG_FB_PXA_MODULE */

static void __init tavorevb_init(void)
{
	/* initialize MFP configurations */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(tavorevb_mfp_cfg));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	platform_device_register(&smc91x_device);

	tavorevb_init_lcd();
	tavorevb_init_keypad();
}

MACHINE_START(TAVOREVB, "PXA930 Evaluation Board (aka TavorEVB)")
	/* Maintainer: Eric Miao <eric.miao@marvell.com> */
	.atag_offset    = 0x100,
	.map_io         = pxa3xx_map_io,
	.init_irq       = pxa3xx_init_irq,
	.handle_irq       = pxa3xx_handle_irq,
	.timer          = &pxa_timer,
	.init_machine   = tavorevb_init,
MACHINE_END
