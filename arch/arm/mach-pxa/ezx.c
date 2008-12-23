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
#include <linux/input.h>

#include <asm/setup.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/i2c.h>
#include <mach/hardware.h>
#include <mach/pxa27x_keypad.h>

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

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* PCAP SSP */
	GPIO29_SSP1_SCLK,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,
	GPIO24_GPIO,				/* pcap chip select */
	GPIO1_GPIO,				/* pcap interrupt */
	GPIO4_GPIO,				/* WDI_AP */
	GPIO55_GPIO,				/* SYS_RESTART */

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO11_GPIO,				/* mmc detect */

	/* usb to external transceiver */
	GPIO34_USB_P2_2,
	GPIO35_USB_P2_1,
	GPIO36_USB_P2_4,
	GPIO39_USB_P2_6,
	GPIO40_USB_P2_5,
	GPIO53_USB_P2_3,

	/* usb to Neptune GSM chip */
	GPIO30_USB_P3_2,
	GPIO31_USB_P3_6,
	GPIO90_USB_P3_5,
	GPIO91_USB_P3_1,
	GPIO56_USB_P3_4,
	GPIO113_USB_P3_3,
};

#if defined(CONFIG_MACH_EZX_A780) || defined(CONFIG_MACH_EZX_E680)
static unsigned long gen1_pin_config[] __initdata = {
	/* flip / lockswitch */
	GPIO12_GPIO,

	/* bluetooth (bcm2035) */
	GPIO14_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* HOSTWAKE */
	GPIO48_GPIO,				/* RESET */
	GPIO28_GPIO,				/* WAKEUP */

	/* Neptune handshake */
	GPIO0_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* BP_RDY */
	GPIO57_GPIO,				/* AP_RDY */
	GPIO13_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* WDI */
	GPIO3_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* WDI2 */
	GPIO82_GPIO,				/* RESET */
	GPIO99_GPIO,				/* TC_MM_EN */

	/* sound */
	GPIO52_SSP3_SCLK,
	GPIO83_SSP3_SFRM,
	GPIO81_SSP3_TXD,
	GPIO89_SSP3_RXD,

	/* ssp2 pins to in */
	GPIO22_GPIO,				/* SSP2_SCLK */
	GPIO37_GPIO,				/* SSP2_SFRM */
	GPIO38_GPIO,				/* SSP2_TXD */
	GPIO88_GPIO,				/* SSP2_RXD */

	/* camera */
	GPIO23_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO85_CIF_LV,
	GPIO84_CIF_FV,
	GPIO27_CIF_DD_0,
	GPIO114_CIF_DD_1,
	GPIO51_CIF_DD_2,
	GPIO115_CIF_DD_3,
	GPIO95_CIF_DD_4,
	GPIO94_CIF_DD_5,
	GPIO17_CIF_DD_6,
	GPIO108_CIF_DD_7,
	GPIO50_GPIO,				/* CAM_EN */
	GPIO19_GPIO,				/* CAM_RST */

	/* EMU */
	GPIO120_GPIO,				/* EMU_MUX1 */
	GPIO119_GPIO,				/* EMU_MUX2 */
	GPIO86_GPIO,				/* SNP_INT_CTL */
	GPIO87_GPIO,				/* SNP_INT_IN */
};
#endif

#if defined(CONFIG_MACH_EZX_A1200) || defined(CONFIG_MACH_EZX_A910) || \
	defined(CONFIG_MACH_EZX_E2) || defined(CONFIG_MACH_EZX_E6)
static unsigned long gen2_pin_config[] __initdata = {
	/* flip / lockswitch */
	GPIO15_GPIO,

	/* EOC */
	GPIO10_GPIO,

	/* bluetooth (bcm2045) */
	GPIO13_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* HOSTWAKE */
	GPIO37_GPIO,				/* RESET */
	GPIO57_GPIO,				/* WAKEUP */

	/* Neptune handshake */
	GPIO0_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* BP_RDY */
	GPIO96_GPIO,				/* AP_RDY */
	GPIO3_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* WDI */
	GPIO116_GPIO,				/* RESET */
	GPIO41_GPIO,				/* BP_FLASH */

	/* sound */
	GPIO52_SSP3_SCLK,
	GPIO83_SSP3_SFRM,
	GPIO81_SSP3_TXD,
	GPIO82_SSP3_RXD,

	/* ssp2 pins to in */
	GPIO22_GPIO,				/* SSP2_SCLK */
	GPIO14_GPIO,				/* SSP2_SFRM */
	GPIO38_GPIO,				/* SSP2_TXD */
	GPIO88_GPIO,				/* SSP2_RXD */

	/* camera */
	GPIO23_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO85_CIF_LV,
	GPIO84_CIF_FV,
	GPIO27_CIF_DD_0,
	GPIO114_CIF_DD_1,
	GPIO51_CIF_DD_2,
	GPIO115_CIF_DD_3,
	GPIO95_CIF_DD_4,
	GPIO48_CIF_DD_5,
	GPIO93_CIF_DD_6,
	GPIO12_CIF_DD_7,
	GPIO50_GPIO,				/* CAM_EN */
	GPIO28_GPIO,				/* CAM_RST */
	GPIO17_GPIO,				/* CAM_FLASH */
};
#endif

#ifdef CONFIG_MACH_EZX_A780
static unsigned long a780_pin_config[] __initdata = {
	/* keypad */
	GPIO93_KP_DKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,

	/* attenuate sound */
	GPIO96_GPIO,
};
#endif

#ifdef CONFIG_MACH_EZX_E680
static unsigned long e680_pin_config[] __initdata = {
	/* keypad */
	GPIO93_KP_DKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO96_KP_DKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_DKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_DKIN_5 | WAKEUP_ON_LEVEL_HIGH,
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,

	/* MIDI */
	GPIO79_GPIO,				/* VA_SEL_BUL */
	GPIO80_GPIO,				/* FLT_SEL_BUL */
	GPIO78_GPIO,				/* MIDI_RESET */
	GPIO33_GPIO,				/* MIDI_CS */
	GPIO15_GPIO,				/* MIDI_IRQ */
	GPIO49_GPIO,				/* MIDI_NPWE */
	GPIO18_GPIO,				/* MIDI_RDY */

	/* leds */
	GPIO46_GPIO,
	GPIO47_GPIO,
};
#endif

#ifdef CONFIG_MACH_EZX_A1200
static unsigned long a1200_pin_config[] __initdata = {
	/* keypad */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
};
#endif

#ifdef CONFIG_MACH_EZX_A910
static unsigned long a910_pin_config[] __initdata = {
	/* keypad */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,

	/* WLAN */
	GPIO89_GPIO,				/* RESET */
	GPIO33_GPIO,				/* WAKEUP */
	GPIO94_GPIO | WAKEUP_ON_LEVEL_HIGH,	/* HOSTWAKE */

	/* MMC CS */
	GPIO20_GPIO,
};
#endif

#ifdef CONFIG_MACH_EZX_E2
static unsigned long e2_pin_config[] __initdata = {
	/* keypad */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
};
#endif

#ifdef CONFIG_MACH_EZX_E6
static unsigned long e6_pin_config[] __initdata = {
	/* keypad */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
};
#endif

/* KEYPAD */
#ifdef CONFIG_MACH_EZX_A780
static unsigned int a780_key_map[] = {
	KEY(0, 0, KEY_SEND),
	KEY(0, 1, KEY_BACK),
	KEY(0, 2, KEY_END),
	KEY(0, 3, KEY_PAGEUP),
	KEY(0, 4, KEY_UP),

	KEY(1, 0, KEY_NUMERIC_1),
	KEY(1, 1, KEY_NUMERIC_2),
	KEY(1, 2, KEY_NUMERIC_3),
	KEY(1, 3, KEY_SELECT),
	KEY(1, 4, KEY_KPENTER),

	KEY(2, 0, KEY_NUMERIC_4),
	KEY(2, 1, KEY_NUMERIC_5),
	KEY(2, 2, KEY_NUMERIC_6),
	KEY(2, 3, KEY_RECORD),
	KEY(2, 4, KEY_LEFT),

	KEY(3, 0, KEY_NUMERIC_7),
	KEY(3, 1, KEY_NUMERIC_8),
	KEY(3, 2, KEY_NUMERIC_9),
	KEY(3, 3, KEY_HOME),
	KEY(3, 4, KEY_RIGHT),

	KEY(4, 0, KEY_NUMERIC_STAR),
	KEY(4, 1, KEY_NUMERIC_0),
	KEY(4, 2, KEY_NUMERIC_POUND),
	KEY(4, 3, KEY_PAGEDOWN),
	KEY(4, 4, KEY_DOWN),
};

static struct pxa27x_keypad_platform_data a780_keypad_platform_data = {
	.matrix_key_rows = 5,
	.matrix_key_cols = 5,
	.matrix_key_map = a780_key_map,
	.matrix_key_map_size = ARRAY_SIZE(a780_key_map),

	.direct_key_map = { KEY_CAMERA },
	.direct_key_num = 1,

	.debounce_interval = 30,
};
#endif /* CONFIG_MACH_EZX_A780 */

#ifdef CONFIG_MACH_EZX_E680
static unsigned int e680_key_map[] = {
	KEY(0, 0, KEY_UP),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_RESERVED),
	KEY(0, 3, KEY_SEND),

	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_LEFT),
	KEY(1, 2, KEY_PAGEUP),
	KEY(1, 3, KEY_PAGEDOWN),

	KEY(2, 0, KEY_RESERVED),
	KEY(2, 1, KEY_RESERVED),
	KEY(2, 2, KEY_RESERVED),
	KEY(2, 3, KEY_KPENTER),
};

static struct pxa27x_keypad_platform_data e680_keypad_platform_data = {
	.matrix_key_rows = 3,
	.matrix_key_cols = 4,
	.matrix_key_map = e680_key_map,
	.matrix_key_map_size = ARRAY_SIZE(e680_key_map),

	.direct_key_map = {
		KEY_CAMERA,
		KEY_RESERVED,
		KEY_RESERVED,
		KEY_F1,
		KEY_CANCEL,
		KEY_F2,
	},
	.direct_key_num = 6,

	.debounce_interval = 30,
};
#endif /* CONFIG_MACH_EZX_E680 */

#ifdef CONFIG_MACH_EZX_A1200
static unsigned int a1200_key_map[] = {
	KEY(0, 0, KEY_RESERVED),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_PAGEDOWN),
	KEY(0, 3, KEY_RESERVED),
	KEY(0, 4, KEY_RESERVED),
	KEY(0, 5, KEY_RESERVED),

	KEY(1, 0, KEY_RESERVED),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_CAMERA),
	KEY(1, 3, KEY_RESERVED),
	KEY(1, 4, KEY_RESERVED),
	KEY(1, 5, KEY_RESERVED),

	KEY(2, 0, KEY_RESERVED),
	KEY(2, 1, KEY_KPENTER),
	KEY(2, 2, KEY_RECORD),
	KEY(2, 3, KEY_RESERVED),
	KEY(2, 4, KEY_RESERVED),
	KEY(2, 5, KEY_SELECT),

	KEY(3, 0, KEY_RESERVED),
	KEY(3, 1, KEY_UP),
	KEY(3, 2, KEY_SEND),
	KEY(3, 3, KEY_RESERVED),
	KEY(3, 4, KEY_RESERVED),
	KEY(3, 5, KEY_RESERVED),

	KEY(4, 0, KEY_RESERVED),
	KEY(4, 1, KEY_LEFT),
	KEY(4, 2, KEY_PAGEUP),
	KEY(4, 3, KEY_RESERVED),
	KEY(4, 4, KEY_RESERVED),
	KEY(4, 5, KEY_RESERVED),
};

static struct pxa27x_keypad_platform_data a1200_keypad_platform_data = {
	.matrix_key_rows = 5,
	.matrix_key_cols = 6,
	.matrix_key_map = a1200_key_map,
	.matrix_key_map_size = ARRAY_SIZE(a1200_key_map),

	.debounce_interval = 30,
};
#endif /* CONFIG_MACH_EZX_A1200 */

#ifdef CONFIG_MACH_EZX_E6
static unsigned int e6_key_map[] = {
	KEY(0, 0, KEY_RESERVED),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_PAGEDOWN),
	KEY(0, 3, KEY_RESERVED),
	KEY(0, 4, KEY_RESERVED),
	KEY(0, 5, KEY_NEXTSONG),

	KEY(1, 0, KEY_RESERVED),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_PROG1),
	KEY(1, 3, KEY_RESERVED),
	KEY(1, 4, KEY_RESERVED),
	KEY(1, 5, KEY_RESERVED),

	KEY(2, 0, KEY_RESERVED),
	KEY(2, 1, KEY_ENTER),
	KEY(2, 2, KEY_CAMERA),
	KEY(2, 3, KEY_RESERVED),
	KEY(2, 4, KEY_RESERVED),
	KEY(2, 5, KEY_WWW),

	KEY(3, 0, KEY_RESERVED),
	KEY(3, 1, KEY_UP),
	KEY(3, 2, KEY_SEND),
	KEY(3, 3, KEY_RESERVED),
	KEY(3, 4, KEY_RESERVED),
	KEY(3, 5, KEY_PLAYPAUSE),

	KEY(4, 0, KEY_RESERVED),
	KEY(4, 1, KEY_LEFT),
	KEY(4, 2, KEY_PAGEUP),
	KEY(4, 3, KEY_RESERVED),
	KEY(4, 4, KEY_RESERVED),
	KEY(4, 5, KEY_PREVIOUSSONG),
};

static struct pxa27x_keypad_platform_data e6_keypad_platform_data = {
	.matrix_key_rows = 5,
	.matrix_key_cols = 6,
	.matrix_key_map = e6_key_map,
	.matrix_key_map_size = ARRAY_SIZE(e6_key_map),

	.debounce_interval = 30,
};
#endif /* CONFIG_MACH_EZX_E6 */

#ifdef CONFIG_MACH_EZX_A910
static unsigned int a910_key_map[] = {
	KEY(0, 0, KEY_NUMERIC_6),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_PAGEDOWN),
	KEY(0, 3, KEY_KPENTER),
	KEY(0, 4, KEY_NUMERIC_5),
	KEY(0, 5, KEY_CAMERA),

	KEY(1, 0, KEY_NUMERIC_8),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_RESERVED),
	KEY(1, 3, KEY_F1), /* Left SoftKey */
	KEY(1, 4, KEY_NUMERIC_STAR),
	KEY(1, 5, KEY_RESERVED),

	KEY(2, 0, KEY_NUMERIC_7),
	KEY(2, 1, KEY_NUMERIC_9),
	KEY(2, 2, KEY_RECORD),
	KEY(2, 3, KEY_F2), /* Right SoftKey */
	KEY(2, 4, KEY_BACK),
	KEY(2, 5, KEY_SELECT),

	KEY(3, 0, KEY_NUMERIC_2),
	KEY(3, 1, KEY_UP),
	KEY(3, 2, KEY_SEND),
	KEY(3, 3, KEY_NUMERIC_0),
	KEY(3, 4, KEY_NUMERIC_1),
	KEY(3, 5, KEY_RECORD),

	KEY(4, 0, KEY_NUMERIC_4),
	KEY(4, 1, KEY_LEFT),
	KEY(4, 2, KEY_PAGEUP),
	KEY(4, 3, KEY_NUMERIC_POUND),
	KEY(4, 4, KEY_NUMERIC_3),
	KEY(4, 5, KEY_RESERVED),
};

static struct pxa27x_keypad_platform_data a910_keypad_platform_data = {
	.matrix_key_rows = 5,
	.matrix_key_cols = 6,
	.matrix_key_map = a910_key_map,
	.matrix_key_map_size = ARRAY_SIZE(a910_key_map),

	.debounce_interval = 30,
};
#endif /* CONFIG_MACH_EZX_A910 */

#ifdef CONFIG_MACH_EZX_E2
static unsigned int e2_key_map[] = {
	KEY(0, 0, KEY_NUMERIC_6),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_NUMERIC_9),
	KEY(0, 3, KEY_NEXTSONG),
	KEY(0, 4, KEY_NUMERIC_5),
	KEY(0, 5, KEY_F1), /* Left SoftKey */

	KEY(1, 0, KEY_NUMERIC_8),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_RESERVED),
	KEY(1, 3, KEY_PAGEUP),
	KEY(1, 4, KEY_NUMERIC_STAR),
	KEY(1, 5, KEY_F2), /* Right SoftKey */

	KEY(2, 0, KEY_NUMERIC_7),
	KEY(2, 1, KEY_KPENTER),
	KEY(2, 2, KEY_RECORD),
	KEY(2, 3, KEY_PAGEDOWN),
	KEY(2, 4, KEY_BACK),
	KEY(2, 5, KEY_NUMERIC_0),

	KEY(3, 0, KEY_NUMERIC_2),
	KEY(3, 1, KEY_UP),
	KEY(3, 2, KEY_SEND),
	KEY(3, 3, KEY_PLAYPAUSE),
	KEY(3, 4, KEY_NUMERIC_1),
	KEY(3, 5, KEY_SOUND), /* Music SoftKey */

	KEY(4, 0, KEY_NUMERIC_4),
	KEY(4, 1, KEY_LEFT),
	KEY(4, 2, KEY_NUMERIC_POUND),
	KEY(4, 3, KEY_PREVIOUSSONG),
	KEY(4, 4, KEY_NUMERIC_3),
	KEY(4, 5, KEY_RESERVED),
};

static struct pxa27x_keypad_platform_data e2_keypad_platform_data = {
	.matrix_key_rows = 5,
	.matrix_key_cols = 6,
	.matrix_key_map = e2_key_map,
	.matrix_key_map_size = ARRAY_SIZE(e2_key_map),

	.debounce_interval = 30,
};
#endif /* CONFIG_MACH_EZX_E2 */

#ifdef CONFIG_MACH_EZX_A780
static void __init a780_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gen1_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(a780_pin_config));

	pxa_set_i2c_info(NULL);

	set_pxa_fb_info(&ezx_fb_info_1);

	pxa_set_keypad_info(&a780_keypad_platform_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(EZX_A780, "Motorola EZX A780")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = a780_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_E680
static struct i2c_board_info __initdata e680_i2c_board_info[] = {
	{ I2C_BOARD_INFO("tea5767", 0x81) },
};

static void __init e680_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gen1_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e680_pin_config));

	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(e680_i2c_board_info));

	set_pxa_fb_info(&ezx_fb_info_1);

	pxa_set_keypad_info(&e680_keypad_platform_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(EZX_E680, "Motorola EZX E680")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = e680_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_A1200
static struct i2c_board_info __initdata a1200_i2c_board_info[] = {
	{ I2C_BOARD_INFO("tea5767", 0x81) },
};

static void __init a1200_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gen2_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(a1200_pin_config));

	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(a1200_i2c_board_info));

	set_pxa_fb_info(&ezx_fb_info_2);

	pxa_set_keypad_info(&a1200_keypad_platform_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(EZX_A1200, "Motorola EZX A1200")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = a1200_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_A910
static void __init a910_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gen2_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(a910_pin_config));

	pxa_set_i2c_info(NULL);

	set_pxa_fb_info(&ezx_fb_info_2);

	pxa_set_keypad_info(&a910_keypad_platform_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(EZX_A910, "Motorola EZX A910")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = a910_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_E6
static struct i2c_board_info __initdata e6_i2c_board_info[] = {
	{ I2C_BOARD_INFO("tea5767", 0x81) },
};

static void __init e6_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gen2_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e6_pin_config));

	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(e6_i2c_board_info));

	set_pxa_fb_info(&ezx_fb_info_2);

	pxa_set_keypad_info(&e6_keypad_platform_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(EZX_E6, "Motorola EZX E6")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = e6_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EZX_E2
static struct i2c_board_info __initdata e2_i2c_board_info[] = {
	{ I2C_BOARD_INFO("tea5767", 0x81) },
};

static void __init e2_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(ezx_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gen2_pin_config));
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e2_pin_config));

	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(e2_i2c_board_info));

	set_pxa_fb_info(&ezx_fb_info_2);

	pxa_set_keypad_info(&e2_keypad_platform_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(EZX_E2, "Motorola EZX E2")
	.phys_io        = 0x40000000,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.init_irq       = pxa27x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = e2_init,
MACHINE_END
#endif
