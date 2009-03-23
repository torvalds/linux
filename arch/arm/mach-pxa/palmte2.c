/*
 * Hardware definitions for Palm Tungsten|E2
 *
 * Author:
 *	Carlos Eduardo Medaglia Dyonisio <cadu@nerdfeliz.com>
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
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/audio.h>
#include <mach/palmte2.h>
#include <mach/mmc.h>
#include <mach/pxafb.h>
#include <mach/mfp-pxa25x.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long palmte2_pin_config[] __initdata = {
	/* MMC */
	GPIO6_MMC_CLK,
	GPIO8_MMC_CS0,
	GPIO10_GPIO,	/* SD detect */
	GPIO55_GPIO,	/* SD power */
	GPIO51_GPIO,	/* SD r/o switch */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* PWM */
	GPIO16_PWM0_OUT,

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

	/* GPIO KEYS */
	GPIO5_GPIO,	/* notes */
	GPIO7_GPIO,	/* tasks */
	GPIO11_GPIO,	/* calendar */
	GPIO13_GPIO,	/* contacts */
	GPIO14_GPIO,	/* center */
	GPIO19_GPIO,	/* left */
	GPIO20_GPIO,	/* right */
	GPIO21_GPIO,	/* down */
	GPIO22_GPIO,	/* up */

	/* MISC */
	GPIO1_RST,	/* reset */
};

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
static int palmte2_mci_init(struct device *dev,
				irq_handler_t palmte2_detect_int, void *data)
{
	int err = 0;

	/* Setup an interrupt for detecting card insert/remove events */
	err = gpio_request(GPIO_NR_PALMTE2_SD_DETECT_N, "SD IRQ");
	if (err)
		goto err;
	err = gpio_direction_input(GPIO_NR_PALMTE2_SD_DETECT_N);
	if (err)
		goto err2;
	err = request_irq(gpio_to_irq(GPIO_NR_PALMTE2_SD_DETECT_N),
			palmte2_detect_int, IRQF_DISABLED | IRQF_SAMPLE_RANDOM |
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"SD/MMC card detect", data);
	if (err) {
		printk(KERN_ERR "%s: cannot request SD/MMC card detect IRQ\n",
				__func__);
		goto err2;
	}

	err = gpio_request(GPIO_NR_PALMTE2_SD_POWER, "SD_POWER");
	if (err)
		goto err3;
	err = gpio_direction_output(GPIO_NR_PALMTE2_SD_POWER, 0);
	if (err)
		goto err4;

	err = gpio_request(GPIO_NR_PALMTE2_SD_READONLY, "SD_READONLY");
	if (err)
		goto err4;
	err = gpio_direction_input(GPIO_NR_PALMTE2_SD_READONLY);
	if (err)
		goto err5;

	printk(KERN_DEBUG "%s: irq registered\n", __func__);

	return 0;

err5:
	gpio_free(GPIO_NR_PALMTE2_SD_READONLY);
err4:
	gpio_free(GPIO_NR_PALMTE2_SD_POWER);
err3:
	free_irq(gpio_to_irq(GPIO_NR_PALMTE2_SD_DETECT_N), data);
err2:
	gpio_free(GPIO_NR_PALMTE2_SD_DETECT_N);
err:
	return err;
}

static void palmte2_mci_exit(struct device *dev, void *data)
{
	gpio_free(GPIO_NR_PALMTE2_SD_READONLY);
	gpio_free(GPIO_NR_PALMTE2_SD_POWER);
	free_irq(gpio_to_irq(GPIO_NR_PALMTE2_SD_DETECT_N), data);
	gpio_free(GPIO_NR_PALMTE2_SD_DETECT_N);
}

static void palmte2_mci_power(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *p_d = dev->platform_data;
	gpio_set_value(GPIO_NR_PALMTE2_SD_POWER, p_d->ocr_mask & (1 << vdd));
}

static int palmte2_mci_get_ro(struct device *dev)
{
	return gpio_get_value(GPIO_NR_PALMTE2_SD_READONLY);
}

static struct pxamci_platform_data palmte2_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.setpower	= palmte2_mci_power,
	.get_ro		= palmte2_mci_get_ro,
	.init 		= palmte2_mci_init,
	.exit		= palmte2_mci_exit,
};

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
static struct gpio_keys_button palmte2_pxa_buttons[] = {
	{KEY_F1,	GPIO_NR_PALMTE2_KEY_CONTACTS,	1, "Contacts" },
	{KEY_F2,	GPIO_NR_PALMTE2_KEY_CALENDAR,	1, "Calendar" },
	{KEY_F3,	GPIO_NR_PALMTE2_KEY_TASKS,	1, "Tasks" },
	{KEY_F4,	GPIO_NR_PALMTE2_KEY_NOTES,	1, "Notes" },
	{KEY_ENTER,	GPIO_NR_PALMTE2_KEY_CENTER,	1, "Center" },
	{KEY_LEFT,	GPIO_NR_PALMTE2_KEY_LEFT,	1, "Left" },
	{KEY_RIGHT,	GPIO_NR_PALMTE2_KEY_RIGHT,	1, "Right" },
	{KEY_DOWN,	GPIO_NR_PALMTE2_KEY_DOWN,	1, "Down" },
	{KEY_UP,	GPIO_NR_PALMTE2_KEY_UP,		1, "Up" },
};

static struct gpio_keys_platform_data palmte2_pxa_keys_data = {
	.buttons	= palmte2_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(palmte2_pxa_buttons),
};

static struct platform_device palmte2_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &palmte2_pxa_keys_data,
	},
};

/******************************************************************************
 * Backlight
 ******************************************************************************/
static struct platform_pwm_backlight_data palmte2_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= PALMTE2_MAX_INTENSITY,
	.dft_brightness	= PALMTE2_MAX_INTENSITY,
	.pwm_period_ns	= PALMTE2_PERIOD_NS,
};

static struct platform_device palmte2_backlight = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa25x_device_pwm0.dev,
		.platform_data	= &palmte2_backlight_data,
	},
};

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
static struct pxafb_mode_info palmte2_lcd_modes[] = {
{
	.pixclock	= 77757,
	.xres		= 320,
	.yres		= 320,
	.bpp		= 16,

	.left_margin	= 28,
	.right_margin	= 7,
	.upper_margin	= 7,
	.lower_margin	= 5,

	.hsync_len	= 4,
	.vsync_len	= 1,
},
};

static struct pxafb_mach_info palmte2_lcd_screen = {
	.modes		= palmte2_lcd_modes,
	.num_modes	= ARRAY_SIZE(palmte2_lcd_modes),
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

/******************************************************************************
 * Machine init
 ******************************************************************************/
static struct platform_device *devices[] __initdata = {
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&palmte2_pxa_keys,
#endif
	&palmte2_backlight,
};

static void __init palmte2_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmte2_pin_config));

	set_pxa_fb_info(&palmte2_lcd_screen);
	pxa_set_mci_info(&palmte2_mci_platform_data);
	pxa_set_ac97_info(NULL);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(PALMTE2, "Palm Tungsten|E2")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= palmte2_init
MACHINE_END
