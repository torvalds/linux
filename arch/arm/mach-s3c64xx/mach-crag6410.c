/* linux/arch/arm/mach-s3c64xx/mach-crag6410.c
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *	Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * Copyright 2011 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/pwm_backlight.h>
#include <linux/dm9000.h>
#include <linux/gpio_keys.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/spi/spi.h>

#include <linux/i2c/pca953x.h>
#include <linux/platform_data/s3c-hsotg.h>

#include <video/platform_lcd.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>
#include <linux/mfd/wm831x/gpio.h>

#include <sound/wm1250-ev1.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <video/samsung_fimd.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <mach/regs-gpio.h>

#include <plat/regs-serial.h>
#include <plat/fb.h>
#include <plat/sdhci.h>
#include <plat/gpio-cfg.h>
#include <linux/platform_data/spi-s3c64xx.h>

#include <plat/keypad.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/adc.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/pm.h>
#include <plat/samsung-time.h>

#include "common.h"
#include "crag6410.h"
#include "regs-gpio-memport.h"
#include "regs-modem.h"
#include "regs-sys.h"

/* serial port setup */

#define UCON (S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK)
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c2410_uartcfg crag6410_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
};

static struct platform_pwm_backlight_data crag6410_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 1000,
	.dft_brightness	= 600,
	.pwm_period_ns	= 100000,	/* about 1kHz */
};

static struct platform_device crag6410_backlight_device = {
	.name		= "pwm-backlight",
	.id		= -1,
	.dev		= {
		.parent	= &s3c_device_timer[0].dev,
		.platform_data = &crag6410_backlight_data,
	},
};

static void crag6410_lcd_power_set(struct plat_lcd_data *pd, unsigned int power)
{
	pr_debug("%s: setting power %d\n", __func__, power);

	if (power) {
		gpio_set_value(S3C64XX_GPB(0), 1);
		msleep(1);
		s3c_gpio_cfgpin(S3C64XX_GPF(14), S3C_GPIO_SFN(2));
	} else {
		gpio_direction_output(S3C64XX_GPF(14), 0);
		gpio_set_value(S3C64XX_GPB(0), 0);
	}
}

static struct platform_device crag6410_lcd_powerdev = {
	.name			= "platform-lcd",
	.id			= -1,
	.dev.parent		= &s3c_device_fb.dev,
	.dev.platform_data	= &(struct plat_lcd_data) {
		.set_power	= crag6410_lcd_power_set,
	},
};

/* 640x480 URT */
static struct s3c_fb_pd_win crag6410_fb_win0 = {
	.max_bpp	= 32,
	.default_bpp	= 16,
	.xres		= 640,
	.yres		= 480,
	.virtual_y	= 480 * 2,
	.virtual_x	= 640,
};

static struct fb_videomode crag6410_lcd_timing = {
	.left_margin	= 150,
	.right_margin	= 80,
	.upper_margin	= 40,
	.lower_margin	= 5,
	.hsync_len	= 40,
	.vsync_len	= 5,
	.xres		= 640,
	.yres		= 480,
};

/* 405566 clocks per frame => 60Hz refresh requires 24333960Hz clock */
static struct s3c_fb_platdata crag6410_lcd_pdata = {
	.setup_gpio	= s3c64xx_fb_gpio_setup_24bpp,
	.vtiming	= &crag6410_lcd_timing,
	.win[0]		= &crag6410_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
};

/* 2x6 keypad */

static uint32_t crag6410_keymap[] = {
	/* KEY(row, col, keycode) */
	KEY(0, 0, KEY_VOLUMEUP),
	KEY(0, 1, KEY_HOME),
	KEY(0, 2, KEY_VOLUMEDOWN),
	KEY(0, 3, KEY_HELP),
	KEY(0, 4, KEY_MENU),
	KEY(0, 5, KEY_MEDIA),
	KEY(1, 0, 232),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_LEFT),
	KEY(1, 3, KEY_UP),
	KEY(1, 4, KEY_RIGHT),
	KEY(1, 5, KEY_CAMERA),
};

static struct matrix_keymap_data crag6410_keymap_data = {
	.keymap		= crag6410_keymap,
	.keymap_size	= ARRAY_SIZE(crag6410_keymap),
};

static struct samsung_keypad_platdata crag6410_keypad_data = {
	.keymap_data	= &crag6410_keymap_data,
	.rows		= 2,
	.cols		= 6,
};

static struct gpio_keys_button crag6410_gpio_keys[] = {
	[0] = {
		.code	= KEY_SUSPEND,
		.gpio	= S3C64XX_GPL(10),	/* EINT 18 */
		.type	= EV_KEY,
		.wakeup	= 1,
		.active_low = 1,
	},
	[1] = {
		.code	= SW_FRONT_PROXIMITY,
		.gpio	= S3C64XX_GPN(11),	/* EINT 11 */
		.type	= EV_SW,
	},
};

static struct gpio_keys_platform_data crag6410_gpio_keydata = {
	.buttons	= crag6410_gpio_keys,
	.nbuttons	= ARRAY_SIZE(crag6410_gpio_keys),
};

static struct platform_device crag6410_gpio_keydev = {
	.name		= "gpio-keys",
	.id		= 0,
	.dev.platform_data = &crag6410_gpio_keydata,
};

static struct resource crag6410_dm9k_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_XM0CSN5, 2),
	[1] = DEFINE_RES_MEM(S3C64XX_PA_XM0CSN5 + (1 << 8), 2),
	[2] = DEFINE_RES_NAMED(S3C_EINT(17), 1, NULL, IORESOURCE_IRQ \
				| IORESOURCE_IRQ_HIGHLEVEL),
};

static struct dm9000_plat_data mini6410_dm9k_pdata = {
	.flags	= DM9000_PLATF_16BITONLY,
};

static struct platform_device crag6410_dm9k_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(crag6410_dm9k_resource),
	.resource	= crag6410_dm9k_resource,
	.dev.platform_data = &mini6410_dm9k_pdata,
};

static struct resource crag6410_mmgpio_resource[] = {
	[0] = DEFINE_RES_MEM_NAMED(S3C64XX_PA_XM0CSN4, 1, "dat"),
};

static struct platform_device crag6410_mmgpio = {
	.name		= "basic-mmio-gpio",
	.id		= -1,
	.resource	= crag6410_mmgpio_resource,
	.num_resources	= ARRAY_SIZE(crag6410_mmgpio_resource),
	.dev.platform_data = &(struct bgpio_pdata) {
		.base	= MMGPIO_GPIO_BASE,
	},
};

static struct platform_device speyside_device = {
	.name		= "speyside",
	.id		= -1,
};

static struct platform_device lowland_device = {
	.name		= "lowland",
	.id		= -1,
};

static struct platform_device tobermory_device = {
	.name		= "tobermory",
	.id		= -1,
};

static struct platform_device littlemill_device = {
	.name		= "littlemill",
	.id		= -1,
};

static struct platform_device bells_wm2200_device = {
	.name		= "bells",
	.id		= 0,
};

static struct platform_device bells_wm5102_device = {
	.name		= "bells",
	.id		= 1,
};

static struct platform_device bells_wm5110_device = {
	.name		= "bells",
	.id		= 2,
};

static struct regulator_consumer_supply wallvdd_consumers[] = {
	REGULATOR_SUPPLY("SPKVDD", "1-001a"),
	REGULATOR_SUPPLY("SPKVDD1", "1-001a"),
	REGULATOR_SUPPLY("SPKVDD2", "1-001a"),
	REGULATOR_SUPPLY("SPKVDDL", "1-001a"),
	REGULATOR_SUPPLY("SPKVDDR", "1-001a"),

	REGULATOR_SUPPLY("SPKVDDL", "spi0.1"),
	REGULATOR_SUPPLY("SPKVDDR", "spi0.1"),
	REGULATOR_SUPPLY("SPKVDDL", "wm5102-codec"),
	REGULATOR_SUPPLY("SPKVDDR", "wm5102-codec"),
	REGULATOR_SUPPLY("SPKVDDL", "wm5110-codec"),
	REGULATOR_SUPPLY("SPKVDDR", "wm5110-codec"),

	REGULATOR_SUPPLY("DC1VDD", "0-0034"),
	REGULATOR_SUPPLY("DC2VDD", "0-0034"),
	REGULATOR_SUPPLY("DC3VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO1VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO2VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO4VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO5VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO6VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO7VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO8VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO9VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO10VDD", "0-0034"),
	REGULATOR_SUPPLY("LDO11VDD", "0-0034"),

	REGULATOR_SUPPLY("DC1VDD", "1-0034"),
	REGULATOR_SUPPLY("DC2VDD", "1-0034"),
	REGULATOR_SUPPLY("DC3VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO1VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO2VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO4VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO5VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO6VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO7VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO8VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO9VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO10VDD", "1-0034"),
	REGULATOR_SUPPLY("LDO11VDD", "1-0034"),
};

static struct regulator_init_data wallvdd_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(wallvdd_consumers),
	.consumer_supplies = wallvdd_consumers,
};

static struct fixed_voltage_config wallvdd_pdata = {
	.supply_name = "WALLVDD",
	.microvolts = 5000000,
	.init_data = &wallvdd_data,
	.gpio = -EINVAL,
};

static struct platform_device wallvdd_device = {
	.name		= "reg-fixed-voltage",
	.id		= -1,
	.dev = {
		.platform_data = &wallvdd_pdata,
	},
};

static struct platform_device *crag6410_devices[] __initdata = {
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc2,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_fb,
	&s3c_device_ohci,
	&s3c_device_usb_hsotg,
	&s3c_device_timer[0],
	&s3c64xx_device_iis0,
	&s3c64xx_device_iis1,
	&samsung_device_keypad,
	&crag6410_gpio_keydev,
	&crag6410_dm9k_device,
	&s3c64xx_device_spi0,
	&crag6410_mmgpio,
	&crag6410_lcd_powerdev,
	&crag6410_backlight_device,
	&speyside_device,
	&tobermory_device,
	&littlemill_device,
	&lowland_device,
	&bells_wm2200_device,
	&bells_wm5102_device,
	&bells_wm5110_device,
	&wallvdd_device,
};

static struct pca953x_platform_data crag6410_pca_data = {
	.gpio_base	= PCA935X_GPIO_BASE,
	.irq_base	= -1,
};

/* VDDARM is controlled by DVS1 connected to GPK(0) */
static struct wm831x_buckv_pdata vddarm_pdata = {
	.dvs_control_src = 1,
	.dvs_gpio = S3C64XX_GPK(0),
};

static struct regulator_consumer_supply vddarm_consumers[] = {
	REGULATOR_SUPPLY("vddarm", NULL),
};

static struct regulator_init_data vddarm = {
	.constraints = {
		.name = "VDDARM",
		.min_uV = 1000000,
		.max_uV = 1300000,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(vddarm_consumers),
	.consumer_supplies = vddarm_consumers,
	.supply_regulator = "WALLVDD",
	.driver_data = &vddarm_pdata,
};

static struct regulator_consumer_supply vddint_consumers[] = {
	REGULATOR_SUPPLY("vddint", NULL),
};

static struct regulator_init_data vddint = {
	.constraints = {
		.name = "VDDINT",
		.min_uV = 1000000,
		.max_uV = 1200000,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(vddint_consumers),
	.consumer_supplies = vddint_consumers,
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddmem = {
	.constraints = {
		.name = "VDDMEM",
		.always_on = 1,
	},
};

static struct regulator_init_data vddsys = {
	.constraints = {
		.name = "VDDSYS,VDDEXT,VDDPCM,VDDSS",
		.always_on = 1,
	},
};

static struct regulator_consumer_supply vddmmc_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.0"),
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.1"),
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.2"),
};

static struct regulator_init_data vddmmc = {
	.constraints = {
		.name = "VDDMMC,UH",
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(vddmmc_consumers),
	.consumer_supplies = vddmmc_consumers,
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddotgi = {
	.constraints = {
		.name = "VDDOTGi",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddotg = {
	.constraints = {
		.name = "VDDOTG",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddhi = {
	.constraints = {
		.name = "VDDHI",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddadc = {
	.constraints = {
		.name = "VDDADC,VDDDAC",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddmem0 = {
	.constraints = {
		.name = "VDDMEM0",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddpll = {
	.constraints = {
		.name = "VDDPLL",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddlcd = {
	.constraints = {
		.name = "VDDLCD",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data vddalive = {
	.constraints = {
		.name = "VDDALIVE",
		.always_on = 1,
	},
	.supply_regulator = "WALLVDD",
};

static struct wm831x_backup_pdata banff_backup_pdata = {
	.charger_enable = 1,
	.vlim = 2500,  /* mV */
	.ilim = 200,   /* uA */
};

static struct wm831x_status_pdata banff_red_led = {
	.name = "banff:red:",
	.default_src = WM831X_STATUS_MANUAL,
};

static struct wm831x_status_pdata banff_green_led = {
	.name = "banff:green:",
	.default_src = WM831X_STATUS_MANUAL,
};

static struct wm831x_touch_pdata touch_pdata = {
	.data_irq = S3C_EINT(26),
	.pd_irq = S3C_EINT(27),
};

static struct wm831x_pdata crag_pmic_pdata = {
	.wm831x_num = 1,
	.gpio_base = BANFF_PMIC_GPIO_BASE,
	.soft_shutdown = true,

	.backup = &banff_backup_pdata,

	.gpio_defaults = {
		/* GPIO5: DVS1_REQ - CMOS, DBVDD, active high */
		[4] = WM831X_GPN_DIR | WM831X_GPN_POL | WM831X_GPN_ENA | 0x8,
		/* GPIO11: Touchscreen data - CMOS, DBVDD, active high*/
		[10] = WM831X_GPN_POL | WM831X_GPN_ENA | 0x6,
		/* GPIO12: Touchscreen pen down - CMOS, DBVDD, active high*/
		[11] = WM831X_GPN_POL | WM831X_GPN_ENA | 0x7,
	},

	.dcdc = {
		&vddarm,  /* DCDC1 */
		&vddint,  /* DCDC2 */
		&vddmem,  /* DCDC3 */
	},

	.ldo = {
		&vddsys,   /* LDO1 */
		&vddmmc,   /* LDO2 */
		NULL,      /* LDO3 */
		&vddotgi,  /* LDO4 */
		&vddotg,   /* LDO5 */
		&vddhi,    /* LDO6 */
		&vddadc,   /* LDO7 */
		&vddmem0,  /* LDO8 */
		&vddpll,   /* LDO9 */
		&vddlcd,   /* LDO10 */
		&vddalive, /* LDO11 */
	},

	.status = {
		&banff_green_led,
		&banff_red_led,
	},

	.touch = &touch_pdata,
};

static struct i2c_board_info i2c_devs0[] = {
	{ I2C_BOARD_INFO("24c08", 0x50), },
	{ I2C_BOARD_INFO("tca6408", 0x20),
	  .platform_data = &crag6410_pca_data,
	},
	{ I2C_BOARD_INFO("wm8312", 0x34),
	  .platform_data = &crag_pmic_pdata,
	  .irq = S3C_EINT(23),
	},
};

static struct s3c2410_platform_i2c i2c0_pdata = {
	.frequency = 400000,
};

static struct regulator_consumer_supply pvdd_1v2_consumers[] = {
	REGULATOR_SUPPLY("DCVDD", "spi0.0"),
	REGULATOR_SUPPLY("AVDD", "spi0.0"),
	REGULATOR_SUPPLY("AVDD", "spi0.1"),
};

static struct regulator_init_data pvdd_1v2 = {
	.constraints = {
		.name = "PVDD_1V2",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},

	.consumer_supplies = pvdd_1v2_consumers,
	.num_consumer_supplies = ARRAY_SIZE(pvdd_1v2_consumers),
};

static struct regulator_consumer_supply pvdd_1v8_consumers[] = {
	REGULATOR_SUPPLY("LDOVDD", "1-001a"),
	REGULATOR_SUPPLY("PLLVDD", "1-001a"),
	REGULATOR_SUPPLY("DBVDD", "1-001a"),
	REGULATOR_SUPPLY("DBVDD1", "1-001a"),
	REGULATOR_SUPPLY("DBVDD2", "1-001a"),
	REGULATOR_SUPPLY("DBVDD3", "1-001a"),
	REGULATOR_SUPPLY("CPVDD", "1-001a"),
	REGULATOR_SUPPLY("AVDD2", "1-001a"),
	REGULATOR_SUPPLY("DCVDD", "1-001a"),
	REGULATOR_SUPPLY("AVDD", "1-001a"),
	REGULATOR_SUPPLY("DBVDD", "spi0.0"),

	REGULATOR_SUPPLY("DBVDD", "1-003a"),
	REGULATOR_SUPPLY("LDOVDD", "1-003a"),
	REGULATOR_SUPPLY("CPVDD", "1-003a"),
	REGULATOR_SUPPLY("AVDD", "1-003a"),
	REGULATOR_SUPPLY("DBVDD1", "spi0.1"),
	REGULATOR_SUPPLY("DBVDD2", "spi0.1"),
	REGULATOR_SUPPLY("DBVDD3", "spi0.1"),
	REGULATOR_SUPPLY("LDOVDD", "spi0.1"),
	REGULATOR_SUPPLY("CPVDD", "spi0.1"),

	REGULATOR_SUPPLY("DBVDD2", "wm5102-codec"),
	REGULATOR_SUPPLY("DBVDD3", "wm5102-codec"),
	REGULATOR_SUPPLY("CPVDD", "wm5102-codec"),

	REGULATOR_SUPPLY("DBVDD2", "wm5110-codec"),
	REGULATOR_SUPPLY("DBVDD3", "wm5110-codec"),
	REGULATOR_SUPPLY("CPVDD", "wm5110-codec"),
};

static struct regulator_init_data pvdd_1v8 = {
	.constraints = {
		.name = "PVDD_1V8",
		.always_on = 1,
	},

	.consumer_supplies = pvdd_1v8_consumers,
	.num_consumer_supplies = ARRAY_SIZE(pvdd_1v8_consumers),
};

static struct regulator_consumer_supply pvdd_3v3_consumers[] = {
	REGULATOR_SUPPLY("MICVDD", "1-001a"),
	REGULATOR_SUPPLY("AVDD1", "1-001a"),
};

static struct regulator_init_data pvdd_3v3 = {
	.constraints = {
		.name = "PVDD_3V3",
		.always_on = 1,
	},

	.consumer_supplies = pvdd_3v3_consumers,
	.num_consumer_supplies = ARRAY_SIZE(pvdd_3v3_consumers),
};

static struct wm831x_pdata glenfarclas_pmic_pdata = {
	.wm831x_num = 2,
	.irq_base = GLENFARCLAS_PMIC_IRQ_BASE,
	.gpio_base = GLENFARCLAS_PMIC_GPIO_BASE,
	.soft_shutdown = true,

	.gpio_defaults = {
		/* GPIO1-3: IRQ inputs, rising edge triggered, CMOS */
		[0] = WM831X_GPN_DIR | WM831X_GPN_POL | WM831X_GPN_ENA,
		[1] = WM831X_GPN_DIR | WM831X_GPN_POL | WM831X_GPN_ENA,
		[2] = WM831X_GPN_DIR | WM831X_GPN_POL | WM831X_GPN_ENA,
	},

	.dcdc = {
		&pvdd_1v2,  /* DCDC1 */
		&pvdd_1v8,  /* DCDC2 */
		&pvdd_3v3,  /* DCDC3 */
	},

	.disable_touch = true,
};

static struct wm1250_ev1_pdata wm1250_ev1_pdata = {
	.gpios = {
		[WM1250_EV1_GPIO_CLK_ENA] = S3C64XX_GPN(12),
		[WM1250_EV1_GPIO_CLK_SEL0] = S3C64XX_GPL(12),
		[WM1250_EV1_GPIO_CLK_SEL1] = S3C64XX_GPL(13),
		[WM1250_EV1_GPIO_OSR] = S3C64XX_GPL(14),
		[WM1250_EV1_GPIO_MASTER] = S3C64XX_GPL(8),
	},
};

static struct i2c_board_info i2c_devs1[] = {
	{ I2C_BOARD_INFO("wm8311", 0x34),
	  .irq = S3C_EINT(0),
	  .platform_data = &glenfarclas_pmic_pdata },

	{ I2C_BOARD_INFO("wlf-gf-module", 0x20) },
	{ I2C_BOARD_INFO("wlf-gf-module", 0x22) },
	{ I2C_BOARD_INFO("wlf-gf-module", 0x24) },
	{ I2C_BOARD_INFO("wlf-gf-module", 0x25) },
	{ I2C_BOARD_INFO("wlf-gf-module", 0x26) },

	{ I2C_BOARD_INFO("wm1250-ev1", 0x27),
	  .platform_data = &wm1250_ev1_pdata },
};

static struct s3c2410_platform_i2c i2c1_pdata = {
	.frequency = 400000,
	.bus_num = 1,
};

static void __init crag6410_map_io(void)
{
	s3c64xx_init_io(NULL, 0);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(crag6410_uartcfgs, ARRAY_SIZE(crag6410_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);

	/* LCD type and Bypass set by bootloader */
}

static struct s3c_sdhci_platdata crag6410_hsmmc2_pdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
	.host_caps		= MMC_CAP_POWER_OFF_CARD,
};

static void crag6410_cfg_sdhci0(struct platform_device *dev, int width)
{
	/* Set all the necessary GPG pins to special-function 2 */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPG(0), 2 + width, S3C_GPIO_SFN(2));

	/* force card-detected for prototype 0 */
	s3c_gpio_setpull(S3C64XX_GPG(6), S3C_GPIO_PULL_DOWN);
}

static struct s3c_sdhci_platdata crag6410_hsmmc0_pdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
	.cfg_gpio		= crag6410_cfg_sdhci0,
	.host_caps		= MMC_CAP_POWER_OFF_CARD,
};

static const struct gpio_led gpio_leds[] = {
	{
		.name = "d13:green:",
		.gpio = MMGPIO_GPIO_BASE + 0,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d14:green:",
		.gpio = MMGPIO_GPIO_BASE + 1,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d15:green:",
		.gpio = MMGPIO_GPIO_BASE + 2,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d16:green:",
		.gpio = MMGPIO_GPIO_BASE + 3,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d17:green:",
		.gpio = MMGPIO_GPIO_BASE + 4,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d18:green:",
		.gpio = MMGPIO_GPIO_BASE + 5,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d19:green:",
		.gpio = MMGPIO_GPIO_BASE + 6,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "d20:green:",
		.gpio = MMGPIO_GPIO_BASE + 7,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
};

static const struct gpio_led_platform_data gpio_leds_pdata = {
	.leds = gpio_leds,
	.num_leds = ARRAY_SIZE(gpio_leds),
};

static struct s3c_hsotg_plat crag6410_hsotg_pdata;

static void __init crag6410_machine_init(void)
{
	/* Open drain IRQs need pullups */
	s3c_gpio_setpull(S3C64XX_GPM(0), S3C_GPIO_PULL_UP);
	s3c_gpio_setpull(S3C64XX_GPN(0), S3C_GPIO_PULL_UP);

	gpio_request(S3C64XX_GPB(0), "LCD power");
	gpio_direction_output(S3C64XX_GPB(0), 0);

	gpio_request(S3C64XX_GPF(14), "LCD PWM");
	gpio_direction_output(S3C64XX_GPF(14), 0);  /* turn off */

	gpio_request(S3C64XX_GPB(1), "SD power");
	gpio_direction_output(S3C64XX_GPB(1), 0);

	gpio_request(S3C64XX_GPF(10), "nRESETSEL");
	gpio_direction_output(S3C64XX_GPF(10), 1);

	s3c_sdhci0_set_platdata(&crag6410_hsmmc0_pdata);
	s3c_sdhci2_set_platdata(&crag6410_hsmmc2_pdata);

	s3c_i2c0_set_platdata(&i2c0_pdata);
	s3c_i2c1_set_platdata(&i2c1_pdata);
	s3c_fb_set_platdata(&crag6410_lcd_pdata);
	s3c_hsotg_set_platdata(&crag6410_hsotg_pdata);

	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	samsung_keypad_set_platdata(&crag6410_keypad_data);
	s3c64xx_spi0_set_platdata(NULL, 0, 2);

	platform_add_devices(crag6410_devices, ARRAY_SIZE(crag6410_devices));

	gpio_led_register_device(-1, &gpio_leds_pdata);

	regulator_has_full_constraints();

	s3c64xx_pm_init();
}

MACHINE_START(WLF_CRAGG_6410, "Wolfson Cragganmore 6410")
	/* Maintainer: Mark Brown <broonie@opensource.wolfsonmicro.com> */
	.atag_offset	= 0x100,
	.init_irq	= s3c6410_init_irq,
	.map_io		= crag6410_map_io,
	.init_machine	= crag6410_machine_init,
	.init_late	= s3c64xx_init_late,
	.init_time	= samsung_timer_init,
	.restart	= s3c64xx_restart,
MACHINE_END
