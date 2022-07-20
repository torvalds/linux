// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware definitions for Palm Tungsten|E2
 *
 * Author:
 *	Carlos Eduardo Medaglia Dyonisio <cadu@nerdfeliz.com>
 *
 * Rewrite for mainline:
 *	Marek Vasut <marek.vasut@gmail.com>
 *
 * (find more info at www.hackndev.com)
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/machine.h>
#include <linux/input.h>
#include <linux/pda_power.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>
#include <linux/wm97xx.h>
#include <linux/power_supply.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "pxa25x.h"
#include <linux/platform_data/asoc-pxa.h>
#include "palmte2.h"
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/irda-pxaficp.h>
#include "udc.h"
#include <linux/platform_data/asoc-palm27x.h>

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

	/* USB */
	GPIO15_GPIO,	/* usb detect */
	GPIO53_GPIO,	/* usb power */

	/* IrDA */
	GPIO48_GPIO,	/* ir disable */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

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
	GPIO4_GPIO,	/* Hotsync button */
	GPIO9_GPIO,	/* power detect */
	GPIO15_GPIO,	/* earphone detect */
	GPIO37_GPIO,	/* LCD power */
	GPIO56_GPIO,	/* Backlight power */
};

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
static struct pxamci_platform_data palmte2_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
};

static struct gpiod_lookup_table palmte2_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTE2_SD_DETECT_N,
			    "cd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTE2_SD_READONLY,
			    "wp", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTE2_SD_POWER,
			    "power", GPIO_ACTIVE_HIGH),
		{ },
	},
};

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
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
#endif

/******************************************************************************
 * Backlight
 ******************************************************************************/
static struct pwm_lookup palmte2_pwm_lookup[] = {
	PWM_LOOKUP("pxa25x-pwm.0", 0, "pwm-backlight.0", NULL,
		   PALMTE2_PERIOD_NS, PWM_POLARITY_NORMAL),
};

static struct gpio palmte_bl_gpios[] = {
	{ GPIO_NR_PALMTE2_BL_POWER, GPIOF_INIT_LOW, "Backlight power" },
	{ GPIO_NR_PALMTE2_LCD_POWER, GPIOF_INIT_LOW, "LCD power" },
};

static int palmte2_backlight_init(struct device *dev)
{
	return gpio_request_array(ARRAY_AND_SIZE(palmte_bl_gpios));
}

static int palmte2_backlight_notify(struct device *dev, int brightness)
{
	gpio_set_value(GPIO_NR_PALMTE2_BL_POWER, brightness);
	gpio_set_value(GPIO_NR_PALMTE2_LCD_POWER, brightness);
	return brightness;
}

static void palmte2_backlight_exit(struct device *dev)
{
	gpio_free_array(ARRAY_AND_SIZE(palmte_bl_gpios));
}

static struct platform_pwm_backlight_data palmte2_backlight_data = {
	.max_brightness	= PALMTE2_MAX_INTENSITY,
	.dft_brightness	= PALMTE2_MAX_INTENSITY,
	.init		= palmte2_backlight_init,
	.notify		= palmte2_backlight_notify,
	.exit		= palmte2_backlight_exit,
};

static struct platform_device palmte2_backlight = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa25x_device_pwm0.dev,
		.platform_data	= &palmte2_backlight_data,
	},
};

/******************************************************************************
 * IrDA
 ******************************************************************************/
static struct pxaficp_platform_data palmte2_ficp_platform_data = {
	.gpio_pwdown		= GPIO_NR_PALMTE2_IR_DISABLE,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

/******************************************************************************
 * UDC
 ******************************************************************************/
static struct gpiod_lookup_table palmte2_udc_gpiod_table = {
	.dev_id = "gpio-vbus",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTE2_USB_DETECT_N,
			    "vbus", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTE2_USB_PULLUP,
			    "pullup", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct platform_device palmte2_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
};

/******************************************************************************
 * Power supply
 ******************************************************************************/
static int power_supply_init(struct device *dev)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMTE2_POWER_DETECT, "CABLE_STATE_AC");
	if (ret)
		goto err1;
	ret = gpio_direction_input(GPIO_NR_PALMTE2_POWER_DETECT);
	if (ret)
		goto err2;

	return 0;

err2:
	gpio_free(GPIO_NR_PALMTE2_POWER_DETECT);
err1:
	return ret;
}

static int palmte2_is_ac_online(void)
{
	return gpio_get_value(GPIO_NR_PALMTE2_POWER_DETECT);
}

static void power_supply_exit(struct device *dev)
{
	gpio_free(GPIO_NR_PALMTE2_POWER_DETECT);
}

static char *palmte2_supplicants[] = {
	"main-battery",
};

static struct pda_power_pdata power_supply_info = {
	.init            = power_supply_init,
	.is_ac_online    = palmte2_is_ac_online,
	.exit            = power_supply_exit,
	.supplied_to     = palmte2_supplicants,
	.num_supplicants = ARRAY_SIZE(palmte2_supplicants),
};

static struct platform_device power_supply = {
	.name = "pda-power",
	.id   = -1,
	.dev  = {
		.platform_data = &power_supply_info,
	},
};

/******************************************************************************
 * WM97xx audio, battery
 ******************************************************************************/
static struct wm97xx_batt_pdata palmte2_batt_pdata = {
	.batt_aux	= WM97XX_AUX_ID3,
	.temp_aux	= WM97XX_AUX_ID2,
	.max_voltage	= PALMTE2_BAT_MAX_VOLTAGE,
	.min_voltage	= PALMTE2_BAT_MIN_VOLTAGE,
	.batt_mult	= 1000,
	.batt_div	= 414,
	.temp_mult	= 1,
	.temp_div	= 1,
	.batt_tech	= POWER_SUPPLY_TECHNOLOGY_LIPO,
	.batt_name	= "main-batt",
};

static struct wm97xx_pdata palmte2_wm97xx_pdata = {
	.batt_pdata	= &palmte2_batt_pdata,
};

static pxa2xx_audio_ops_t palmte2_ac97_pdata = {
	.codec_pdata	= { &palmte2_wm97xx_pdata, },
};

static struct palm27x_asoc_info palmte2_asoc_pdata = {
	.jack_gpio	= GPIO_NR_PALMTE2_EARPHONE_DETECT,
};

static struct platform_device palmte2_asoc = {
	.name = "palm27x-asoc",
	.id   = -1,
	.dev  = {
		.platform_data = &palmte2_asoc_pdata,
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
	&power_supply,
	&palmte2_asoc,
	&palmte2_gpio_vbus,
};

/* setup udc GPIOs initial state */
static void __init palmte2_udc_init(void)
{
	if (!gpio_request(GPIO_NR_PALMTE2_USB_PULLUP, "UDC Vbus")) {
		gpio_direction_output(GPIO_NR_PALMTE2_USB_PULLUP, 1);
		gpio_free(GPIO_NR_PALMTE2_USB_PULLUP);
	}
}

static void __init palmte2_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmte2_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	pxa_set_fb_info(NULL, &palmte2_lcd_screen);
	gpiod_add_lookup_table(&palmte2_mci_gpio_table);
	pxa_set_mci_info(&palmte2_mci_platform_data);
	palmte2_udc_init();
	pxa_set_ac97_info(&palmte2_ac97_pdata);
	pxa_set_ficp_info(&palmte2_ficp_platform_data);

	pwm_add_table(palmte2_pwm_lookup, ARRAY_SIZE(palmte2_pwm_lookup));
	gpiod_add_lookup_table(&palmte2_udc_gpiod_table);
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(PALMTE2, "Palm Tungsten|E2")
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= palmte2_init,
	.restart	= pxa_restart,
MACHINE_END
