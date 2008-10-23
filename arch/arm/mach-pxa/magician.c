/*
 * Support for HTC Magician PDA phones:
 * i-mate JAM, O2 Xda mini, Orange SPV M500, Qtek s100, Qtek s110
 * and T-Mobile MDA Compact.
 *
 * Copyright (c) 2006-2007 Philipp Zabel
 *
 * Based on hx4700.c, spitz.c and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mfd/htc-egpio.h>
#include <linux/mfd/htc-pasic3.h>
#include <linux/mtd/physmap.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/magician.h>
#include <mach/mfp-pxa27x.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h>
#include <mach/pxafb.h>
#include <mach/i2c.h>
#include <mach/mmc.h>
#include <mach/irda.h>
#include <mach/ohci.h>

#include "devices.h"
#include "generic.h"

static unsigned long magician_pin_config[] __initdata = {

	/* SDRAM and Static Memory I/O Signals */
	GPIO20_nSDCS_2,
	GPIO21_nSDCS_3,
	GPIO15_nCS_1,
	GPIO78_nCS_2,   /* PASIC3 */
	GPIO79_nCS_3,   /* EGPIO CPLD */
	GPIO80_nCS_4,
	GPIO33_nCS_5,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* PWM 0 */
	GPIO16_PWM0_OUT,

	/* I2S */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO31_I2S_SYNC,
	GPIO113_I2S_SYSCLK,

	/* SSP 2 */
	GPIO19_SSP2_SCLK,
	GPIO14_SSP2_SFRM,
	GPIO89_SSP2_TXD,
	GPIO88_SSP2_RXD,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,

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

	/* QCI */
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
 * IRDA
 */

static void magician_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(GPIO83_MAGICIAN_nIR_EN, mode & IR_OFF);
	pxa2xx_transceiver_mode(dev, mode);
}

static struct pxaficp_platform_data magician_ficp_info = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = magician_irda_transceiver_mode,
};

/*
 * GPIO Keys
 */

static struct gpio_keys_button magician_button_table[] = {
	{KEY_POWER,      GPIO0_MAGICIAN_KEY_POWER,      0, "Power button"},
	{KEY_ESC,        GPIO37_MAGICIAN_KEY_HANGUP,    0, "Hangup button"},
	{KEY_F10,        GPIO38_MAGICIAN_KEY_CONTACTS,  0, "Contacts button"},
	{KEY_CALENDAR,   GPIO90_MAGICIAN_KEY_CALENDAR,  0, "Calendar button"},
	{KEY_CAMERA,     GPIO91_MAGICIAN_KEY_CAMERA,    0, "Camera button"},
	{KEY_UP,         GPIO93_MAGICIAN_KEY_UP,        0, "Up button"},
	{KEY_DOWN,       GPIO94_MAGICIAN_KEY_DOWN,      0, "Down button"},
	{KEY_LEFT,       GPIO95_MAGICIAN_KEY_LEFT,      0, "Left button"},
	{KEY_RIGHT,      GPIO96_MAGICIAN_KEY_RIGHT,     0, "Right button"},
	{KEY_KPENTER,    GPIO97_MAGICIAN_KEY_ENTER,     0, "Action button"},
	{KEY_RECORD,     GPIO98_MAGICIAN_KEY_RECORD,    0, "Record button"},
	{KEY_VOLUMEUP,   GPIO100_MAGICIAN_KEY_VOL_UP,   0, "Volume up"},
	{KEY_VOLUMEDOWN, GPIO101_MAGICIAN_KEY_VOL_DOWN, 0, "Volume down"},
	{KEY_PHONE,      GPIO102_MAGICIAN_KEY_PHONE,    0, "Phone button"},
	{KEY_PLAY,       GPIO99_MAGICIAN_HEADPHONE_IN,  0, "Headset button"},
};

static struct gpio_keys_platform_data gpio_keys_data = {
	.buttons  = magician_button_table,
	.nbuttons = ARRAY_SIZE(magician_button_table),
};

static struct platform_device gpio_keys = {
	.name = "gpio-keys",
	.dev  = {
		.platform_data = &gpio_keys_data,
	},
	.id   = -1,
};


/*
 * EGPIO (Xilinx CPLD)
 *
 * 7 32-bit aligned 8-bit registers: 3x output, 1x irq, 3x input
 */

static struct resource egpio_resources[] = {
	[0] = {
		.start = PXA_CS3_PHYS,
		.end   = PXA_CS3_PHYS + 0x20,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gpio_to_irq(GPIO13_MAGICIAN_CPLD_IRQ),
		.end   = gpio_to_irq(GPIO13_MAGICIAN_CPLD_IRQ),
		.flags = IORESOURCE_IRQ,
	},
};

static struct htc_egpio_chip egpio_chips[] = {
	[0] = {
		.reg_start = 0,
		.gpio_base = MAGICIAN_EGPIO(0, 0),
		.num_gpios = 24,
		.direction = HTC_EGPIO_OUTPUT,
		.initial_values = 0x40, /* EGPIO_MAGICIAN_GSM_RESET */
	},
	[1] = {
		.reg_start = 4,
		.gpio_base = MAGICIAN_EGPIO(4, 0),
		.num_gpios = 24,
		.direction = HTC_EGPIO_INPUT,
	},
};

static struct htc_egpio_platform_data egpio_info = {
	.reg_width    = 8,
	.bus_width    = 32,
	.irq_base     = IRQ_BOARD_START,
	.num_irqs     = 4,
	.ack_register = 3,
	.chip         = egpio_chips,
	.num_chips    = ARRAY_SIZE(egpio_chips),
};

static struct platform_device egpio = {
	.name          = "htc-egpio",
	.id            = -1,
	.resource      = egpio_resources,
	.num_resources = ARRAY_SIZE(egpio_resources),
	.dev = {
		.platform_data = &egpio_info,
	},
};

/*
 * LCD - Toppoly TD028STEB1 or Samsung LTP280QV
 */

static struct pxafb_mode_info toppoly_modes[] = {
	{
		.pixclock     = 96153,
		.bpp          = 16,
		.xres         = 240,
		.yres         = 320,
		.hsync_len    = 11,
		.vsync_len    = 3,
		.left_margin  = 19,
		.upper_margin = 2,
		.right_margin = 10,
		.lower_margin = 2,
		.sync         = 0,
	},
};

static struct pxafb_mode_info samsung_modes[] = {
	{
		.pixclock     = 96153,
		.bpp          = 16,
		.xres         = 240,
		.yres         = 320,
		.hsync_len    = 8,
		.vsync_len    = 4,
		.left_margin  = 9,
		.upper_margin = 4,
		.right_margin = 9,
		.lower_margin = 4,
		.sync         = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},
};

static void toppoly_lcd_power(int on, struct fb_var_screeninfo *si)
{
	pr_debug("Toppoly LCD power\n");

	if (on) {
		pr_debug("on\n");
		gpio_set_value(EGPIO_MAGICIAN_TOPPOLY_POWER, 1);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 1);
		udelay(2000);
		gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 1);
		udelay(2000);
		/* FIXME: enable LCDC here */
		udelay(2000);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 1);
		udelay(2000);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 1);
	} else {
		pr_debug("off\n");
		msleep(15);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 0);
		udelay(500);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 0);
		udelay(1000);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 0);
		gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 0);
	}
}

static void samsung_lcd_power(int on, struct fb_var_screeninfo *si)
{
	pr_debug("Samsung LCD power\n");

	if (on) {
		pr_debug("on\n");
		if (system_rev < 3)
			gpio_set_value(GPIO75_MAGICIAN_SAMSUNG_POWER, 1);
		else
			gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 1);
		mdelay(10);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 1);
		mdelay(10);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 1);
		mdelay(30);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 1);
		mdelay(10);
	} else {
		pr_debug("off\n");
		mdelay(10);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 0);
		mdelay(30);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 0);
		mdelay(10);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 0);
		mdelay(10);
		if (system_rev < 3)
			gpio_set_value(GPIO75_MAGICIAN_SAMSUNG_POWER, 0);
		else
			gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 0);
	}
}

static struct pxafb_mach_info toppoly_info = {
	.modes           = toppoly_modes,
	.num_modes       = 1,
	.fixed_modes     = 1,
	.lccr0           = LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3           = LCCR3_PixRsEdg,
	.pxafb_lcd_power = toppoly_lcd_power,
};

static struct pxafb_mach_info samsung_info = {
	.modes           = samsung_modes,
	.num_modes       = 1,
	.fixed_modes     = 1,
	.lccr0           = LCCR0_LDDALT | LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3           = LCCR3_PixFlEdg,
	.pxafb_lcd_power = samsung_lcd_power,
};

/*
 * Backlight
 */

static int magician_backlight_init(struct device *dev)
{
	int ret;

	ret = gpio_request(EGPIO_MAGICIAN_BL_POWER, "BL_POWER");
	if (ret)
		goto err;
	ret = gpio_request(EGPIO_MAGICIAN_BL_POWER2, "BL_POWER2");
	if (ret)
		goto err2;
	return 0;

err2:
	gpio_free(EGPIO_MAGICIAN_BL_POWER);
err:
	return ret;
}

static int magician_backlight_notify(int brightness)
{
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
	gpio_free(EGPIO_MAGICIAN_BL_POWER);
	gpio_free(EGPIO_MAGICIAN_BL_POWER2);
}

static struct platform_pwm_backlight_data backlight_data = {
	.pwm_id         = 0,
	.max_brightness = 272,
	.dft_brightness = 100,
	.pwm_period_ns  = 30923,
	.init           = magician_backlight_init,
	.notify         = magician_backlight_notify,
	.exit           = magician_backlight_exit,
};

static struct platform_device backlight = {
	.name = "pwm-backlight",
	.id   = -1,
	.dev  = {
		.parent        = &pxa27x_device_pwm0.dev,
		.platform_data = &backlight_data,
	},
};

/*
 * LEDs
 */

static struct gpio_led gpio_leds[] = {
	{
		.name = "magician::vibra",
		.default_trigger = "none",
		.gpio = GPIO22_MAGICIAN_VIBRA_EN,
	},
	{
		.name = "magician::phone_bl",
		.default_trigger = "none",
		.gpio = GPIO103_MAGICIAN_LED_KP,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds = gpio_leds,
	.num_leds = ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name = "leds-gpio",
	.id   = -1,
	.dev  = {
		.platform_data = &gpio_led_info,
	},
};

static struct pasic3_led pasic3_leds[] = {
	{
		.led = {
			.name            = "magician:red",
			.default_trigger = "ds2760-battery.0-charging",
		},
		.hw_num = 0,
		.bit2   = PASIC3_BIT2_LED0,
		.mask   = PASIC3_MASK_LED0,
	},
	{
		.led = {
			.name            = "magician:green",
			.default_trigger = "ds2760-battery.0-charging-or-full",
		},
		.hw_num = 1,
		.bit2   = PASIC3_BIT2_LED1,
		.mask   = PASIC3_MASK_LED1,
	},
	{
		.led = {
			.name            = "magician:blue",
			.default_trigger = "bluetooth",
		},
		.hw_num = 2,
		.bit2   = PASIC3_BIT2_LED2,
		.mask   = PASIC3_MASK_LED2,
	},
};

static struct platform_device pasic3;

static struct pasic3_leds_machinfo pasic3_leds_info = {
	.num_leds   = ARRAY_SIZE(pasic3_leds),
	.power_gpio = EGPIO_MAGICIAN_LED_POWER,
	.leds       = pasic3_leds,
};

/*
 * PASIC3 with DS1WM
 */

static struct resource pasic3_resources[] = {
	[0] = {
		.start  = PXA_CS2_PHYS,
		.end	= PXA_CS2_PHYS + 0x1b,
		.flags  = IORESOURCE_MEM,
	},
	/* No IRQ handler in the PASIC3, DS1WM needs an external IRQ */
	[1] = {
		.start  = gpio_to_irq(GPIO107_MAGICIAN_DS1WM_IRQ),
		.end    = gpio_to_irq(GPIO107_MAGICIAN_DS1WM_IRQ),
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct pasic3_platform_data pasic3_platform_data = {
	.bus_shift  = 2,
	.led_pdata  = &pasic3_leds_info,
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
 * External power
 */

static int power_supply_init(struct device *dev)
{
	int ret;

	ret = gpio_request(EGPIO_MAGICIAN_CABLE_STATE_AC, "CABLE_STATE_AC");
	if (ret)
		goto err_cs_ac;
	ret = gpio_request(EGPIO_MAGICIAN_CABLE_STATE_USB, "CABLE_STATE_USB");
	if (ret)
		goto err_cs_usb;
	ret = gpio_request(EGPIO_MAGICIAN_CHARGE_EN, "CHARGE_EN");
	if (ret)
		goto err_chg_en;
	ret = gpio_request(GPIO30_MAGICIAN_nCHARGE_EN, "nCHARGE_EN");
	if (!ret)
		ret = gpio_direction_output(GPIO30_MAGICIAN_nCHARGE_EN, 0);
	if (ret)
		goto err_nchg_en;

	return 0;

err_nchg_en:
	gpio_free(EGPIO_MAGICIAN_CHARGE_EN);
err_chg_en:
	gpio_free(EGPIO_MAGICIAN_CABLE_STATE_USB);
err_cs_usb:
	gpio_free(EGPIO_MAGICIAN_CABLE_STATE_AC);
err_cs_ac:
	return ret;
}

static int magician_is_ac_online(void)
{
	return gpio_get_value(EGPIO_MAGICIAN_CABLE_STATE_AC);
}

static int magician_is_usb_online(void)
{
	return gpio_get_value(EGPIO_MAGICIAN_CABLE_STATE_USB);
}

static void magician_set_charge(int flags)
{
	gpio_set_value(GPIO30_MAGICIAN_nCHARGE_EN, !flags);
	gpio_set_value(EGPIO_MAGICIAN_CHARGE_EN, flags);
}

static void power_supply_exit(struct device *dev)
{
	gpio_free(GPIO30_MAGICIAN_nCHARGE_EN);
	gpio_free(EGPIO_MAGICIAN_CHARGE_EN);
	gpio_free(EGPIO_MAGICIAN_CABLE_STATE_USB);
	gpio_free(EGPIO_MAGICIAN_CABLE_STATE_AC);
}

static char *magician_supplicants[] = {
	"ds2760-battery.0", "backup-battery"
};

static struct pda_power_pdata power_supply_info = {
	.init            = power_supply_init,
	.is_ac_online    = magician_is_ac_online,
	.is_usb_online   = magician_is_usb_online,
	.set_charge      = magician_set_charge,
	.exit            = power_supply_exit,
	.supplied_to     = magician_supplicants,
	.num_supplicants = ARRAY_SIZE(magician_supplicants),
};

static struct resource power_supply_resources[] = {
	[0] = {
		.name  = "ac",
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAGICIAN_AC,
		.end   = IRQ_MAGICIAN_AC,
	},
	[1] = {
		.name  = "usb",
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAGICIAN_AC,
		.end   = IRQ_MAGICIAN_AC,
	},
};

static struct platform_device power_supply = {
	.name = "pda-power",
	.id   = -1,
	.dev  = {
		.platform_data = &power_supply_info,
	},
	.resource      = power_supply_resources,
	.num_resources = ARRAY_SIZE(power_supply_resources),
};


/*
 * MMC/SD
 */

static int magician_mci_init(struct device *dev,
				irq_handler_t detect_irq, void *data)
{
	int err;

	err = request_irq(IRQ_MAGICIAN_SD, detect_irq,
				IRQF_DISABLED | IRQF_SAMPLE_RANDOM,
				"MMC card detect", data);
	if (err)
		goto err_request_irq;
	err = gpio_request(EGPIO_MAGICIAN_SD_POWER, "SD_POWER");
	if (err)
		goto err_request_power;
	err = gpio_request(EGPIO_MAGICIAN_nSD_READONLY, "nSD_READONLY");
	if (err)
		goto err_request_readonly;

	return 0;

err_request_readonly:
	gpio_free(EGPIO_MAGICIAN_SD_POWER);
err_request_power:
	free_irq(IRQ_MAGICIAN_SD, data);
err_request_irq:
	return err;
}

static void magician_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *pdata = dev->platform_data;

	gpio_set_value(EGPIO_MAGICIAN_SD_POWER, (1 << vdd) & pdata->ocr_mask);
}

static int magician_mci_get_ro(struct device *dev)
{
	return (!gpio_get_value(EGPIO_MAGICIAN_nSD_READONLY));
}

static void magician_mci_exit(struct device *dev, void *data)
{
	gpio_free(EGPIO_MAGICIAN_nSD_READONLY);
	gpio_free(EGPIO_MAGICIAN_SD_POWER);
	free_irq(IRQ_MAGICIAN_SD, data);
}

static struct pxamci_platform_data magician_mci_info = {
	.ocr_mask = MMC_VDD_32_33|MMC_VDD_33_34,
	.init     = magician_mci_init,
	.get_ro   = magician_mci_get_ro,
	.setpower = magician_mci_setpower,
	.exit     = magician_mci_exit,
};


/*
 * USB OHCI
 */

static struct pxaohci_platform_data magician_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT3 | POWER_CONTROL_LOW,
	.power_budget	= 0,
};


/*
 * StrataFlash
 */

static void magician_set_vpp(struct map_info *map, int vpp)
{
	gpio_set_value(EGPIO_MAGICIAN_FLASH_VPP, vpp);
}

#define PXA_CS_SIZE		0x04000000

static struct resource strataflash_resource = {
	.start = PXA_CS0_PHYS,
	.end   = PXA_CS0_PHYS + PXA_CS_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

static struct physmap_flash_data strataflash_data = {
	.width = 4,
	.set_vpp = magician_set_vpp,
};

static struct platform_device strataflash = {
	.name          = "physmap-flash",
	.id            = -1,
	.resource      = &strataflash_resource,
	.num_resources = 1,
	.dev = {
		.platform_data = &strataflash_data,
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
	&power_supply,
	&strataflash,
	&leds_gpio,
};

static void __init magician_init(void)
{
	void __iomem *cpld;
	int lcd_select;
	int err;

	gpio_request(GPIO13_MAGICIAN_CPLD_IRQ, "CPLD_IRQ");
	gpio_request(GPIO107_MAGICIAN_DS1WM_IRQ, "DS1WM_IRQ");

	pxa2xx_mfp_config(ARRAY_AND_SIZE(magician_pin_config));

	platform_add_devices(devices, ARRAY_SIZE(devices));

	err = gpio_request(GPIO83_MAGICIAN_nIR_EN, "nIR_EN");
	if (!err) {
		gpio_direction_output(GPIO83_MAGICIAN_nIR_EN, 1);
		pxa_set_ficp_info(&magician_ficp_info);
	}
	pxa_set_i2c_info(NULL);
	pxa_set_mci_info(&magician_mci_info);
	pxa_set_ohci_info(&magician_ohci_info);

	/* Check LCD type we have */
	cpld = ioremap_nocache(PXA_CS3_PHYS, 0x1000);
	if (cpld) {
		u8 board_id = __raw_readb(cpld+0x14);
		iounmap(cpld);
		system_rev = board_id & 0x7;
		lcd_select = board_id & 0x8;
		pr_info("LCD type: %s\n", lcd_select ? "Samsung" : "Toppoly");
		if (lcd_select && (system_rev < 3)) {
			gpio_request(GPIO75_MAGICIAN_SAMSUNG_POWER, "SAMSUNG_POWER");
			gpio_direction_output(GPIO75_MAGICIAN_SAMSUNG_POWER, 0);
		}
		gpio_request(GPIO104_MAGICIAN_LCD_POWER_1, "LCD_POWER_1");
		gpio_request(GPIO105_MAGICIAN_LCD_POWER_2, "LCD_POWER_2");
		gpio_request(GPIO106_MAGICIAN_LCD_POWER_3, "LCD_POWER_3");
		gpio_direction_output(GPIO104_MAGICIAN_LCD_POWER_1, 0);
		gpio_direction_output(GPIO105_MAGICIAN_LCD_POWER_2, 0);
		gpio_direction_output(GPIO106_MAGICIAN_LCD_POWER_3, 0);
		set_pxa_fb_info(lcd_select ? &samsung_info : &toppoly_info);
	} else
		pr_err("LCD detection: CPLD mapping failed\n");
}


MACHINE_START(MAGICIAN, "HTC Magician")
	.phys_io = 0x40000000,
	.io_pg_offst = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params = 0xa0000100,
	.map_io = pxa_map_io,
	.init_irq = pxa27x_init_irq,
	.init_machine = magician_init,
	.timer = &pxa_timer,
MACHINE_END
