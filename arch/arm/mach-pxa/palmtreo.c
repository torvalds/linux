/*
 * Hardware definitions for Palm Treo smartphones
 *
 * currently supported:
 *     Palm Treo 680 (GSM)
 *     Palm Centro 685 (GSM)
 *
 * Author:     Tomas Cech <sleep_walker@suse.cz>
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
#include <linux/power_supply.h>
#include <linux/w1-gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/pxa27x.h>
#include <mach/pxa27x-udc.h>
#include <mach/audio.h>
#include <mach/palmtreo.h>
#include <mach/mmc.h>
#include <mach/pxafb.h>
#include <mach/irda.h>
#include <plat/pxa27x_keypad.h>
#include <mach/udc.h>
#include <mach/ohci.h>
#include <mach/pxa2xx-regs.h>
#include <mach/palmasoc.h>
#include <mach/camera.h>
#include <mach/palm27x.h>

#include <sound/pxa2xx-lib.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long treo_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO113_GPIO,				/* SD detect */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	GPIO95_AC97_nRESET,

	/* IrDA */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* PWM */
	GPIO16_PWM0_OUT,

	/* USB */
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,	/* usb detect */

	/* MATRIX KEYPAD */
	GPIO101_KP_MKIN_1,
	GPIO102_KP_MKIN_2,
	GPIO97_KP_MKIN_3,
	GPIO98_KP_MKIN_4,
	GPIO91_KP_MKIN_6,
	GPIO13_KP_MKIN_7,
	GPIO103_KP_MKOUT_0 | MFP_LPM_DRIVE_HIGH,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
	GPIO96_KP_MKOUT_6,
	GPIO93_KP_DKIN_0 | WAKEUP_ON_LEVEL_HIGH,	/* Hotsync button */

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* Quick Capture Interface */
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO81_CIF_DD_0,
	GPIO55_CIF_DD_1,
	GPIO51_CIF_DD_2,
	GPIO50_CIF_DD_3,
	GPIO52_CIF_DD_4,
	GPIO48_CIF_DD_5,
	GPIO17_CIF_DD_6,
	GPIO12_CIF_DD_7,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* GSM */
	GPIO14_GPIO | WAKEUP_ON_EDGE_BOTH,	/* GSM host wake up */
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,
	GPIO39_FFUART_TXD,
	GPIO41_FFUART_RTS,

	/* MISC. */
	GPIO0_GPIO | WAKEUP_ON_EDGE_BOTH,	/* external power detect */
	GPIO15_GPIO | WAKEUP_ON_EDGE_BOTH,	/* silent switch */
	GPIO116_GPIO,				/* headphone detect */
	GPIO11_GPIO | WAKEUP_ON_EDGE_BOTH,	/* bluetooth host wake up */
};

#ifdef CONFIG_MACH_TREO680
static unsigned long treo680_pin_config[] __initdata = {
	GPIO33_GPIO,    /* SD read only */

	/* MATRIX KEYPAD - different wake up source */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO99_KP_MKIN_5,
};
#endif /* CONFIG_MACH_TREO680 */

#ifdef CONFIG_MACH_CENTRO
static unsigned long centro685_pin_config[] __initdata = {
	/* Bluetooth attached to BT UART*/
	MFP_CFG_OUT(GPIO80, AF0, DRIVE_LOW),    /* power: LOW = off */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* MATRIX KEYPAD - different wake up source */
	GPIO100_KP_MKIN_0,
	GPIO99_KP_MKIN_5 | WAKEUP_ON_LEVEL_HIGH,
};
#endif /* CONFIG_MACH_CENTRO */

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int treo680_matrix_keys[] = {
	KEY(0, 0, KEY_F8),		/* Red/Off/Power */
	KEY(0, 1, KEY_LEFT),
	KEY(0, 2, KEY_LEFTCTRL),	/* Alternate */
	KEY(0, 3, KEY_L),
	KEY(0, 4, KEY_A),
	KEY(0, 5, KEY_Q),
	KEY(0, 6, KEY_P),

	KEY(1, 0, KEY_RIGHTCTRL),	/* Menu */
	KEY(1, 1, KEY_RIGHT),
	KEY(1, 2, KEY_LEFTSHIFT),	/* Left shift */
	KEY(1, 3, KEY_Z),
	KEY(1, 4, KEY_S),
	KEY(1, 5, KEY_W),

	KEY(2, 0, KEY_F1),		/* Phone */
	KEY(2, 1, KEY_UP),
	KEY(2, 2, KEY_0),
	KEY(2, 3, KEY_X),
	KEY(2, 4, KEY_D),
	KEY(2, 5, KEY_E),

	KEY(3, 0, KEY_F10),		/* Calendar */
	KEY(3, 1, KEY_DOWN),
	KEY(3, 2, KEY_SPACE),
	KEY(3, 3, KEY_C),
	KEY(3, 4, KEY_F),
	KEY(3, 5, KEY_R),

	KEY(4, 0, KEY_F12),		/* Mail */
	KEY(4, 1, KEY_KPENTER),
	KEY(4, 2, KEY_RIGHTALT),	/* Alt */
	KEY(4, 3, KEY_V),
	KEY(4, 4, KEY_G),
	KEY(4, 5, KEY_T),

	KEY(5, 0, KEY_F9),		/* Home */
	KEY(5, 1, KEY_PAGEUP),		/* Side up */
	KEY(5, 2, KEY_DOT),
	KEY(5, 3, KEY_B),
	KEY(5, 4, KEY_H),
	KEY(5, 5, KEY_Y),

	KEY(6, 0, KEY_TAB),		/* Side Activate */
	KEY(6, 1, KEY_PAGEDOWN),	/* Side down */
	KEY(6, 2, KEY_ENTER),
	KEY(6, 3, KEY_N),
	KEY(6, 4, KEY_J),
	KEY(6, 5, KEY_U),

	KEY(7, 0, KEY_F6),		/* Green/Call */
	KEY(7, 1, KEY_O),
	KEY(7, 2, KEY_BACKSPACE),
	KEY(7, 3, KEY_M),
	KEY(7, 4, KEY_K),
	KEY(7, 5, KEY_I),
};

static unsigned int centro_matrix_keys[] = {
	KEY(0, 0, KEY_F9),		/* Home */
	KEY(0, 1, KEY_LEFT),
	KEY(0, 2, KEY_LEFTCTRL),	/* Alternate */
	KEY(0, 3, KEY_L),
	KEY(0, 4, KEY_A),
	KEY(0, 5, KEY_Q),
	KEY(0, 6, KEY_P),

	KEY(1, 0, KEY_RIGHTCTRL),	/* Menu */
	KEY(1, 1, KEY_RIGHT),
	KEY(1, 2, KEY_LEFTSHIFT),	/* Left shift */
	KEY(1, 3, KEY_Z),
	KEY(1, 4, KEY_S),
	KEY(1, 5, KEY_W),

	KEY(2, 0, KEY_F1),		/* Phone */
	KEY(2, 1, KEY_UP),
	KEY(2, 2, KEY_0),
	KEY(2, 3, KEY_X),
	KEY(2, 4, KEY_D),
	KEY(2, 5, KEY_E),

	KEY(3, 0, KEY_F10),		/* Calendar */
	KEY(3, 1, KEY_DOWN),
	KEY(3, 2, KEY_SPACE),
	KEY(3, 3, KEY_C),
	KEY(3, 4, KEY_F),
	KEY(3, 5, KEY_R),

	KEY(4, 0, KEY_F12),		/* Mail */
	KEY(4, 1, KEY_KPENTER),
	KEY(4, 2, KEY_RIGHTALT),	/* Alt */
	KEY(4, 3, KEY_V),
	KEY(4, 4, KEY_G),
	KEY(4, 5, KEY_T),

	KEY(5, 0, KEY_F8),		/* Red/Off/Power */
	KEY(5, 1, KEY_PAGEUP),		/* Side up */
	KEY(5, 2, KEY_DOT),
	KEY(5, 3, KEY_B),
	KEY(5, 4, KEY_H),
	KEY(5, 5, KEY_Y),

	KEY(6, 0, KEY_TAB),		/* Side Activate */
	KEY(6, 1, KEY_PAGEDOWN),	/* Side down */
	KEY(6, 2, KEY_ENTER),
	KEY(6, 3, KEY_N),
	KEY(6, 4, KEY_J),
	KEY(6, 5, KEY_U),

	KEY(7, 0, KEY_F6),		/* Green/Call */
	KEY(7, 1, KEY_O),
	KEY(7, 2, KEY_BACKSPACE),
	KEY(7, 3, KEY_M),
	KEY(7, 4, KEY_K),
	KEY(7, 5, KEY_I),
};

static struct pxa27x_keypad_platform_data treo680_keypad_pdata = {
	.matrix_key_rows	= 8,
	.matrix_key_cols	= 7,
	.matrix_key_map		= treo680_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(treo680_matrix_keys),
	.direct_key_map		= { KEY_CONNECT },
	.direct_key_num		= 1,

	.debounce_interval	= 30,
};

static void __init palmtreo_kpc_init(void)
{
	static struct pxa27x_keypad_platform_data *data = &treo680_keypad_pdata;

	if (machine_is_centro()) {
		data->matrix_key_map = centro_matrix_keys;
		data->matrix_key_map_size = ARRAY_SIZE(centro_matrix_keys);
	}

	pxa_set_keypad_info(&treo680_keypad_pdata);
}
#else
static inline void palmtreo_kpc_init(void) {}
#endif

/******************************************************************************
 * USB host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data treo680_ohci_info = {
	.port_mode    = PMM_PERPORT_MODE,
	.flags        = ENABLE_PORT1 | ENABLE_PORT3,
	.power_budget = 0,
};

static void __init palmtreo_uhc_init(void)
{
	if (machine_is_treo680())
		pxa_set_ohci_info(&treo680_ohci_info);
}
#else
static inline void palmtreo_uhc_init(void) {}
#endif

/******************************************************************************
 * Vibra and LEDs
 ******************************************************************************/
#ifdef CONFIG_MACH_TREO680
static struct gpio_led treo680_gpio_leds[] = {
	{
		.name			= "treo680:vibra:vibra",
		.default_trigger	= "none",
		.gpio			= GPIO_NR_TREO680_VIBRATE_EN,
	},
	{
		.name			= "treo680:green:led",
		.default_trigger	= "mmc0",
		.gpio			= GPIO_NR_TREO_GREEN_LED,
	},
	{
		.name			= "treo680:white:keybbl",
		.default_trigger	= "none",
		.gpio			= GPIO_NR_TREO680_KEYB_BL,
	},
};

static struct gpio_led_platform_data treo680_gpio_led_info = {
	.leds		= treo680_gpio_leds,
	.num_leds	= ARRAY_SIZE(treo680_gpio_leds),
};

static struct gpio_led centro_gpio_leds[] = {
	{
		.name			= "centro:vibra:vibra",
		.default_trigger	= "none",
		.gpio			= GPIO_NR_CENTRO_VIBRATE_EN,
	},
	{
		.name			= "centro:green:led",
		.default_trigger	= "mmc0",
		.gpio			= GPIO_NR_TREO_GREEN_LED,
	},
	{
		.name			= "centro:white:keybbl",
		.default_trigger	= "none",
		.active_low		= 1,
		.gpio			= GPIO_NR_CENTRO_KEYB_BL,
	},
};

static struct gpio_led_platform_data centro_gpio_led_info = {
	.leds		= centro_gpio_leds,
	.num_leds	= ARRAY_SIZE(centro_gpio_leds),
};

static struct platform_device palmtreo_leds = {
	.name   = "leds-gpio",
	.id     = -1,
	.dev    = {
		.platform_data  = &treo680_gpio_led_info,
	}
};

static void __init palmtreo_leds_init(void)
{
	if (machine_is_centro())
		palmtreo_leds.dev.platform_data = &centro_gpio_led_info;

	platform_device_register(&palmtreo_leds);
}
#else
static inline void palmtreo_leds_init(void) {}
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static void __init treo_reserve(void)
{
	memblock_reserve(0xa0000000, 0x1000);
	memblock_reserve(0xa2000000, 0x1000);
}

static void __init palmphone_common_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(treo_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	palm27x_pm_init(TREO_STR_BASE);
	palm27x_lcd_init(GPIO_NR_TREO_BL_POWER, &palm_320x320_new_lcd_mode);
	palm27x_udc_init(GPIO_NR_TREO_USB_DETECT, GPIO_NR_TREO_USB_PULLUP, 1);
	palm27x_irda_init(GPIO_NR_TREO_IR_EN);
	palm27x_ac97_init(-1, -1, -1, 95);
	palm27x_pwm_init(GPIO_NR_TREO_BL_POWER, -1);
	palm27x_power_init(GPIO_NR_TREO_POWER_DETECT, -1);
	palm27x_pmic_init();
	palmtreo_kpc_init();
	palmtreo_uhc_init();
	palmtreo_leds_init();
}

#ifdef CONFIG_MACH_TREO680
static void __init treo680_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(treo680_pin_config));
	palmphone_common_init();
	palm27x_mmc_init(GPIO_NR_TREO_SD_DETECT_N, GPIO_NR_TREO680_SD_READONLY,
			GPIO_NR_TREO680_SD_POWER, 0);
}
#endif

#ifdef CONFIG_MACH_CENTRO
static void __init centro_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(centro685_pin_config));
	palmphone_common_init();
	palm27x_mmc_init(GPIO_NR_TREO_SD_DETECT_N, -1,
			GPIO_NR_CENTRO_SD_POWER, 1);
}
#endif

#ifdef CONFIG_MACH_TREO680
MACHINE_START(TREO680, "Palm Treo 680")
	.atag_offset    = 0x100,
	.map_io         = pxa27x_map_io,
	.reserve	= treo_reserve,
	.init_irq       = pxa27x_init_irq,
	.handle_irq       = pxa27x_handle_irq,
	.timer          = &pxa_timer,
	.init_machine   = treo680_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_CENTRO
MACHINE_START(CENTRO, "Palm Centro 685")
	.atag_offset    = 0x100,
	.map_io         = pxa27x_map_io,
	.reserve	= treo_reserve,
	.init_irq       = pxa27x_init_irq,
	.handle_irq       = pxa27x_handle_irq,
	.timer          = &pxa_timer,
	.init_machine	= centro_init,
	.restart	= pxa_restart,
MACHINE_END
#endif
