/*
 * Hardware definitions for Palm Tungsten|T5
 *
 * Author:	Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on work of:
 *		Ales Snuparek <snuparek@atlas.cz>
 *		Justin Kendrick <twilightsentry@gmail.com>
 *		RichardT5 <richard_t5@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (find more info at www.hackndev.com)
 *
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/memblock.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>
#include <linux/wm97xx.h>
#include <linux/power_supply.h>
#include <linux/usb/gpio_vbus.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/pxa27x.h>
#include <mach/audio.h>
#include <mach/palmt5.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/keypad-pxa27x.h>
#include <mach/udc.h>
#include <linux/platform_data/asoc-palm27x.h>
#include <mach/palm27x.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long palmt5_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO14_GPIO,	/* SD detect */
	GPIO114_GPIO,	/* SD power */
	GPIO115_GPIO,	/* SD r/o switch */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	GPIO95_AC97_nRESET,

	/* IrDA */
	GPIO40_GPIO,	/* ir disable */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* USB */
	GPIO15_GPIO,	/* usb detect */
	GPIO93_GPIO,	/* usb power */

	/* MATRIX KEYPAD */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* PWM */
	GPIO16_PWM0_OUT,

	/* FFUART */
	GPIO34_FFUART_RXD,
	GPIO39_FFUART_TXD,

	/* MISC */
	GPIO10_GPIO,	/* hotsync button */
	GPIO90_GPIO,	/* power detect */
	GPIO107_GPIO,	/* earphone detect */
};

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int palmt5_matrix_keys[] = {
	KEY(0, 0, KEY_POWER),
	KEY(0, 1, KEY_F1),
	KEY(0, 2, KEY_ENTER),

	KEY(1, 0, KEY_F2),
	KEY(1, 1, KEY_F3),
	KEY(1, 2, KEY_F4),

	KEY(2, 0, KEY_UP),
	KEY(2, 2, KEY_DOWN),

	KEY(3, 0, KEY_RIGHT),
	KEY(3, 2, KEY_LEFT),
};

static struct pxa27x_keypad_platform_data palmt5_keypad_platform_data = {
	.matrix_key_rows	= 4,
	.matrix_key_cols	= 3,
	.matrix_key_map		= palmt5_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(palmt5_matrix_keys),

	.debounce_interval	= 30,
};

static void __init palmt5_kpc_init(void)
{
	pxa_set_keypad_info(&palmt5_keypad_platform_data);
}
#else
static inline void palmt5_kpc_init(void) {}
#endif

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button palmt5_pxa_buttons[] = {
	{KEY_F8, GPIO_NR_PALMT5_HOTSYNC_BUTTON_N, 1, "HotSync Button" },
};

static struct gpio_keys_platform_data palmt5_pxa_keys_data = {
	.buttons	= palmt5_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(palmt5_pxa_buttons),
};

static struct platform_device palmt5_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &palmt5_pxa_keys_data,
	},
};

static void __init palmt5_keys_init(void)
{
	platform_device_register(&palmt5_pxa_keys);
}
#else
static inline void palmt5_keys_init(void) {}
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static void __init palmt5_reserve(void)
{
	memblock_reserve(0xa0200000, 0x1000);
}

static void __init palmt5_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmt5_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	palm27x_mmc_init(GPIO_NR_PALMT5_SD_DETECT_N, GPIO_NR_PALMT5_SD_READONLY,
			GPIO_NR_PALMT5_SD_POWER, 0);
	palm27x_pm_init(PALMT5_STR_BASE);
	palm27x_lcd_init(-1, &palm_320x480_lcd_mode);
	palm27x_udc_init(GPIO_NR_PALMT5_USB_DETECT_N,
			GPIO_NR_PALMT5_USB_PULLUP, 1);
	palm27x_irda_init(GPIO_NR_PALMT5_IR_DISABLE);
	palm27x_ac97_init(PALMT5_BAT_MIN_VOLTAGE, PALMT5_BAT_MAX_VOLTAGE,
			GPIO_NR_PALMT5_EARPHONE_DETECT, 95);
	palm27x_pwm_init(GPIO_NR_PALMT5_BL_POWER, GPIO_NR_PALMT5_LCD_POWER);
	palm27x_power_init(GPIO_NR_PALMT5_POWER_DETECT, -1);
	palm27x_pmic_init();
	palmt5_kpc_init();
	palmt5_keys_init();
}

MACHINE_START(PALMT5, "Palm Tungsten|T5")
	.atag_offset	= 0x100,
	.map_io		= pxa27x_map_io,
	.reserve	= palmt5_reserve,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.timer		= &pxa_timer,
	.init_machine	= palmt5_init,
	.restart	= pxa_restart,
MACHINE_END
