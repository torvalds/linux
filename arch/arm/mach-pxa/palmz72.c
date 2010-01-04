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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (find more info at www.hackndev.com)
 *
 */

#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>
#include <linux/wm97xx_batt.h>
#include <linux/power_supply.h>
#include <linux/usb/gpio_vbus.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/pxa27x.h>
#include <mach/audio.h>
#include <mach/palmz72.h>
#include <mach/mmc.h>
#include <mach/pxafb.h>
#include <mach/irda.h>
#include <mach/pxa27x_keypad.h>
#include <mach/udc.h>
#include <mach/palmasoc.h>

#include <mach/pm.h>

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
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,
	GPIO20_GPIO,	/* bl power */
	GPIO21_GPIO,	/* LCD border switch */
	GPIO22_GPIO,	/* LCD border color */
	GPIO96_GPIO,	/* lcd power */

	/* Misc. */
	GPIO0_GPIO	| WAKEUP_ON_LEVEL_HIGH,	/* power detect */
	GPIO88_GPIO,				/* green led */
	GPIO27_GPIO,				/* WM9712 IRQ */
};

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
/* SD_POWER is not actually power, but it is more like chip
 * select, i.e. it is inverted */
static struct pxamci_platform_data palmz72_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_card_detect	= GPIO_NR_PALMZ72_SD_DETECT_N,
	.gpio_card_ro		= GPIO_NR_PALMZ72_SD_RO,
	.gpio_power		= GPIO_NR_PALMZ72_SD_POWER_N,
	.gpio_power_invert	= 1,
};

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
static unsigned int palmz72_matrix_keys[] = {
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

static struct pxa27x_keypad_platform_data palmz72_keypad_platform_data = {
	.matrix_key_rows	= 4,
	.matrix_key_cols	= 3,
	.matrix_key_map		= palmz72_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(palmz72_matrix_keys),

	.debounce_interval	= 30,
};

/******************************************************************************
 * Backlight
 ******************************************************************************/
static int palmz72_backlight_init(struct device *dev)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMZ72_BL_POWER, "BL POWER");
	if (ret)
		goto err;
	ret = gpio_direction_output(GPIO_NR_PALMZ72_BL_POWER, 0);
	if (ret)
		goto err2;
	ret = gpio_request(GPIO_NR_PALMZ72_LCD_POWER, "LCD POWER");
	if (ret)
		goto err2;
	ret = gpio_direction_output(GPIO_NR_PALMZ72_LCD_POWER, 0);
	if (ret)
		goto err3;

	return 0;
err3:
	gpio_free(GPIO_NR_PALMZ72_LCD_POWER);
err2:
	gpio_free(GPIO_NR_PALMZ72_BL_POWER);
err:
	return ret;
}

static int palmz72_backlight_notify(struct device *dev, int brightness)
{
	gpio_set_value(GPIO_NR_PALMZ72_BL_POWER, brightness);
	gpio_set_value(GPIO_NR_PALMZ72_LCD_POWER, brightness);
	return brightness;
}

static void palmz72_backlight_exit(struct device *dev)
{
	gpio_free(GPIO_NR_PALMZ72_BL_POWER);
	gpio_free(GPIO_NR_PALMZ72_LCD_POWER);
}

static struct platform_pwm_backlight_data palmz72_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= PALMZ72_MAX_INTENSITY,
	.dft_brightness	= PALMZ72_MAX_INTENSITY,
	.pwm_period_ns	= PALMZ72_PERIOD_NS,
	.init		= palmz72_backlight_init,
	.notify		= palmz72_backlight_notify,
	.exit		= palmz72_backlight_exit,
};

static struct platform_device palmz72_backlight = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa27x_device_pwm0.dev,
		.platform_data	= &palmz72_backlight_data,
	},
};

/******************************************************************************
 * IrDA
 ******************************************************************************/
static struct pxaficp_platform_data palmz72_ficp_platform_data = {
	.gpio_pwdown		= GPIO_NR_PALMZ72_IR_DISABLE,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

/******************************************************************************
 * LEDs
 ******************************************************************************/
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

/******************************************************************************
 * UDC
 ******************************************************************************/
static struct gpio_vbus_mach_info palmz72_udc_info = {
	.gpio_vbus		= GPIO_NR_PALMZ72_USB_DETECT_N,
	.gpio_pullup		= GPIO_NR_PALMZ72_USB_PULLUP,
};

static struct platform_device palmz72_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
	.dev	= {
		.platform_data	= &palmz72_udc_info,
	},
};

/******************************************************************************
 * Power supply
 ******************************************************************************/
static int power_supply_init(struct device *dev)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMZ72_POWER_DETECT, "CABLE_STATE_AC");
	if (ret)
		goto err1;
	ret = gpio_direction_input(GPIO_NR_PALMZ72_POWER_DETECT);
	if (ret)
		goto err2;

	ret = gpio_request(GPIO_NR_PALMZ72_USB_DETECT_N, "CABLE_STATE_USB");
	if (ret)
		goto err2;
	ret = gpio_direction_input(GPIO_NR_PALMZ72_USB_DETECT_N);
	if (ret)
		goto err3;

	return 0;
err3:
	gpio_free(GPIO_NR_PALMZ72_USB_DETECT_N);
err2:
	gpio_free(GPIO_NR_PALMZ72_POWER_DETECT);
err1:
	return ret;
}

static int palmz72_is_ac_online(void)
{
	return gpio_get_value(GPIO_NR_PALMZ72_POWER_DETECT);
}

static int palmz72_is_usb_online(void)
{
	return !gpio_get_value(GPIO_NR_PALMZ72_USB_DETECT_N);
}

static void power_supply_exit(struct device *dev)
{
	gpio_free(GPIO_NR_PALMZ72_USB_DETECT_N);
	gpio_free(GPIO_NR_PALMZ72_POWER_DETECT);
}

static char *palmz72_supplicants[] = {
	"main-battery",
};

static struct pda_power_pdata power_supply_info = {
	.init            = power_supply_init,
	.is_ac_online    = palmz72_is_ac_online,
	.is_usb_online   = palmz72_is_usb_online,
	.exit            = power_supply_exit,
	.supplied_to     = palmz72_supplicants,
	.num_supplicants = ARRAY_SIZE(palmz72_supplicants),
};

static struct platform_device power_supply = {
	.name = "pda-power",
	.id   = -1,
	.dev  = {
		.platform_data = &power_supply_info,
	},
};

/******************************************************************************
 * WM97xx battery
 ******************************************************************************/
static struct wm97xx_batt_info wm97xx_batt_pdata = {
	.batt_aux	= WM97XX_AUX_ID3,
	.temp_aux	= WM97XX_AUX_ID2,
	.charge_gpio	= -1,
	.max_voltage	= PALMZ72_BAT_MAX_VOLTAGE,
	.min_voltage	= PALMZ72_BAT_MIN_VOLTAGE,
	.batt_mult	= 1000,
	.batt_div	= 414,
	.temp_mult	= 1,
	.temp_div	= 1,
	.batt_tech	= POWER_SUPPLY_TECHNOLOGY_LIPO,
	.batt_name	= "main-batt",
};

/******************************************************************************
 * aSoC audio
 ******************************************************************************/
static struct platform_device palmz72_asoc = {
	.name = "palm27x-asoc",
	.id   = -1,
};

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
static struct pxafb_mode_info palmz72_lcd_modes[] = {
{
	.pixclock	= 115384,
	.xres		= 320,
	.yres		= 320,
	.bpp		= 16,

	.left_margin	= 27,
	.right_margin	= 7,
	.upper_margin	= 7,
	.lower_margin	= 8,

	.hsync_len	= 6,
	.vsync_len	= 1,
},
};

static struct pxafb_mach_info palmz72_lcd_screen = {
	.modes		= palmz72_lcd_modes,
	.num_modes	= ARRAY_SIZE(palmz72_lcd_modes),
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

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

/* sys_device for Palm Zire 72 PM */

static int palmz72_pm_suspend(struct sys_device *dev, pm_message_t msg)
{
	/* setup the resume_info struct for the original bootloader */
	palmz72_resume_info.resume_addr = (u32) pxa_cpu_resume;

	/* Storing memory touched by ROM */
	store_ptr = *PALMZ72_SAVE_DWORD;

	/* Setting PSPR to a proper value */
	PSPR = virt_to_phys(&palmz72_resume_info);

	return 0;
}

static int palmz72_pm_resume(struct sys_device *dev)
{
	*PALMZ72_SAVE_DWORD = store_ptr;
	return 0;
}

static struct sysdev_class palmz72_pm_sysclass = {
	.name = "palmz72_pm",
	.suspend = palmz72_pm_suspend,
	.resume = palmz72_pm_resume,
};

static struct sys_device palmz72_pm_device = {
	.cls = &palmz72_pm_sysclass,
};

static int __init palmz72_pm_init(void)
{
	int ret = -ENODEV;
	if (machine_is_palmz72()) {
		ret = sysdev_class_register(&palmz72_pm_sysclass);
		if (ret == 0)
			ret = sysdev_register(&palmz72_pm_device);
	}
	return ret;
}

device_initcall(palmz72_pm_init);
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static struct platform_device *devices[] __initdata = {
	&palmz72_backlight,
	&palmz72_leds,
	&palmz72_asoc,
	&power_supply,
	&palmz72_gpio_vbus,
};

/* setup udc GPIOs initial state */
static void __init palmz72_udc_init(void)
{
	if (!gpio_request(GPIO_NR_PALMZ72_USB_PULLUP, "USB Pullup")) {
		gpio_direction_output(GPIO_NR_PALMZ72_USB_PULLUP, 0);
		gpio_free(GPIO_NR_PALMZ72_USB_PULLUP);
	}
}

static void __init palmz72_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmz72_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	set_pxa_fb_info(&palmz72_lcd_screen);
	pxa_set_mci_info(&palmz72_mci_platform_data);
	palmz72_udc_init();
	pxa_set_ac97_info(NULL);
	pxa_set_ficp_info(&palmz72_ficp_platform_data);
	pxa_set_keypad_info(&palmz72_keypad_platform_data);
	wm97xx_bat_set_pdata(&wm97xx_batt_pdata);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(PALMZ72, "Palm Zire72")
	.phys_io	= 0x40000000,
	.io_pg_offst	= io_p2v(0x40000000),
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= palmz72_init
MACHINE_END
