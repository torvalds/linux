// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for HTC Magician PDA phones:
 * i-mate JAM, O2 Xda mini, Orange SPV M500, Qtek s100, Qtek s110
 * and T-Mobile MDA Compact.
 *
 * Copyright (c) 2006-2007 Philipp Zabel
 *
 * Based on hx4700.c, spitz.c and others.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mfd/htc-pasic3.h>
#include <linux/mtd/physmap.h>
#include <linux/pda_power.h>
#include <linux/platform_data/gpio-htc-egpio.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/platform_data/i2c-pxa.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/system_info.h>

#include "pxa27x.h"
#include <mach/magician.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>

#include <linux/regulator/max1586.h>

#include <linux/platform_data/pxa2xx_udc.h>

#include "udc.h"
#include "pxa27x-udc.h"
#include "devices.h"
#include "generic.h"

#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/spi/ads7846.h>

static unsigned long magician_pin_config[] __initdata = {

	/* SDRAM and Static Memory I/O Signals */
	GPIO20_nSDCS_2,
	GPIO21_nSDCS_3,
	GPIO15_nCS_1,
	GPIO78_nCS_2,	/* PASIC3 */
	GPIO79_nCS_3,	/* EGPIO CPLD */
	GPIO80_nCS_4,
	GPIO33_nCS_5,

	/* I2C UDA1380 + OV9640 */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* PWM 0 - LCD backlight */
	GPIO16_PWM0_OUT,

	/* I2S UDA1380 capture */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO31_I2S_SYNC,
	GPIO113_I2S_SYSCLK,

	/* SSP 1 UDA1380 playback */
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO25_SSP1_TXD,

	/* SSP 2 TSC2046 touchscreen */
	GPIO19_SSP2_SCLK,
	MFP_CFG_OUT(GPIO14, AF0, DRIVE_HIGH),	/* frame as GPIO */
	GPIO89_SSP2_TXD,
	GPIO88_SSP2_RXD,

	/* MMC/SD/SDHC slot */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* QCI camera interface */
	GPIO12_CIF_DD_7,
	GPIO17_CIF_DD_6,
	GPIO50_CIF_DD_3,
	GPIO51_CIF_DD_2,
	GPIO52_CIF_DD_4,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO55_CIF_DD_1,
	GPIO81_CIF_DD_0,
	GPIO82_CIF_DD_5,
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,

	/* Magician specific input GPIOs */
	GPIO9_GPIO,	/* unknown */
	GPIO10_GPIO,	/* GSM_IRQ */
	GPIO13_GPIO,	/* CPLD_IRQ */
	GPIO107_GPIO,	/* DS1WM_IRQ */
	GPIO108_GPIO,	/* GSM_READY */
	GPIO115_GPIO,	/* nPEN_IRQ */
};

/*
 * IrDA
 */

static struct pxaficp_platform_data magician_ficp_info = {
	.gpio_pwdown		= GPIO83_MAGICIAN_nIR_EN,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
	.gpio_pwdown_inverted	= 0,
};

/*
 * GPIO Keys
 */

#define INIT_KEY(_code, _gpio, _desc)	\
	{				\
		.code	= KEY_##_code,	\
		.gpio	= _gpio,	\
		.desc	= _desc,	\
		.type	= EV_KEY,	\
		.wakeup	= 1,		\
	}

static struct gpio_keys_button magician_button_table[] = {
	INIT_KEY(POWER,      GPIO0_MAGICIAN_KEY_POWER,      "Power button"),
	INIT_KEY(ESC,        GPIO37_MAGICIAN_KEY_HANGUP,    "Hangup button"),
	INIT_KEY(F10,        GPIO38_MAGICIAN_KEY_CONTACTS,  "Contacts button"),
	INIT_KEY(CALENDAR,   GPIO90_MAGICIAN_KEY_CALENDAR,  "Calendar button"),
	INIT_KEY(CAMERA,     GPIO91_MAGICIAN_KEY_CAMERA,    "Camera button"),
	INIT_KEY(UP,         GPIO93_MAGICIAN_KEY_UP,        "Up button"),
	INIT_KEY(DOWN,       GPIO94_MAGICIAN_KEY_DOWN,      "Down button"),
	INIT_KEY(LEFT,       GPIO95_MAGICIAN_KEY_LEFT,      "Left button"),
	INIT_KEY(RIGHT,      GPIO96_MAGICIAN_KEY_RIGHT,     "Right button"),
	INIT_KEY(KPENTER,    GPIO97_MAGICIAN_KEY_ENTER,     "Action button"),
	INIT_KEY(RECORD,     GPIO98_MAGICIAN_KEY_RECORD,    "Record button"),
	INIT_KEY(VOLUMEUP,   GPIO100_MAGICIAN_KEY_VOL_UP,   "Volume up"),
	INIT_KEY(VOLUMEDOWN, GPIO101_MAGICIAN_KEY_VOL_DOWN, "Volume down"),
	INIT_KEY(PHONE,      GPIO102_MAGICIAN_KEY_PHONE,    "Phone button"),
	INIT_KEY(PLAY,       GPIO99_MAGICIAN_HEADPHONE_IN,  "Headset button"),
};

static struct gpio_keys_platform_data gpio_keys_data = {
	.buttons	= magician_button_table,
	.nbuttons	= ARRAY_SIZE(magician_button_table),
};

static struct platform_device gpio_keys = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data = &gpio_keys_data,
	},
	.id	= -1,
};

/*
 * EGPIO (Xilinx CPLD)
 *
 * 32-bit aligned 8-bit registers
 * 16 possible registers (reg windows size), only 7 used:
 * 3x output, 1x irq, 3x input
 */

static struct resource egpio_resources[] = {
	[0] = {
		.start	= PXA_CS3_PHYS,
		.end	= PXA_CS3_PHYS + 0x20 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_GPIO_TO_IRQ(GPIO13_MAGICIAN_CPLD_IRQ),
		.end	= PXA_GPIO_TO_IRQ(GPIO13_MAGICIAN_CPLD_IRQ),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct htc_egpio_chip egpio_chips[] = {
	[0] = {
		.reg_start	= 0,
		.gpio_base	= MAGICIAN_EGPIO(0, 0),
		.num_gpios	= 24,
		.direction	= HTC_EGPIO_OUTPUT,
		/*
		 * Depends on modules configuration
		 */
		.initial_values	= 0x40, /* EGPIO_MAGICIAN_GSM_RESET */
	},
	[1] = {
		.reg_start	= 4,
		.gpio_base	= MAGICIAN_EGPIO(4, 0),
		.num_gpios	= 24,
		.direction	= HTC_EGPIO_INPUT,
	},
};

static struct htc_egpio_platform_data egpio_info = {
	.reg_width	= 8,
	.bus_width	= 32,
	.irq_base	= IRQ_BOARD_START,
	.num_irqs	= 4,
	.ack_register	= 3,
	.chip		= egpio_chips,
	.num_chips	= ARRAY_SIZE(egpio_chips),
};

static struct platform_device egpio = {
	.name		= "htc-egpio",
	.id		= -1,
	.resource	= egpio_resources,
	.num_resources	= ARRAY_SIZE(egpio_resources),
	.dev = {
		.platform_data = &egpio_info,
	},
};

/*
 * PXAFB LCD - Toppoly TD028STEB1 or Samsung LTP280QV
 */

static struct pxafb_mode_info toppoly_modes[] = {
	{
		.pixclock	= 96153,
		.bpp		= 16,
		.xres		= 240,
		.yres		= 320,
		.hsync_len	= 11,
		.vsync_len	= 3,
		.left_margin	= 19,
		.upper_margin	= 2,
		.right_margin	= 10,
		.lower_margin	= 2,
		.sync		= 0,
	},
};

static struct pxafb_mode_info samsung_modes[] = {
	{
		.pixclock	= 226469,
		.bpp		= 16,
		.xres		= 240,
		.yres		= 320,
		.hsync_len	= 8,
		.vsync_len	= 4,
		.left_margin	= 9,
		.upper_margin	= 4,
		.right_margin	= 9,
		.lower_margin	= 4,
		.sync	= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},
};

static void toppoly_lcd_power(int on, struct fb_var_screeninfo *si)
{
	pr_debug("Toppoly LCD power: %s\n", on ? "on" : "off");

	if (on) {
		gpio_set_value(EGPIO_MAGICIAN_TOPPOLY_POWER, 1);
		gpio_set_value(GPIO106_MAGICIAN_LCD_DCDC_NRESET, 1);
		udelay(2000);
		gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 1);
		udelay(2000);
		/* FIXME: enable LCDC here */
		udelay(2000);
		gpio_set_value(GPIO104_MAGICIAN_LCD_VOFF_EN, 1);
		udelay(2000);
		gpio_set_value(GPIO105_MAGICIAN_LCD_VON_EN, 1);
	} else {
		msleep(15);
		gpio_set_value(GPIO105_MAGICIAN_LCD_VON_EN, 0);
		udelay(500);
		gpio_set_value(GPIO104_MAGICIAN_LCD_VOFF_EN, 0);
		udelay(1000);
		gpio_set_value(GPIO106_MAGICIAN_LCD_DCDC_NRESET, 0);
		gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 0);
	}
}

static void samsung_lcd_power(int on, struct fb_var_screeninfo *si)
{
	pr_debug("Samsung LCD power: %s\n", on ? "on" : "off");

	if (on) {
		if (system_rev < 3)
			gpio_set_value(GPIO75_MAGICIAN_SAMSUNG_POWER, 1);
		else
			gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 1);
		mdelay(6);
		gpio_set_value(GPIO106_MAGICIAN_LCD_DCDC_NRESET, 1);
		mdelay(6);	/* Avdd -> Voff >5ms */
		gpio_set_value(GPIO104_MAGICIAN_LCD_VOFF_EN, 1);
		mdelay(16);	/* Voff -> Von >(5+10)ms */
		gpio_set_value(GPIO105_MAGICIAN_LCD_VON_EN, 1);
	} else {
		gpio_set_value(GPIO105_MAGICIAN_LCD_VON_EN, 0);
		mdelay(16);
		gpio_set_value(GPIO104_MAGICIAN_LCD_VOFF_EN, 0);
		mdelay(6);
		gpio_set_value(GPIO106_MAGICIAN_LCD_DCDC_NRESET, 0);
		mdelay(6);
		if (system_rev < 3)
			gpio_set_value(GPIO75_MAGICIAN_SAMSUNG_POWER, 0);
		else
			gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 0);
	}
}

static struct pxafb_mach_info toppoly_info = {
	.modes			= toppoly_modes,
	.num_modes		= 1,
	.fixed_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP,
	.pxafb_lcd_power	= toppoly_lcd_power,
};

static struct pxafb_mach_info samsung_info = {
	.modes			= samsung_modes,
	.num_modes		= 1,
	.fixed_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
		LCD_ALTERNATE_MAPPING,
	.pxafb_lcd_power	= samsung_lcd_power,
};

/*
 * Backlight
 */

static struct pwm_lookup magician_pwm_lookup[] = {
	PWM_LOOKUP("pxa27x-pwm.0", 0, "pwm-backlight", NULL, 30923,
		   PWM_POLARITY_NORMAL),
};

 /*
 * fixed regulator for pwm_backlight
 */

static struct regulator_consumer_supply pwm_backlight_supply[] = {
	REGULATOR_SUPPLY("power", "pwm_backlight"),
};


static struct gpio magician_bl_gpios[] = {
	{ EGPIO_MAGICIAN_BL_POWER,	GPIOF_DIR_OUT, "Backlight power" },
	{ EGPIO_MAGICIAN_BL_POWER2,	GPIOF_DIR_OUT, "Backlight power 2" },
};

static int magician_backlight_init(struct device *dev)
{
	return gpio_request_array(ARRAY_AND_SIZE(magician_bl_gpios));
}

static int magician_backlight_notify(struct device *dev, int brightness)
{
	pr_debug("Brightness = %i\n", brightness);
	gpio_set_value(EGPIO_MAGICIAN_BL_POWER, brightness);
	if (brightness >= 200) {
		gpio_set_value(EGPIO_MAGICIAN_BL_POWER2, 1);
		return brightness - 72;
	} else {
		gpio_set_value(EGPIO_MAGICIAN_BL_POWER2, 0);
		return brightness;
	}
}

static void magician_backlight_exit(struct device *dev)
{
	gpio_free_array(ARRAY_AND_SIZE(magician_bl_gpios));
}

/*
 * LCD PWM backlight (main)
 *
 * MP1521 frequency should be:
 *	100-400 Hz = 2 .5*10^6 - 10 *10^6 ns
 */

static struct platform_pwm_backlight_data backlight_data = {
	.max_brightness	= 272,
	.dft_brightness	= 100,
	.enable_gpio	= -1,
	.init		= magician_backlight_init,
	.notify		= magician_backlight_notify,
	.exit		= magician_backlight_exit,
};

static struct platform_device backlight = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.parent		= &pxa27x_device_pwm0.dev,
		.platform_data	= &backlight_data,
	},
};

/*
 * GPIO LEDs, Phone keys backlight, vibra
 */

static struct gpio_led gpio_leds[] = {
	{
		.name = "magician::vibra",
		.default_trigger = "none",
		.gpio = GPIO22_MAGICIAN_VIBRA_EN,
	},
	{
		.name = "magician::phone_bl",
		.default_trigger = "backlight",
		.gpio = GPIO103_MAGICIAN_LED_KP,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds = gpio_leds,
	.num_leds = ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &gpio_led_info,
	},
};

/*
 * PASIC3 with DS1WM
 */

static struct resource pasic3_resources[] = {
	[0] = {
		.start	= PXA_CS2_PHYS,
		.end	= PXA_CS2_PHYS + 0x1b,
		.flags	= IORESOURCE_MEM,
	},
	/* No IRQ handler in the PASIC3, DS1WM needs an external IRQ */
	[1] = {
		.start	= PXA_GPIO_TO_IRQ(GPIO107_MAGICIAN_DS1WM_IRQ),
		.end	= PXA_GPIO_TO_IRQ(GPIO107_MAGICIAN_DS1WM_IRQ),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct pasic3_platform_data pasic3_platform_data = {
	.clock_rate = 4000000,
};

static struct platform_device pasic3 = {
	.name		= "pasic3",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pasic3_resources),
	.resource	= pasic3_resources,
	.dev = {
		.platform_data = &pasic3_platform_data,
	},
};

/*
 * PXA UDC
 */

static void magician_udc_command(int cmd)
{
	if (cmd == PXA2XX_UDC_CMD_CONNECT)
		UP2OCR |= UP2OCR_DPPUE | UP2OCR_DPPUBE;
	else if (cmd == PXA2XX_UDC_CMD_DISCONNECT)
		UP2OCR &= ~(UP2OCR_DPPUE | UP2OCR_DPPUBE);
}

static struct pxa2xx_udc_mach_info magician_udc_info __initdata = {
	.udc_command	= magician_udc_command,
	.gpio_pullup	= GPIO27_MAGICIAN_USBC_PUEN,
};

/*
 * USB device VBus detection
 */

static struct resource gpio_vbus_resource = {
	.flags	= IORESOURCE_IRQ,
	.start	= IRQ_MAGICIAN_VBUS,
	.end	= IRQ_MAGICIAN_VBUS,
};

static struct gpio_vbus_mach_info gpio_vbus_info = {
	.gpio_pullup	= GPIO27_MAGICIAN_USBC_PUEN,
	.gpio_vbus	= EGPIO_MAGICIAN_CABLE_VBUS,
};

static struct platform_device gpio_vbus = {
	.name		= "gpio-vbus",
	.id		= -1,
	.num_resources	= 1,
	.resource	= &gpio_vbus_resource,
	.dev = {
		.platform_data = &gpio_vbus_info,
	},
};

/*
 * External power
 */

static int magician_supply_init(struct device *dev)
{
	int ret = -1;

	ret = gpio_request(EGPIO_MAGICIAN_CABLE_TYPE, "Cable is AC charger");
	if (ret) {
		pr_err("Cannot request AC/USB charger GPIO (%i)\n", ret);
		goto err_ac;
	}

	ret = gpio_request(EGPIO_MAGICIAN_CABLE_INSERTED, "Cable inserted");
	if (ret) {
		pr_err("Cannot request cable detection GPIO (%i)\n", ret);
		goto err_usb;
	}

	return 0;

err_usb:
	gpio_free(EGPIO_MAGICIAN_CABLE_TYPE);
err_ac:
	return ret;
}

static void magician_set_charge(int flags)
{
	if (flags & PDA_POWER_CHARGE_AC) {
		pr_debug("Charging from AC\n");
		gpio_set_value(EGPIO_MAGICIAN_NICD_CHARGE, 1);
	} else if (flags & PDA_POWER_CHARGE_USB) {
		pr_debug("Charging from USB\n");
		gpio_set_value(EGPIO_MAGICIAN_NICD_CHARGE, 1);
	} else {
		pr_debug("Charging disabled\n");
		gpio_set_value(EGPIO_MAGICIAN_NICD_CHARGE, 0);
	}
}

static int magician_is_ac_online(void)
{
	return gpio_get_value(EGPIO_MAGICIAN_CABLE_INSERTED) &&
		gpio_get_value(EGPIO_MAGICIAN_CABLE_TYPE); /* AC=1 */
}

static int magician_is_usb_online(void)
{
	return gpio_get_value(EGPIO_MAGICIAN_CABLE_INSERTED) &&
		(!gpio_get_value(EGPIO_MAGICIAN_CABLE_TYPE)); /* USB=0 */
}

static void magician_supply_exit(struct device *dev)
{
	gpio_free(EGPIO_MAGICIAN_CABLE_INSERTED);
	gpio_free(EGPIO_MAGICIAN_CABLE_TYPE);
}

static char *magician_supplicants[] = {
	"ds2760-battery.0", "backup-battery"
};

static struct pda_power_pdata power_supply_info = {
	.init			= magician_supply_init,
	.exit			= magician_supply_exit,
	.is_ac_online		= magician_is_ac_online,
	.is_usb_online		= magician_is_usb_online,
	.set_charge		= magician_set_charge,
	.supplied_to		= magician_supplicants,
	.num_supplicants	= ARRAY_SIZE(magician_supplicants),
};

static struct resource power_supply_resources[] = {
	[0] = {
		.name	= "ac",
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
			IORESOURCE_IRQ_LOWEDGE,
		.start	= IRQ_MAGICIAN_VBUS,
		.end	= IRQ_MAGICIAN_VBUS,
	},
	[1] = {
		.name	= "usb",
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
			IORESOURCE_IRQ_LOWEDGE,
		.start	= IRQ_MAGICIAN_VBUS,
		.end	= IRQ_MAGICIAN_VBUS,
	},
};

static struct platform_device power_supply = {
	.name	= "pda-power",
	.id	= -1,
	.dev = {
		.platform_data = &power_supply_info,
	},
	.resource	= power_supply_resources,
	.num_resources	= ARRAY_SIZE(power_supply_resources),
};

/*
 * Battery charger
 */

static struct regulator_consumer_supply bq24022_consumers[] = {
	REGULATOR_SUPPLY("vbus_draw", NULL),
	REGULATOR_SUPPLY("ac_draw", NULL),
};

static struct regulator_init_data bq24022_init_data = {
	.constraints = {
		.max_uA		= 500000,
		.valid_ops_mask	= REGULATOR_CHANGE_CURRENT |
			REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(bq24022_consumers),
	.consumer_supplies	= bq24022_consumers,
};


static enum gpiod_flags bq24022_gpiod_gflags[] = { GPIOD_OUT_LOW };

static struct gpio_regulator_state bq24022_states[] = {
	{ .value = 100000, .gpios = (0 << 0) },
	{ .value = 500000, .gpios = (1 << 0) },
};

static struct gpio_regulator_config bq24022_info = {
	.supply_name		= "bq24022",

	.enabled_at_boot	= 1,

	.gflags = bq24022_gpiod_gflags,
	.ngpios = ARRAY_SIZE(bq24022_gpiod_gflags),

	.states			= bq24022_states,
	.nr_states		= ARRAY_SIZE(bq24022_states),

	.type			= REGULATOR_CURRENT,
	.init_data		= &bq24022_init_data,
};

static struct platform_device bq24022 = {
	.name	= "gpio-regulator",
	.id	= -1,
	.dev	= {
		.platform_data = &bq24022_info,
	},
};

static struct gpiod_lookup_table bq24022_gpiod_table = {
	.dev_id = "gpio-regulator",
	.table = {
		GPIO_LOOKUP("gpio-pxa", EGPIO_MAGICIAN_BQ24022_ISET2,
			    NULL, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-pxa", GPIO30_MAGICIAN_BQ24022_nCHARGE_EN,
			    "enable", GPIO_ACTIVE_LOW),
		{ },
	},
};

/*
 * fixed regulator for ads7846
 */

static struct regulator_consumer_supply ads7846_supply =
	REGULATOR_SUPPLY("vcc", "spi2.0");

static struct regulator_init_data vads7846_regulator = {
	.constraints	= {
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &ads7846_supply,
};

static struct fixed_voltage_config vads7846 = {
	.supply_name	= "vads7846",
	.microvolts	= 3300000, /* probably */
	.startup_delay	= 0,
	.init_data	= &vads7846_regulator,
};

static struct platform_device vads7846_device = {
	.name	= "reg-fixed-voltage",
	.id	= -1,
	.dev	= {
		.platform_data	= &vads7846,
	},
};

/*
 * Vcore regulator MAX1587A
 */

static struct regulator_consumer_supply magician_max1587a_consumers[] = {
	REGULATOR_SUPPLY("vcc_core", NULL),
};

static struct regulator_init_data magician_max1587a_v3_info = {
	.constraints = {
		.name		= "vcc_core range",
		.min_uV		= 700000,
		.max_uV		= 1475000,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
	},
	.consumer_supplies	= magician_max1587a_consumers,
	.num_consumer_supplies	= ARRAY_SIZE(magician_max1587a_consumers),
};

static struct max1586_subdev_data magician_max1587a_subdevs[] = {
	{
		.name		= "vcc_core",
		.id		= MAX1586_V3,
		.platform_data	= &magician_max1587a_v3_info,
	}
};

static struct max1586_platform_data magician_max1587a_info = {
	.subdevs     = magician_max1587a_subdevs,
	.num_subdevs = ARRAY_SIZE(magician_max1587a_subdevs),
	/*
	 * NOTICE measured directly on the PCB (board_id == 0x3a), but
	 * if R24 is present, it will boost the voltage
	 * (write 1.475V, get 1.645V and smoke)
	 */
	.v3_gain     = MAX1586_GAIN_NO_R24,
};

static struct i2c_board_info magician_pwr_i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data	= &magician_max1587a_info,
	},
};

/*
 * MMC/SD
 */

static int magician_mci_init(struct device *dev,
	irq_handler_t detect_irq, void *data)
{
	return request_irq(IRQ_MAGICIAN_SD, detect_irq, 0,
		"mmc card detect", data);
}

static void magician_mci_exit(struct device *dev, void *data)
{
	free_irq(IRQ_MAGICIAN_SD, data);
}

static struct pxamci_platform_data magician_mci_info = {
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.init			= magician_mci_init,
	.exit			= magician_mci_exit,
	.gpio_card_ro_invert	= 1,
};

/*
 * Write protect on EGPIO register 5 index 4, this is on the second HTC
 * EGPIO chip which starts at register 4, so we need offset 8+4=12 on that
 * particular chip.
 */
#define EGPIO_MAGICIAN_nSD_READONLY_OFFSET 12
/*
 * Power on EGPIO register 2 index 0, so this is on the first HTC EGPIO chip
 * starting at register 0 so we need offset 2*8+0 = 16 on that chip.
 */
#define EGPIO_MAGICIAN_nSD_POWER_OFFSET 16

static struct gpiod_lookup_table magician_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("htc-egpio-1", EGPIO_MAGICIAN_nSD_READONLY_OFFSET,
			    "wp", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("htc-egpio-0", EGPIO_MAGICIAN_nSD_POWER_OFFSET,
			    "power", GPIO_ACTIVE_HIGH),
		{ },
	},
};

/*
 * USB OHCI
 */

static struct pxaohci_platform_data magician_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	/* port1: CSR Bluetooth, port2: OTG with UDC */
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 | POWER_CONTROL_LOW,
	.power_budget	= 0,
	.power_on_delay = 100,
};

/*
 * StrataFlash
 */

static int magician_flash_init(struct platform_device *pdev)
{
	int ret = gpio_request(EGPIO_MAGICIAN_FLASH_VPP, "flash Vpp enable");

	if (ret) {
		pr_err("Cannot request flash enable GPIO (%i)\n", ret);
		return ret;
	}

	ret = gpio_direction_output(EGPIO_MAGICIAN_FLASH_VPP, 1);
	if (ret) {
		pr_err("Cannot set direction for flash enable (%i)\n", ret);
		gpio_free(EGPIO_MAGICIAN_FLASH_VPP);
	}

	return ret;
}

static void magician_set_vpp(struct platform_device *pdev, int vpp)
{
	gpio_set_value(EGPIO_MAGICIAN_FLASH_VPP, vpp);
}

static void magician_flash_exit(struct platform_device *pdev)
{
	gpio_free(EGPIO_MAGICIAN_FLASH_VPP);
}

static struct resource strataflash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct mtd_partition magician_flash_parts[] = {
	{
		.name		= "Bootloader",
		.offset		= 0x0,
		.size		= 0x40000,
		.mask_flags	= MTD_WRITEABLE, /* EXPERIMENTAL */
	},
	{
		.name		= "Linux Kernel",
		.offset		= 0x40000,
		.size		= MTDPART_SIZ_FULL,
	},
};

/*
 * physmap-flash driver
 */

static struct physmap_flash_data strataflash_data = {
	.width		= 4,
	.init		= magician_flash_init,
	.set_vpp	= magician_set_vpp,
	.exit		= magician_flash_exit,
	.parts		= magician_flash_parts,
	.nr_parts	= ARRAY_SIZE(magician_flash_parts),
};

static struct platform_device strataflash = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= &strataflash_resource,
	.num_resources	= 1,
	.dev = {
		.platform_data = &strataflash_data,
	},
};

/*
 * PXA I2C main controller
 */

static struct i2c_pxa_platform_data i2c_info = {
	/* OV9640 I2C device doesn't support fast mode */
	.fast_mode	= 0,
};

/*
 * PXA I2C power controller
 */

static struct i2c_pxa_platform_data magician_i2c_power_info = {
	.fast_mode	= 1,
};

/*
 * Touchscreen
 */

static struct ads7846_platform_data ads7846_pdata = {
	.model		= 7846,
	.x_plate_ohms	= 317,
	.y_plate_ohms	= 500,
	.pressure_max	= 1023,	/* with x plate ohms it will overflow 255 */
	.debounce_max	= 3,	/* first readout is always bad */
	.debounce_tol	= 30,
	.debounce_rep	= 0,
	.gpio_pendown	= GPIO115_MAGICIAN_nPEN_IRQ,
	.keep_vref_on	= 1,
	.wakeup		= true,
	.vref_delay_usecs		= 100,
	.penirq_recheck_delay_usecs	= 100,
};

struct pxa2xx_spi_chip tsc2046_chip_info = {
	.tx_threshold	= 1,
	.rx_threshold	= 2,
	.timeout	= 64,
	/* NOTICE must be GPIO, incompatibility with hw PXA SPI framing */
	.gpio_cs	= GPIO14_MAGICIAN_TSC2046_CS,
};

static struct pxa2xx_spi_controller magician_spi_info = {
	.num_chipselect	= 1,
	.enable_dma	= 1,
};

static struct spi_board_info ads7846_spi_board_info[] __initdata = {
	{
		.modalias		= "ads7846",
		.bus_num		= 2,
		.max_speed_hz		= 2500000,
		.platform_data		= &ads7846_pdata,
		.controller_data	= &tsc2046_chip_info,
		.irq = PXA_GPIO_TO_IRQ(GPIO115_MAGICIAN_nPEN_IRQ),
	},
};

/*
 * Platform devices
 */

static struct platform_device *devices[] __initdata = {
	&gpio_keys,
	&egpio,
	&backlight,
	&pasic3,
	&bq24022,
	&gpio_vbus,
	&power_supply,
	&strataflash,
	&leds_gpio,
	&vads7846_device,
};

static struct gpio magician_global_gpios[] = {
	{ GPIO13_MAGICIAN_CPLD_IRQ, GPIOF_IN, "CPLD_IRQ" },
	{ GPIO107_MAGICIAN_DS1WM_IRQ, GPIOF_IN, "DS1WM_IRQ" },

	/* NOTICE valid LCD init sequence */
	{ GPIO106_MAGICIAN_LCD_DCDC_NRESET, GPIOF_OUT_INIT_LOW, "LCD DCDC nreset" },
	{ GPIO104_MAGICIAN_LCD_VOFF_EN, GPIOF_OUT_INIT_LOW, "LCD VOFF enable" },
	{ GPIO105_MAGICIAN_LCD_VON_EN, GPIOF_OUT_INIT_LOW, "LCD VON enable" },
};

static void __init magician_init(void)
{
	void __iomem *cpld;
	int lcd_select;
	int err;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(magician_pin_config));
	err = gpio_request_array(ARRAY_AND_SIZE(magician_global_gpios));
	if (err)
		pr_err("magician: Failed to request global GPIOs: %d\n", err);

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);

	pwm_add_table(magician_pwm_lookup, ARRAY_SIZE(magician_pwm_lookup));

	pxa_set_ficp_info(&magician_ficp_info);
	pxa27x_set_i2c_power_info(&magician_i2c_power_info);
	pxa_set_i2c_info(&i2c_info);

	i2c_register_board_info(1,
		ARRAY_AND_SIZE(magician_pwr_i2c_board_info));

	gpiod_add_lookup_table(&magician_mci_gpio_table);
	pxa_set_mci_info(&magician_mci_info);
	pxa_set_ohci_info(&magician_ohci_info);
	pxa_set_udc_info(&magician_udc_info);

	/* Check LCD type we have */
	cpld = ioremap_nocache(PXA_CS3_PHYS, 0x1000);
	if (cpld) {
		u8 board_id = __raw_readb(cpld + 0x14);

		iounmap(cpld);
		system_rev = board_id & 0x7;
		lcd_select = board_id & 0x8;
		pr_info("LCD type: %s\n", lcd_select ? "Samsung" : "Toppoly");
		if (lcd_select && (system_rev < 3))
			/* NOTICE valid LCD init sequence */
			gpio_request_one(GPIO75_MAGICIAN_SAMSUNG_POWER,
				GPIOF_OUT_INIT_LOW, "Samsung LCD Power");
		pxa_set_fb_info(NULL,
			lcd_select ? &samsung_info : &toppoly_info);
	} else
		pr_err("LCD detection: CPLD mapping failed\n");

	pxa2xx_set_spi_info(2, &magician_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(ads7846_spi_board_info));

	regulator_register_always_on(0, "power", pwm_backlight_supply,
		ARRAY_SIZE(pwm_backlight_supply), 5000000);

	gpiod_add_lookup_table(&bq24022_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(devices));
}

MACHINE_START(MAGICIAN, "HTC Magician")
	.atag_offset	= 0x100,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= MAGICIAN_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_machine	= magician_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
