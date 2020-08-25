// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware definitions for Palm Zire72
 *
 * Authors:
 *	Vladimir "Farcaller" Pouzanov <farcaller@gmail.com>
 *	Sergey Lapin <slapin@ossfans.org>
 *	Alex Osborne <bobofdoom@gmail.com>
 *	Jan Herman <2hp@seznam.cz>
 *
 * Rewrite for mainline:
 *	Marek Vasut <marek.vasut@gmail.com>
 *
 * (find more info at www.hackndev.com)
 */

#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>
#include <linux/wm97xx.h>
#include <linux/power_supply.h>
#include <linux/platform_data/i2c-gpio.h>
#include <linux/gpio/machine.h>

#include <asm/mach-types.h>
#include <asm/suspend.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "pxa27x.h"
#include <mach/audio.h>
#include "palmz72.h"
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/keypad-pxa27x.h>
#include "udc.h"
#include <linux/platform_data/asoc-palm27x.h>
#include "palm27x.h"

#include "pm.h"
#include <linux/platform_data/media/camera-pxa.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long palmz72_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO14_GPIO,	/* SD detect */
	GPIO115_GPIO,	/* SD RO */
	GPIO98_GPIO,	/* SD power */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	GPIO113_AC97_nRESET,

	/* IrDA */
	GPIO49_GPIO,	/* ir disable */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* PWM */
	GPIO16_PWM0_OUT,

	/* USB */
	GPIO15_GPIO,	/* usb detect */
	GPIO95_GPIO,	/* usb pullup */

	/* Matrix keypad */
	GPIO100_KP_MKIN_0	| WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1	| WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2	| WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3	| WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	GPIO20_GPIO,	/* bl power */
	GPIO21_GPIO,	/* LCD border switch */
	GPIO22_GPIO,	/* LCD border color */
	GPIO96_GPIO,	/* lcd power */

	/* PXA Camera */
	GPIO81_CIF_DD_0,
	GPIO48_CIF_DD_5,
	GPIO50_CIF_DD_3,
	GPIO51_CIF_DD_2,
	GPIO52_CIF_DD_4,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO55_CIF_DD_1,
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,
	GPIO93_CIF_DD_6,
	GPIO108_CIF_DD_7,

	GPIO56_GPIO,	/* OV9640 Powerdown */
	GPIO57_GPIO,	/* OV9640 Reset */
	GPIO91_GPIO,	/* OV9640 Power */

	/* I2C */
	GPIO117_GPIO,	/* I2C_SCL */
	GPIO118_GPIO,	/* I2C_SDA */

	/* Misc. */
	GPIO0_GPIO	| WAKEUP_ON_LEVEL_HIGH,	/* power detect */
	GPIO88_GPIO,				/* green led */
	GPIO27_GPIO,				/* WM9712 IRQ */
};

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static const unsigned int palmz72_matrix_keys[] = {
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

static struct matrix_keymap_data almz72_matrix_keymap_data = {
	.keymap			= palmz72_matrix_keys,
	.keymap_size		= ARRAY_SIZE(palmz72_matrix_keys),
};

static struct pxa27x_keypad_platform_data palmz72_keypad_platform_data = {
	.matrix_key_rows	= 4,
	.matrix_key_cols	= 3,
	.matrix_keymap_data	= &almz72_matrix_keymap_data,

	.debounce_interval	= 30,
};

static void __init palmz72_kpc_init(void)
{
	pxa_set_keypad_info(&palmz72_keypad_platform_data);
}
#else
static inline void palmz72_kpc_init(void) {}
#endif

/******************************************************************************
 * LEDs
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led gpio_leds[] = {
	{
		.name			= "palmz72:green:led",
		.default_trigger	= "none",
		.gpio			= GPIO_NR_PALMZ72_LED_GREEN,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device palmz72_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	}
};

static void __init palmz72_leds_init(void)
{
	platform_device_register(&palmz72_leds);
}
#else
static inline void palmz72_leds_init(void) {}
#endif

#ifdef CONFIG_PM

/* We have some black magic here
 * PalmOS ROM on recover expects special struct physical address
 * to be transferred via PSPR. Using this struct PalmOS restores
 * its state after sleep. As for Linux, we need to setup it the
 * same way. More than that, PalmOS ROM changes some values in memory.
 * For now only one location is found, which needs special treatment.
 * Thanks to Alex Osborne, Andrzej Zaborowski, and lots of other people
 * for reading backtraces for me :)
 */

#define PALMZ72_SAVE_DWORD ((unsigned long *)0xc0000050)

static struct palmz72_resume_info palmz72_resume_info = {
	.magic0 = 0xb4e6,
	.magic1 = 1,

	/* reset state, MMU off etc */
	.arm_control = 0,
	.aux_control = 0,
	.ttb = 0,
	.domain_access = 0,
	.process_id = 0,
};

static unsigned long store_ptr;

/* syscore_ops for Palm Zire 72 PM */

static int palmz72_pm_suspend(void)
{
	/* setup the resume_info struct for the original bootloader */
	palmz72_resume_info.resume_addr = (u32) cpu_resume;

	/* Storing memory touched by ROM */
	store_ptr = *PALMZ72_SAVE_DWORD;

	/* Setting PSPR to a proper value */
	PSPR = __pa_symbol(&palmz72_resume_info);

	return 0;
}

static void palmz72_pm_resume(void)
{
	*PALMZ72_SAVE_DWORD = store_ptr;
}

static struct syscore_ops palmz72_pm_syscore_ops = {
	.suspend = palmz72_pm_suspend,
	.resume = palmz72_pm_resume,
};

static int __init palmz72_pm_init(void)
{
	if (machine_is_palmz72()) {
		register_syscore_ops(&palmz72_pm_syscore_ops);
		return 0;
	}
	return -ENODEV;
}

device_initcall(palmz72_pm_init);
#endif

static struct gpiod_lookup_table palmz72_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMZ72_SD_DETECT_N,
			    "cd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMZ72_SD_RO,
			    "wp", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMZ72_SD_POWER_N,
			    "power", GPIO_ACTIVE_LOW),
		{ },
	},
};

/******************************************************************************
 * Machine init
 ******************************************************************************/
static void __init palmz72_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmz72_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	palm27x_mmc_init(&palmz72_mci_gpio_table);
	palm27x_lcd_init(-1, &palm_320x320_lcd_mode);
	palm27x_udc_init(GPIO_NR_PALMZ72_USB_DETECT_N,
			GPIO_NR_PALMZ72_USB_PULLUP, 0);
	palm27x_irda_init(GPIO_NR_PALMZ72_IR_DISABLE);
	palm27x_ac97_init(PALMZ72_BAT_MIN_VOLTAGE, PALMZ72_BAT_MAX_VOLTAGE,
			-1, 113);
	palm27x_pwm_init(-1, -1);
	palm27x_power_init(-1, -1);
	palm27x_pmic_init();
	palmz72_kpc_init();
	palmz72_leds_init();
}

MACHINE_START(PALMZ72, "Palm Zire72")
	.atag_offset	= 0x100,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= palmz72_init,
	.restart	= pxa_restart,
MACHINE_END
