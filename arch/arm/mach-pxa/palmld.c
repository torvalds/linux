/*
 * Hardware definitions for Palm LifeDrive
 *
 * Author:     Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on work of:
 *		Alex Osborne <ato@meshy.org>
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
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>
#include <linux/wm97xx_batt.h>
#include <linux/power_supply.h>
#include <linux/sysdev.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/pxa27x.h>
#include <mach/audio.h>
#include <mach/palmld.h>
#include <mach/mmc.h>
#include <mach/pxafb.h>
#include <mach/irda.h>
#include <mach/pxa27x_keypad.h>
#include <mach/palmasoc.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long palmld_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO14_GPIO,	/* SD detect */
	GPIO114_GPIO,	/* SD power */
	GPIO116_GPIO,	/* SD r/o switch */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	GPIO95_AC97_nRESET,

	/* IrDA */
	GPIO108_GPIO,	/* ir disable */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* MATRIX KEYPAD */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
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

	/* PWM */
	GPIO16_PWM0_OUT,

	/* GPIO KEYS */
	GPIO10_GPIO,	/* hotsync button */
	GPIO12_GPIO,	/* power switch */
	GPIO15_GPIO,	/* lock switch */

	/* LEDs */
	GPIO52_GPIO,	/* green led */
	GPIO94_GPIO,	/* orange led */

	/* PCMCIA */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO79_PSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO36_GPIO,	/* wifi power */
	GPIO38_GPIO,	/* wifi ready */
	GPIO81_GPIO,	/* wifi reset */

	/* HDD */
	GPIO95_GPIO,	/* HDD irq */
	GPIO115_GPIO,	/* HDD power */

	/* MISC */
	GPIO13_GPIO,	/* earphone detect */
};

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
static int palmld_mci_init(struct device *dev, irq_handler_t palmld_detect_int,
				void *data)
{
	int err = 0;

	/* Setup an interrupt for detecting card insert/remove events */
	err = gpio_request(GPIO_NR_PALMLD_SD_DETECT_N, "SD IRQ");
	if (err)
		goto err;
	err = gpio_direction_input(GPIO_NR_PALMLD_SD_DETECT_N);
	if (err)
		goto err2;
	err = request_irq(gpio_to_irq(GPIO_NR_PALMLD_SD_DETECT_N),
			palmld_detect_int, IRQF_DISABLED | IRQF_SAMPLE_RANDOM |
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"SD/MMC card detect", data);
	if (err) {
		printk(KERN_ERR "%s: cannot request SD/MMC card detect IRQ\n",
				__func__);
		goto err2;
	}

	err = gpio_request(GPIO_NR_PALMLD_SD_POWER, "SD_POWER");
	if (err)
		goto err3;
	err = gpio_direction_output(GPIO_NR_PALMLD_SD_POWER, 0);
	if (err)
		goto err4;

	err = gpio_request(GPIO_NR_PALMLD_SD_READONLY, "SD_READONLY");
	if (err)
		goto err4;
	err = gpio_direction_input(GPIO_NR_PALMLD_SD_READONLY);
	if (err)
		goto err5;

	printk(KERN_DEBUG "%s: irq registered\n", __func__);

	return 0;

err5:
	gpio_free(GPIO_NR_PALMLD_SD_READONLY);
err4:
	gpio_free(GPIO_NR_PALMLD_SD_POWER);
err3:
	free_irq(gpio_to_irq(GPIO_NR_PALMLD_SD_DETECT_N), data);
err2:
	gpio_free(GPIO_NR_PALMLD_SD_DETECT_N);
err:
	return err;
}

static void palmld_mci_exit(struct device *dev, void *data)
{
	gpio_free(GPIO_NR_PALMLD_SD_READONLY);
	gpio_free(GPIO_NR_PALMLD_SD_POWER);
	free_irq(gpio_to_irq(GPIO_NR_PALMLD_SD_DETECT_N), data);
	gpio_free(GPIO_NR_PALMLD_SD_DETECT_N);
}

static void palmld_mci_power(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *p_d = dev->platform_data;
	gpio_set_value(GPIO_NR_PALMLD_SD_POWER, p_d->ocr_mask & (1 << vdd));
}

static int palmld_mci_get_ro(struct device *dev)
{
	return gpio_get_value(GPIO_NR_PALMLD_SD_READONLY);
}

static struct pxamci_platform_data palmld_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.setpower	= palmld_mci_power,
	.get_ro		= palmld_mci_get_ro,
	.init 		= palmld_mci_init,
	.exit		= palmld_mci_exit,
};

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
static unsigned int palmld_matrix_keys[] = {
	KEY(0, 1, KEY_F2),
	KEY(0, 2, KEY_UP),

	KEY(1, 0, KEY_F3),
	KEY(1, 1, KEY_F4),
	KEY(1, 2, KEY_RIGHT),

	KEY(2, 0, KEY_F1),
	KEY(2, 1, KEY_F5),
	KEY(2, 2, KEY_DOWN),

	KEY(3, 0, KEY_F6),
	KEY(3, 1, KEY_ENTER),
	KEY(3, 2, KEY_LEFT),
};

static struct pxa27x_keypad_platform_data palmld_keypad_platform_data = {
	.matrix_key_rows	= 4,
	.matrix_key_cols	= 3,
	.matrix_key_map		= palmld_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(palmld_matrix_keys),

	.debounce_interval	= 30,
};

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
static struct gpio_keys_button palmld_pxa_buttons[] = {
	{KEY_F8, GPIO_NR_PALMLD_HOTSYNC_BUTTON_N, 1, "HotSync Button" },
	{KEY_F9, GPIO_NR_PALMLD_LOCK_SWITCH, 0, "Lock Switch" },
	{KEY_POWER, GPIO_NR_PALMLD_POWER_SWITCH, 0, "Power Switch" },
};

static struct gpio_keys_platform_data palmld_pxa_keys_data = {
	.buttons	= palmld_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(palmld_pxa_buttons),
};

static struct platform_device palmld_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &palmld_pxa_keys_data,
	},
};

/******************************************************************************
 * Backlight
 ******************************************************************************/
static int palmld_backlight_init(struct device *dev)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMLD_BL_POWER, "BL POWER");
	if (ret)
		goto err;
	ret = gpio_direction_output(GPIO_NR_PALMLD_BL_POWER, 0);
	if (ret)
		goto err2;
	ret = gpio_request(GPIO_NR_PALMLD_LCD_POWER, "LCD POWER");
	if (ret)
		goto err2;
	ret = gpio_direction_output(GPIO_NR_PALMLD_LCD_POWER, 0);
	if (ret)
		goto err3;

	return 0;
err3:
	gpio_free(GPIO_NR_PALMLD_LCD_POWER);
err2:
	gpio_free(GPIO_NR_PALMLD_BL_POWER);
err:
	return ret;
}

static int palmld_backlight_notify(int brightness)
{
	gpio_set_value(GPIO_NR_PALMLD_BL_POWER, brightness);
	gpio_set_value(GPIO_NR_PALMLD_LCD_POWER, brightness);
	return brightness;
}

static void palmld_backlight_exit(struct device *dev)
{
	gpio_free(GPIO_NR_PALMLD_BL_POWER);
	gpio_free(GPIO_NR_PALMLD_LCD_POWER);
}

static struct platform_pwm_backlight_data palmld_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= PALMLD_MAX_INTENSITY,
	.dft_brightness	= PALMLD_MAX_INTENSITY,
	.pwm_period_ns	= PALMLD_PERIOD_NS,
	.init		= palmld_backlight_init,
	.notify		= palmld_backlight_notify,
	.exit		= palmld_backlight_exit,
};

static struct platform_device palmld_backlight = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa27x_device_pwm0.dev,
		.platform_data	= &palmld_backlight_data,
	},
};

/******************************************************************************
 * IrDA
 ******************************************************************************/
static int palmld_irda_startup(struct device *dev)
{
	int err;
	err = gpio_request(GPIO_NR_PALMLD_IR_DISABLE, "IR DISABLE");
	if (err)
		goto err;
	err = gpio_direction_output(GPIO_NR_PALMLD_IR_DISABLE, 1);
	if (err)
		gpio_free(GPIO_NR_PALMLD_IR_DISABLE);
err:
	return err;
}

static void palmld_irda_shutdown(struct device *dev)
{
	gpio_free(GPIO_NR_PALMLD_IR_DISABLE);
}

static void palmld_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(GPIO_NR_PALMLD_IR_DISABLE, mode & IR_OFF);
	pxa2xx_transceiver_mode(dev, mode);
}

static struct pxaficp_platform_data palmld_ficp_platform_data = {
	.startup		= palmld_irda_startup,
	.shutdown		= palmld_irda_shutdown,
	.transceiver_cap	= IR_SIRMODE | IR_FIRMODE | IR_OFF,
	.transceiver_mode	= palmld_irda_transceiver_mode,
};

/******************************************************************************
 * LEDs
 ******************************************************************************/
struct gpio_led gpio_leds[] = {
{
	.name			= "palmld:green:led",
	.default_trigger	= "none",
	.gpio			= GPIO_NR_PALMLD_LED_GREEN,
}, {
	.name			= "palmld:amber:led",
	.default_trigger	= "none",
	.gpio			= GPIO_NR_PALMLD_LED_AMBER,
},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device palmld_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	}
};

/******************************************************************************
 * Power supply
 ******************************************************************************/
static int power_supply_init(struct device *dev)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMLD_POWER_DETECT, "CABLE_STATE_AC");
	if (ret)
		goto err1;
	ret = gpio_direction_input(GPIO_NR_PALMLD_POWER_DETECT);
	if (ret)
		goto err2;

	ret = gpio_request(GPIO_NR_PALMLD_USB_DETECT_N, "CABLE_STATE_USB");
	if (ret)
		goto err2;
	ret = gpio_direction_input(GPIO_NR_PALMLD_USB_DETECT_N);
	if (ret)
		goto err3;

	return 0;

err3:
	gpio_free(GPIO_NR_PALMLD_USB_DETECT_N);
err2:
	gpio_free(GPIO_NR_PALMLD_POWER_DETECT);
err1:
	return ret;
}

static int palmld_is_ac_online(void)
{
	return gpio_get_value(GPIO_NR_PALMLD_POWER_DETECT);
}

static int palmld_is_usb_online(void)
{
	return !gpio_get_value(GPIO_NR_PALMLD_USB_DETECT_N);
}

static void power_supply_exit(struct device *dev)
{
	gpio_free(GPIO_NR_PALMLD_USB_DETECT_N);
	gpio_free(GPIO_NR_PALMLD_POWER_DETECT);
}

static char *palmld_supplicants[] = {
	"main-battery",
};

static struct pda_power_pdata power_supply_info = {
	.init            = power_supply_init,
	.is_ac_online    = palmld_is_ac_online,
	.is_usb_online   = palmld_is_usb_online,
	.exit            = power_supply_exit,
	.supplied_to     = palmld_supplicants,
	.num_supplicants = ARRAY_SIZE(palmld_supplicants),
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
	.max_voltage	= PALMLD_BAT_MAX_VOLTAGE,
	.min_voltage	= PALMLD_BAT_MIN_VOLTAGE,
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
static struct palm27x_asoc_info palmld_asoc_pdata = {
	.jack_gpio	= GPIO_NR_PALMLD_EARPHONE_DETECT,
};

static pxa2xx_audio_ops_t palmld_ac97_pdata = {
	.reset_gpio	= 95,
};

static struct platform_device palmld_asoc = {
	.name = "palm27x-asoc",
	.id   = -1,
	.dev  = {
		.platform_data = &palmld_asoc_pdata,
	},
};

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
static struct pxafb_mode_info palmld_lcd_modes[] = {
{
	.pixclock	= 57692,
	.xres		= 320,
	.yres		= 480,
	.bpp		= 16,

	.left_margin	= 32,
	.right_margin	= 1,
	.upper_margin	= 7,
	.lower_margin	= 1,

	.hsync_len	= 4,
	.vsync_len	= 1,
},
};

static struct pxafb_mach_info palmld_lcd_screen = {
	.modes		= palmld_lcd_modes,
	.num_modes	= ARRAY_SIZE(palmld_lcd_modes),
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

/******************************************************************************
 * Power management - standby
 ******************************************************************************/
#ifdef CONFIG_PM
static u32 *addr __initdata;
static u32 resume[3] __initdata = {
	0xe3a00101,	/* mov	r0,	#0x40000000 */
	0xe380060f,	/* orr	r0, r0, #0x00f00000 */
	0xe590f008,	/* ldr	pc, [r0, #0x08] */
};

static int __init palmld_pm_init(void)
{
	int i;

	/* this is where the bootloader jumps */
	addr = phys_to_virt(PALMLD_STR_BASE);

	for (i = 0; i < 3; i++)
		addr[i] = resume[i];

	return 0;
}

device_initcall(palmld_pm_init);
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static struct platform_device *devices[] __initdata = {
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&palmld_pxa_keys,
#endif
	&palmld_backlight,
	&palmld_leds,
	&power_supply,
	&palmld_asoc,
};

static struct map_desc palmld_io_desc[] __initdata = {
{
	.virtual	= PALMLD_IDE_VIRT,
	.pfn		= __phys_to_pfn(PALMLD_IDE_PHYS),
	.length		= PALMLD_IDE_SIZE,
	.type		= MT_DEVICE
},
{
	.virtual	= PALMLD_USB_VIRT,
	.pfn		= __phys_to_pfn(PALMLD_USB_PHYS),
	.length		= PALMLD_USB_SIZE,
	.type		= MT_DEVICE
},
};

static void __init palmld_map_io(void)
{
	pxa_map_io();
	iotable_init(palmld_io_desc, ARRAY_SIZE(palmld_io_desc));
}

static void __init palmld_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmld_pin_config));

	set_pxa_fb_info(&palmld_lcd_screen);
	pxa_set_mci_info(&palmld_mci_platform_data);
	pxa_set_ac97_info(&palmld_ac97_pdata);
	pxa_set_ficp_info(&palmld_ficp_platform_data);
	pxa_set_keypad_info(&palmld_keypad_platform_data);
	wm97xx_bat_set_pdata(&wm97xx_batt_pdata);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(PALMLD, "Palm LifeDrive")
	.phys_io	= PALMLD_PHYS_IO_START,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= palmld_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= palmld_init
MACHINE_END
