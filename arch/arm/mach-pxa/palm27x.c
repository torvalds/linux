/*
 * Common code for Palm LD, T5, TX, Z72
 *
 * Copyright (C) 2010-2011 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/wm97xx.h>
#include <linux/power_supply.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/regulator/max1586.h>
#include <linux/i2c/pxa-i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/pxa27x.h>
#include <mach/audio.h>
#include <mach/mmc.h>
#include <mach/pxafb.h>
#include <mach/irda.h>
#include <mach/udc.h>
#include <mach/palmasoc.h>
#include <mach/palm27x.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data palm27x_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.detect_delay_ms	= 200,
};

void __init palm27x_mmc_init(int detect, int ro, int power,
					int power_inverted)
{
	palm27x_mci_platform_data.gpio_card_detect	= detect;
	palm27x_mci_platform_data.gpio_card_ro		= ro;
	palm27x_mci_platform_data.gpio_power		= power;
	palm27x_mci_platform_data.gpio_power_invert	= power_inverted;

	pxa_set_mci_info(&palm27x_mci_platform_data);
}
#endif

/******************************************************************************
 * Power management - standby
 ******************************************************************************/
#if defined(CONFIG_SUSPEND)
void __init palm27x_pm_init(unsigned long str_base)
{
	static const unsigned long resume[] = {
		0xe3a00101,	/* mov	r0,	#0x40000000 */
		0xe380060f,	/* orr	r0, r0, #0x00f00000 */
		0xe590f008,	/* ldr	pc, [r0, #0x08] */
	};

	/*
	 * Copy the bootloader.
	 * NOTE: PalmZ72 uses a different wakeup method!
	 */
	memcpy(phys_to_virt(str_base), resume, sizeof(resume));
}
#endif

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
struct pxafb_mode_info palm_320x480_lcd_mode = {
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
};

struct pxafb_mode_info palm_320x320_lcd_mode = {
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
};

struct pxafb_mode_info palm_320x320_new_lcd_mode = {
	.pixclock	= 86538,
	.xres		= 320,
	.yres		= 320,
	.bpp		= 16,

	.left_margin	= 20,
	.right_margin	= 8,
	.upper_margin	= 8,
	.lower_margin	= 5,

	.hsync_len	= 4,
	.vsync_len	= 1,
};

static struct pxafb_mach_info palm27x_lcd_screen = {
	.num_modes	= 1,
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

static int palm27x_lcd_power;
static void palm27x_lcd_ctl(int on, struct fb_var_screeninfo *info)
{
	gpio_set_value(palm27x_lcd_power, on);
}

void __init palm27x_lcd_init(int power, struct pxafb_mode_info *mode)
{
	palm27x_lcd_screen.modes = mode;

	if (gpio_is_valid(power)) {
		if (!gpio_request(power, "LCD power")) {
			pr_err("Palm27x: failed to claim lcd power gpio!\n");
			return;
		}
		if (!gpio_direction_output(power, 1)) {
			pr_err("Palm27x: lcd power configuration failed!\n");
			return;
		}
		palm27x_lcd_power = power;
		palm27x_lcd_screen.pxafb_lcd_power = palm27x_lcd_ctl;
	}

	pxa_set_fb_info(NULL, &palm27x_lcd_screen);
}
#endif

/******************************************************************************
 * USB Gadget
 ******************************************************************************/
#if	defined(CONFIG_USB_PXA27X) || \
	defined(CONFIG_USB_PXA27X_MODULE)
static struct gpio_vbus_mach_info palm27x_udc_info = {
	.gpio_vbus_inverted	= 1,
};

static struct platform_device palm27x_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
	.dev	= {
		.platform_data	= &palm27x_udc_info,
	},
};

void __init palm27x_udc_init(int vbus, int pullup, int vbus_inverted)
{
	palm27x_udc_info.gpio_vbus	= vbus;
	palm27x_udc_info.gpio_pullup	= pullup;

	palm27x_udc_info.gpio_vbus_inverted = vbus_inverted;

	if (!gpio_request(pullup, "USB Pullup")) {
		gpio_direction_output(pullup,
			palm27x_udc_info.gpio_vbus_inverted);
		gpio_free(pullup);
	} else
		return;

	platform_device_register(&palm27x_gpio_vbus);
}
#endif

/******************************************************************************
 * IrDA
 ******************************************************************************/
#if defined(CONFIG_IRDA) || defined(CONFIG_IRDA_MODULE)
static struct pxaficp_platform_data palm27x_ficp_platform_data = {
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

void __init palm27x_irda_init(int pwdn)
{
	palm27x_ficp_platform_data.gpio_pwdown = pwdn;
	pxa_set_ficp_info(&palm27x_ficp_platform_data);
}
#endif

/******************************************************************************
 * WM97xx audio, battery
 ******************************************************************************/
#if	defined(CONFIG_TOUCHSCREEN_WM97XX) || \
	defined(CONFIG_TOUCHSCREEN_WM97XX_MODULE)
static struct wm97xx_batt_pdata palm27x_batt_pdata = {
	.batt_aux	= WM97XX_AUX_ID3,
	.temp_aux	= WM97XX_AUX_ID2,
	.charge_gpio	= -1,
	.batt_mult	= 1000,
	.batt_div	= 414,
	.temp_mult	= 1,
	.temp_div	= 1,
	.batt_tech	= POWER_SUPPLY_TECHNOLOGY_LIPO,
	.batt_name	= "main-batt",
};

static struct wm97xx_pdata palm27x_wm97xx_pdata = {
	.batt_pdata	= &palm27x_batt_pdata,
};

static pxa2xx_audio_ops_t palm27x_ac97_pdata = {
	.codec_pdata	= { &palm27x_wm97xx_pdata, },
};

static struct palm27x_asoc_info palm27x_asoc_pdata = {
	.jack_gpio	= -1,
};

static struct platform_device palm27x_asoc = {
	.name = "palm27x-asoc",
	.id   = -1,
	.dev  = {
		.platform_data = &palm27x_asoc_pdata,
	},
};

void __init palm27x_ac97_init(int minv, int maxv, int jack, int reset)
{
	palm27x_ac97_pdata.reset_gpio	= reset;
	palm27x_asoc_pdata.jack_gpio	= jack;

	if (minv < 0 || maxv < 0) {
		palm27x_ac97_pdata.codec_pdata[0] = NULL;
		pxa_set_ac97_info(&palm27x_ac97_pdata);
	} else {
		palm27x_batt_pdata.min_voltage	= minv,
		palm27x_batt_pdata.max_voltage	= maxv,

		pxa_set_ac97_info(&palm27x_ac97_pdata);
		platform_device_register(&palm27x_asoc);
	}
}
#endif

/******************************************************************************
 * Backlight
 ******************************************************************************/
#if defined(CONFIG_BACKLIGHT_PWM) || defined(CONFIG_BACKLIGHT_PWM_MODULE)
static int palm_bl_power;
static int palm_lcd_power;

static int palm27x_backlight_init(struct device *dev)
{
	int ret;

	ret = gpio_request(palm_bl_power, "BL POWER");
	if (ret)
		goto err;
	ret = gpio_direction_output(palm_bl_power, 0);
	if (ret)
		goto err2;

	if (gpio_is_valid(palm_lcd_power)) {
		ret = gpio_request(palm_lcd_power, "LCD POWER");
		if (ret)
			goto err2;
		ret = gpio_direction_output(palm_lcd_power, 0);
		if (ret)
			goto err3;
	}

	return 0;
err3:
	gpio_free(palm_lcd_power);
err2:
	gpio_free(palm_bl_power);
err:
	return ret;
}

static int palm27x_backlight_notify(struct device *dev, int brightness)
{
	gpio_set_value(palm_bl_power, brightness);
	if (gpio_is_valid(palm_lcd_power))
		gpio_set_value(palm_lcd_power, brightness);
	return brightness;
}

static void palm27x_backlight_exit(struct device *dev)
{
	gpio_free(palm_bl_power);
	if (gpio_is_valid(palm_lcd_power))
		gpio_free(palm_lcd_power);
}

static struct platform_pwm_backlight_data palm27x_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 0xfe,
	.dft_brightness	= 0x7e,
	.pwm_period_ns	= 3500 * 1024,
	.init		= palm27x_backlight_init,
	.notify		= palm27x_backlight_notify,
	.exit		= palm27x_backlight_exit,
};

static struct platform_device palm27x_backlight = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa27x_device_pwm0.dev,
		.platform_data	= &palm27x_backlight_data,
	},
};

void __init palm27x_pwm_init(int bl, int lcd)
{
	palm_bl_power	= bl;
	palm_lcd_power	= lcd;
	platform_device_register(&palm27x_backlight);
}
#endif

/******************************************************************************
 * Power supply
 ******************************************************************************/
#if defined(CONFIG_PDA_POWER) || defined(CONFIG_PDA_POWER_MODULE)
static int palm_ac_state;
static int palm_usb_state;

static int palm27x_power_supply_init(struct device *dev)
{
	int ret;

	ret = gpio_request(palm_ac_state, "AC state");
	if (ret)
		goto err1;
	ret = gpio_direction_input(palm_ac_state);
	if (ret)
		goto err2;

	if (gpio_is_valid(palm_usb_state)) {
		ret = gpio_request(palm_usb_state, "USB state");
		if (ret)
			goto err2;
		ret = gpio_direction_input(palm_usb_state);
		if (ret)
			goto err3;
	}

	return 0;
err3:
	gpio_free(palm_usb_state);
err2:
	gpio_free(palm_ac_state);
err1:
	return ret;
}

static void palm27x_power_supply_exit(struct device *dev)
{
	gpio_free(palm_usb_state);
	gpio_free(palm_ac_state);
}

static int palm27x_is_ac_online(void)
{
	return gpio_get_value(palm_ac_state);
}

static int palm27x_is_usb_online(void)
{
	return !gpio_get_value(palm_usb_state);
}
static char *palm27x_supplicants[] = {
	"main-battery",
};

static struct pda_power_pdata palm27x_ps_info = {
	.init			= palm27x_power_supply_init,
	.exit			= palm27x_power_supply_exit,
	.is_ac_online		= palm27x_is_ac_online,
	.is_usb_online		= palm27x_is_usb_online,
	.supplied_to		= palm27x_supplicants,
	.num_supplicants	= ARRAY_SIZE(palm27x_supplicants),
};

static struct platform_device palm27x_power_supply = {
	.name = "pda-power",
	.id   = -1,
	.dev  = {
		.platform_data = &palm27x_ps_info,
	},
};

void __init palm27x_power_init(int ac, int usb)
{
	palm_ac_state	= ac;
	palm_usb_state	= usb;
	platform_device_register(&palm27x_power_supply);
}
#endif

/******************************************************************************
 * Core power regulator
 ******************************************************************************/
#if defined(CONFIG_REGULATOR_MAX1586) || \
    defined(CONFIG_REGULATOR_MAX1586_MODULE)
static struct regulator_consumer_supply palm27x_max1587a_consumers[] = {
	{
		.supply	= "vcc_core",
	}
};

static struct regulator_init_data palm27x_max1587a_v3_info = {
	.constraints = {
		.name		= "vcc_core range",
		.min_uV		= 900000,
		.max_uV		= 1705000,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
	},
	.consumer_supplies	= palm27x_max1587a_consumers,
	.num_consumer_supplies	= ARRAY_SIZE(palm27x_max1587a_consumers),
};

static struct max1586_subdev_data palm27x_max1587a_subdevs[] = {
	{
		.name		= "vcc_core",
		.id		= MAX1586_V3,
		.platform_data	= &palm27x_max1587a_v3_info,
	}
};

static struct max1586_platform_data palm27x_max1587a_info = {
	.subdevs     = palm27x_max1587a_subdevs,
	.num_subdevs = ARRAY_SIZE(palm27x_max1587a_subdevs),
	.v3_gain     = MAX1586_GAIN_R24_3k32, /* 730..1550 mV */
};

static struct i2c_board_info __initdata palm27x_pi2c_board_info[] = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data	= &palm27x_max1587a_info,
	},
};

static struct i2c_pxa_platform_data palm27x_i2c_power_info = {
	.use_pio	= 1,
};

void __init palm27x_pmic_init(void)
{
	i2c_register_board_info(1, ARRAY_AND_SIZE(palm27x_pi2c_board_info));
	pxa27x_set_i2c_power_info(&palm27x_i2c_power_info);
}
#endif
