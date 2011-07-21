/*
 * linux/arch/arm/mach-exynos4/mach-nuri.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/power/max8903_charger.h>
#include <linux/power/max17042_battery.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/mmc/host.h>
#include <linux/fb.h>
#include <linux/pwm_backlight.h>

#include <video/platform_lcd.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/adc.h>
#include <plat/regs-serial.h>
#include <plat/exynos4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>
#include <plat/ehci.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/iic.h>
#include <plat/mfc.h>
#include <plat/pd.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define NURI_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define NURI_ULCON_DEFAULT	S3C2410_LCON_CS8

#define NURI_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG256 |	\
				 S5PV210_UFCON_RXTRIG256)

enum fixed_regulator_id {
	FIXED_REG_ID_MMC = 0,
	FIXED_REG_ID_MAX8903,
};

static struct s3c2410_uartcfg nuri_uartcfgs[] __initdata = {
	{
		.hwport		= 0,
		.ucon		= NURI_UCON_DEFAULT,
		.ulcon		= NURI_ULCON_DEFAULT,
		.ufcon		= NURI_UFCON_DEFAULT,
	},
	{
		.hwport		= 1,
		.ucon		= NURI_UCON_DEFAULT,
		.ulcon		= NURI_ULCON_DEFAULT,
		.ufcon		= NURI_UFCON_DEFAULT,
	},
	{
		.hwport		= 2,
		.ucon		= NURI_UCON_DEFAULT,
		.ulcon		= NURI_ULCON_DEFAULT,
		.ufcon		= NURI_UFCON_DEFAULT,
	},
	{
		.hwport		= 3,
		.ucon		= NURI_UCON_DEFAULT,
		.ulcon		= NURI_ULCON_DEFAULT,
		.ufcon		= NURI_UFCON_DEFAULT,
	},
};

/* eMMC */
static struct s3c_sdhci_platdata nuri_hsmmc0_data __initdata = {
	.max_width		= 8,
	.host_caps		= (MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_DISABLE | MMC_CAP_ERASE),
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static struct regulator_consumer_supply emmc_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.0"),
	REGULATOR_SUPPLY("vmmc", "dw_mmc"),
};

static struct regulator_init_data emmc_fixed_voltage_init_data = {
	.constraints		= {
		.name		= "VMEM_VDD_2.8V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(emmc_supplies),
	.consumer_supplies	= emmc_supplies,
};

static struct fixed_voltage_config emmc_fixed_voltage_config = {
	.supply_name		= "MASSMEMORY_EN (inverted)",
	.microvolts		= 2800000,
	.gpio			= EXYNOS4_GPL1(1),
	.enable_high		= false,
	.init_data		= &emmc_fixed_voltage_init_data,
};

static struct platform_device emmc_fixed_voltage = {
	.name			= "reg-fixed-voltage",
	.id			= FIXED_REG_ID_MMC,
	.dev			= {
		.platform_data	= &emmc_fixed_voltage_config,
	},
};

/* SD */
static struct s3c_sdhci_platdata nuri_hsmmc2_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_DISABLE,
	.ext_cd_gpio		= EXYNOS4_GPX3(3),	/* XEINT_27 */
	.ext_cd_gpio_invert	= 1,
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

/* WLAN */
static struct s3c_sdhci_platdata nuri_hsmmc3_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static void __init nuri_sdhci_init(void)
{
	s3c_sdhci0_set_platdata(&nuri_hsmmc0_data);
	s3c_sdhci2_set_platdata(&nuri_hsmmc2_data);
	s3c_sdhci3_set_platdata(&nuri_hsmmc3_data);
}

/* GPIO KEYS */
static struct gpio_keys_button nuri_gpio_keys_tables[] = {
	{
		.code			= KEY_VOLUMEUP,
		.gpio			= EXYNOS4_GPX2(0),	/* XEINT16 */
		.desc			= "gpio-keys: KEY_VOLUMEUP",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_VOLUMEDOWN,
		.gpio			= EXYNOS4_GPX2(1),	/* XEINT17 */
		.desc			= "gpio-keys: KEY_VOLUMEDOWN",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_POWER,
		.gpio			= EXYNOS4_GPX2(7),	/* XEINT23 */
		.desc			= "gpio-keys: KEY_POWER",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	},
};

static struct gpio_keys_platform_data nuri_gpio_keys_data = {
	.buttons		= nuri_gpio_keys_tables,
	.nbuttons		= ARRAY_SIZE(nuri_gpio_keys_tables),
};

static struct platform_device nuri_gpio_keys = {
	.name			= "gpio-keys",
	.dev			= {
		.platform_data	= &nuri_gpio_keys_data,
	},
};

static void nuri_lcd_power_on(struct plat_lcd_data *pd, unsigned int power)
{
	int gpio = EXYNOS4_GPE1(5);

	gpio_request(gpio, "LVDS_nSHDN");
	gpio_direction_output(gpio, power);
	gpio_free(gpio);
}

static int nuri_bl_init(struct device *dev)
{
	int ret, gpio = EXYNOS4_GPE2(3);

	ret = gpio_request(gpio, "LCD_LDO_EN");
	if (!ret)
		gpio_direction_output(gpio, 0);

	return ret;
}

static int nuri_bl_notify(struct device *dev, int brightness)
{
	if (brightness < 1)
		brightness = 0;

	gpio_set_value(EXYNOS4_GPE2(3), 1);

	return brightness;
}

static void nuri_bl_exit(struct device *dev)
{
	gpio_free(EXYNOS4_GPE2(3));
}

/* nuri pwm backlight */
static struct platform_pwm_backlight_data nuri_backlight_data = {
	.pwm_id			= 0,
	.pwm_period_ns		= 30000,
	.max_brightness		= 100,
	.dft_brightness		= 50,
	.init			= nuri_bl_init,
	.notify			= nuri_bl_notify,
	.exit			= nuri_bl_exit,
};

static struct platform_device nuri_backlight_device = {
	.name			= "pwm-backlight",
	.id			= -1,
	.dev			= {
		.parent		= &s3c_device_timer[0].dev,
		.platform_data	= &nuri_backlight_data,
	},
};

static struct plat_lcd_data nuri_lcd_platform_data = {
	.set_power		= nuri_lcd_power_on,
};

static struct platform_device nuri_lcd_device = {
	.name			= "platform-lcd",
	.id			= -1,
	.dev			= {
		.platform_data	= &nuri_lcd_platform_data,
	},
};

/* I2C1 */
static struct i2c_board_info i2c1_devs[] __initdata = {
	/* Gyro, To be updated */
};

/* TSP */
static u8 mxt_init_vals[] = {
	/* MXT_GEN_COMMAND(6) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_GEN_POWER(7) */
	0x20, 0xff, 0x32,
	/* MXT_GEN_ACQUIRE(8) */
	0x0a, 0x00, 0x05, 0x00, 0x00, 0x00, 0x09, 0x23,
	/* MXT_TOUCH_MULTI(9) */
	0x00, 0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x01, 0x01, 0x0e, 0x0a, 0x0a, 0x0a, 0x0a, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_TOUCH_KEYARRAY(15) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00,
	/* MXT_SPT_GPIOPWM(19) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_GRIPFACE(20) */
	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x28, 0x04,
	0x0f, 0x0a,
	/* MXT_PROCG_NOISE(22) */
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x23, 0x00,
	0x00, 0x05, 0x0f, 0x19, 0x23, 0x2d, 0x03,
	/* MXT_TOUCH_PROXIMITY(23) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_ONETOUCH(24) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x02, 0x08, 0x10, 0x00,
};

static struct mxt_platform_data mxt_platform_data = {
	.config			= mxt_init_vals,
	.config_length		= ARRAY_SIZE(mxt_init_vals),

	.x_line			= 18,
	.y_line			= 11,
	.x_size			= 1024,
	.y_size			= 600,
	.blen			= 0x1,
	.threshold		= 0x28,
	.voltage		= 2800000,		/* 2.8V */
	.orient			= MXT_DIAGONAL_COUNTER,
	.irqflags		= IRQF_TRIGGER_FALLING,
};

static struct s3c2410_platform_i2c i2c3_data __initdata = {
	.flags		= 0,
	.bus_num	= 3,
	.slave_addr	= 0x10,
	.frequency	= 400 * 1000,
	.sda_delay	= 100,
};

static struct i2c_board_info i2c3_devs[] __initdata = {
	{
		I2C_BOARD_INFO("atmel_mxt_ts", 0x4a),
		.platform_data	= &mxt_platform_data,
		.irq		= IRQ_EINT(4),
	},
};

static void __init nuri_tsp_init(void)
{
	int gpio;

	/* TOUCH_INT: XEINT_4 */
	gpio = EXYNOS4_GPX0(4);
	gpio_request(gpio, "TOUCH_INT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
}

static struct regulator_consumer_supply __initdata max8997_ldo1_[] = {
	REGULATOR_SUPPLY("vdd", "s5p-adc"), /* Used by CPU's ADC drv */
};
static struct regulator_consumer_supply __initdata max8997_ldo3_[] = {
	REGULATOR_SUPPLY("vdd11", "s5p-mipi-csis.0"), /* MIPI */
};
static struct regulator_consumer_supply __initdata max8997_ldo4_[] = {
	REGULATOR_SUPPLY("vdd18", "s5p-mipi-csis.0"), /* MIPI */
};
static struct regulator_consumer_supply __initdata max8997_ldo5_[] = {
	REGULATOR_SUPPLY("vhsic", "modemctl"), /* MODEM */
};
static struct regulator_consumer_supply __initdata max8997_ldo7_[] = {
	REGULATOR_SUPPLY("dig_18", "0-001f"), /* HCD803 */
};
static struct regulator_consumer_supply __initdata max8997_ldo8_[] = {
	REGULATOR_SUPPLY("vusb_d", NULL), /* Used by CPU */
	REGULATOR_SUPPLY("vdac", NULL), /* Used by CPU */
};
static struct regulator_consumer_supply __initdata max8997_ldo11_[] = {
	REGULATOR_SUPPLY("vcc", "platform-lcd"), /* U804 LVDS */
};
static struct regulator_consumer_supply __initdata max8997_ldo12_[] = {
	REGULATOR_SUPPLY("vddio", "6-003c"), /* HDC802 */
};
static struct regulator_consumer_supply __initdata max8997_ldo13_[] = {
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.2"), /* TFLASH */
};
static struct regulator_consumer_supply __initdata max8997_ldo14_[] = {
	REGULATOR_SUPPLY("inmotor", "max8997-haptic"),
};
static struct regulator_consumer_supply __initdata max8997_ldo15_[] = {
	REGULATOR_SUPPLY("avdd", "3-004a"), /* Touch Screen */
};
static struct regulator_consumer_supply __initdata max8997_ldo16_[] = {
	REGULATOR_SUPPLY("d_sensor", "0-001f"), /* HDC803 */
};
static struct regulator_consumer_supply __initdata max8997_ldo18_[] = {
	REGULATOR_SUPPLY("vdd", "3-004a"), /* Touch Screen */
};
static struct regulator_consumer_supply __initdata max8997_buck1_[] = {
	REGULATOR_SUPPLY("vdd_arm", NULL), /* CPUFREQ */
};
static struct regulator_consumer_supply __initdata max8997_buck2_[] = {
	REGULATOR_SUPPLY("vdd_int", NULL), /* CPUFREQ */
};
static struct regulator_consumer_supply __initdata max8997_buck3_[] = {
	REGULATOR_SUPPLY("vdd", "mali_dev.0"), /* G3D of Exynos 4 */
};
static struct regulator_consumer_supply __initdata max8997_buck4_[] = {
	REGULATOR_SUPPLY("core", "0-001f"), /* HDC803 */
};
static struct regulator_consumer_supply __initdata max8997_buck6_[] = {
	REGULATOR_SUPPLY("dig_28", "0-001f"), /* pin "7" of HDC803 */
};
static struct regulator_consumer_supply __initdata max8997_esafeout1_[] = {
	REGULATOR_SUPPLY("usb_vbus", NULL), /* CPU's USB OTG */
};
static struct regulator_consumer_supply __initdata max8997_esafeout2_[] = {
	REGULATOR_SUPPLY("usb_vbus", "modemctl"), /* VBUS of Modem */
};

static struct regulator_consumer_supply __initdata max8997_charger_[] = {
	REGULATOR_SUPPLY("vinchg1", "charger-manager.0"),
};
static struct regulator_consumer_supply __initdata max8997_chg_toff_[] = {
	REGULATOR_SUPPLY("vinchg_stop", NULL), /* for jack interrupt handlers */
};

static struct regulator_consumer_supply __initdata max8997_32khz_ap_[] = {
	REGULATOR_SUPPLY("gps_clk", "bcm4751"),
	REGULATOR_SUPPLY("bt_clk", "bcm4330-b1"),
	REGULATOR_SUPPLY("wifi_clk", "bcm433-b1"),
};

static struct regulator_init_data __initdata max8997_ldo1_data = {
	.constraints	= {
		.name		= "VADC_3.3V_C210",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo1_),
	.consumer_supplies	= max8997_ldo1_,
};

static struct regulator_init_data __initdata max8997_ldo2_data = {
	.constraints	= {
		.name		= "VALIVE_1.1V_C210",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo3_data = {
	.constraints	= {
		.name		= "VUSB_1.1V_C210",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo3_),
	.consumer_supplies	= max8997_ldo3_,
};

static struct regulator_init_data __initdata max8997_ldo4_data = {
	.constraints	= {
		.name		= "VMIPI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo4_),
	.consumer_supplies	= max8997_ldo4_,
};

static struct regulator_init_data __initdata max8997_ldo5_data = {
	.constraints	= {
		.name		= "VHSIC_1.2V_C210",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo5_),
	.consumer_supplies	= max8997_ldo5_,
};

static struct regulator_init_data __initdata max8997_ldo6_data = {
	.constraints	= {
		.name		= "VCC_1.8V_PDA",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo7_data = {
	.constraints	= {
		.name		= "CAM_ISP_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo7_),
	.consumer_supplies	= max8997_ldo7_,
};

static struct regulator_init_data __initdata max8997_ldo8_data = {
	.constraints	= {
		.name		= "VUSB/VDAC_3.3V_C210",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo8_),
	.consumer_supplies	= max8997_ldo8_,
};

static struct regulator_init_data __initdata max8997_ldo9_data = {
	.constraints	= {
		.name		= "VCC_2.8V_PDA",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo10_data = {
	.constraints	= {
		.name		= "VPLL_1.1V_C210",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo11_data = {
	.constraints	= {
		.name		= "LVDS_VDD3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.boot_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo11_),
	.consumer_supplies	= max8997_ldo11_,
};

static struct regulator_init_data __initdata max8997_ldo12_data = {
	.constraints	= {
		.name		= "VT_CAM_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo12_),
	.consumer_supplies	= max8997_ldo12_,
};

static struct regulator_init_data __initdata max8997_ldo13_data = {
	.constraints	= {
		.name		= "VTF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo13_),
	.consumer_supplies	= max8997_ldo13_,
};

static struct regulator_init_data __initdata max8997_ldo14_data = {
	.constraints	= {
		.name		= "VCC_3.0V_MOTOR",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo14_),
	.consumer_supplies	= max8997_ldo14_,
};

static struct regulator_init_data __initdata max8997_ldo15_data = {
	.constraints	= {
		.name		= "VTOUCH_ADVV2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo15_),
	.consumer_supplies	= max8997_ldo15_,
};

static struct regulator_init_data __initdata max8997_ldo16_data = {
	.constraints	= {
		.name		= "CAM_SENSOR_IO_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo16_),
	.consumer_supplies	= max8997_ldo16_,
};

static struct regulator_init_data __initdata max8997_ldo18_data = {
	.constraints	= {
		.name		= "VTOUCH_VDD2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo18_),
	.consumer_supplies	= max8997_ldo18_,
};

static struct regulator_init_data __initdata max8997_ldo21_data = {
	.constraints	= {
		.name		= "VDDQ_M1M2_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_buck1_data = {
	.constraints	= {
		.name		= "VARM_1.2V_C210",
		.min_uV		= 900000,
		.max_uV		= 1350000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck1_),
	.consumer_supplies = max8997_buck1_,
};

static struct regulator_init_data __initdata max8997_buck2_data = {
	.constraints	= {
		.name		= "VINT_1.1V_C210",
		.min_uV		= 900000,
		.max_uV		= 1100000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck2_),
	.consumer_supplies = max8997_buck2_,
};

static struct regulator_init_data __initdata max8997_buck3_data = {
	.constraints	= {
		.name		= "VG3D_1.1V_C210",
		.min_uV		= 900000,
		.max_uV		= 1100000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck3_),
	.consumer_supplies = max8997_buck3_,
};

static struct regulator_init_data __initdata max8997_buck4_data = {
	.constraints	= {
		.name		= "CAM_ISP_CORE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck4_),
	.consumer_supplies = max8997_buck4_,
};

static struct regulator_init_data __initdata max8997_buck5_data = {
	.constraints	= {
		.name		= "VMEM_1.2V_C210",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_buck6_data = {
	.constraints	= {
		.name		= "CAM_AF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck6_),
	.consumer_supplies = max8997_buck6_,
};

static struct regulator_init_data __initdata max8997_buck7_data = {
	.constraints	= {
		.name		= "VCC_SUB_2.0V",
		.min_uV		= 2000000,
		.max_uV		= 2000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_32khz_ap_data = {
	.constraints	= {
		.name		= "32KHz AP",
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_32khz_ap_),
	.consumer_supplies = max8997_32khz_ap_,
};

static struct regulator_init_data __initdata max8997_32khz_cp_data = {
	.constraints	= {
		.name		= "32KHz CP",
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_vichg_data = {
	.constraints	= {
		.name		= "VICHG",
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_esafeout1_data = {
	.constraints	= {
		.name		= "SAFEOUT1",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_esafeout1_),
	.consumer_supplies	= max8997_esafeout1_,
};

static struct regulator_init_data __initdata max8997_esafeout2_data = {
	.constraints	= {
		.name		= "SAFEOUT2",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_esafeout2_),
	.consumer_supplies	= max8997_esafeout2_,
};

static struct regulator_init_data __initdata max8997_charger_cv_data = {
	.constraints	= {
		.name		= "CHARGER_CV",
		.min_uV		= 4200000,
		.max_uV		= 4200000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data __initdata max8997_charger_data = {
	.constraints	= {
		.name		= "CHARGER",
		.min_uA		= 200000,
		.max_uA		= 950000,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_CURRENT,
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_charger_),
	.consumer_supplies	= max8997_charger_,
};

static struct regulator_init_data __initdata max8997_charger_topoff_data = {
	.constraints	= {
		.name		= "CHARGER TOPOFF",
		.min_uA		= 50000,
		.max_uA		= 200000,
		.valid_ops_mask = REGULATOR_CHANGE_CURRENT,
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_chg_toff_),
	.consumer_supplies	= max8997_chg_toff_,
};

static struct max8997_regulator_data __initdata nuri_max8997_regulators[] = {
	{ MAX8997_LDO1, &max8997_ldo1_data },
	{ MAX8997_LDO2, &max8997_ldo2_data },
	{ MAX8997_LDO3, &max8997_ldo3_data },
	{ MAX8997_LDO4, &max8997_ldo4_data },
	{ MAX8997_LDO5, &max8997_ldo5_data },
	{ MAX8997_LDO6, &max8997_ldo6_data },
	{ MAX8997_LDO7, &max8997_ldo7_data },
	{ MAX8997_LDO8, &max8997_ldo8_data },
	{ MAX8997_LDO9, &max8997_ldo9_data },
	{ MAX8997_LDO10, &max8997_ldo10_data },
	{ MAX8997_LDO11, &max8997_ldo11_data },
	{ MAX8997_LDO12, &max8997_ldo12_data },
	{ MAX8997_LDO13, &max8997_ldo13_data },
	{ MAX8997_LDO14, &max8997_ldo14_data },
	{ MAX8997_LDO15, &max8997_ldo15_data },
	{ MAX8997_LDO16, &max8997_ldo16_data },

	{ MAX8997_LDO18, &max8997_ldo18_data },
	{ MAX8997_LDO21, &max8997_ldo21_data },

	{ MAX8997_BUCK1, &max8997_buck1_data },
	{ MAX8997_BUCK2, &max8997_buck2_data },
	{ MAX8997_BUCK3, &max8997_buck3_data },
	{ MAX8997_BUCK4, &max8997_buck4_data },
	{ MAX8997_BUCK5, &max8997_buck5_data },
	{ MAX8997_BUCK6, &max8997_buck6_data },
	{ MAX8997_BUCK7, &max8997_buck7_data },

	{ MAX8997_EN32KHZ_AP, &max8997_32khz_ap_data },
	{ MAX8997_EN32KHZ_CP, &max8997_32khz_cp_data },

	{ MAX8997_ENVICHG, &max8997_vichg_data },
	{ MAX8997_ESAFEOUT1, &max8997_esafeout1_data },
	{ MAX8997_ESAFEOUT2, &max8997_esafeout2_data },
	{ MAX8997_CHARGER_CV, &max8997_charger_cv_data },
	{ MAX8997_CHARGER, &max8997_charger_data },
	{ MAX8997_CHARGER_TOPOFF, &max8997_charger_topoff_data },
};

static struct max8997_platform_data __initdata nuri_max8997_pdata = {
	.wakeup			= 1,

	.num_regulators		= ARRAY_SIZE(nuri_max8997_regulators),
	.regulators		= nuri_max8997_regulators,

	.buck125_gpios = { EXYNOS4_GPX0(5), EXYNOS4_GPX0(6), EXYNOS4_GPL0(0) },
	.buck2_gpiodvs = true,

	.buck1_voltage[0] = 1350000, /* 1.35V */
	.buck1_voltage[1] = 1300000, /* 1.3V */
	.buck1_voltage[2] = 1250000, /* 1.25V */
	.buck1_voltage[3] = 1200000, /* 1.2V */
	.buck1_voltage[4] = 1150000, /* 1.15V */
	.buck1_voltage[5] = 1100000, /* 1.1V */
	.buck1_voltage[6] = 1000000, /* 1.0V */
	.buck1_voltage[7] = 950000, /* 0.95V */

	.buck2_voltage[0] = 1100000, /* 1.1V */
	.buck2_voltage[1] = 1000000, /* 1.0V */
	.buck2_voltage[2] = 950000, /* 0.95V */
	.buck2_voltage[3] = 900000, /* 0.9V */
	.buck2_voltage[4] = 1100000, /* 1.1V */
	.buck2_voltage[5] = 1000000, /* 1.0V */
	.buck2_voltage[6] = 950000, /* 0.95V */
	.buck2_voltage[7] = 900000, /* 0.9V */

	.buck5_voltage[0] = 1200000, /* 1.2V */
	.buck5_voltage[1] = 1200000, /* 1.2V */
	.buck5_voltage[2] = 1200000, /* 1.2V */
	.buck5_voltage[3] = 1200000, /* 1.2V */
	.buck5_voltage[4] = 1200000, /* 1.2V */
	.buck5_voltage[5] = 1200000, /* 1.2V */
	.buck5_voltage[6] = 1200000, /* 1.2V */
	.buck5_voltage[7] = 1200000, /* 1.2V */
};

/* GPIO I2C 5 (PMIC) */
enum { I2C5_MAX8997 };
static struct i2c_board_info i2c5_devs[] __initdata = {
	[I2C5_MAX8997] = {
		I2C_BOARD_INFO("max8997", 0xCC >> 1),
		.platform_data	= &nuri_max8997_pdata,
	},
};

static struct max17042_platform_data nuri_battery_platform_data = {
};

/* GPIO I2C 9 (Fuel Gauge) */
static struct i2c_gpio_platform_data i2c9_gpio_data = {
	.sda_pin		= EXYNOS4_GPY4(0),      /* XM0ADDR_8 */
	.scl_pin		= EXYNOS4_GPY4(1),      /* XM0ADDR_9 */
};
static struct platform_device i2c9_gpio = {
	.name			= "i2c-gpio",
	.id			= 9,
	.dev			= {
		.platform_data	= &i2c9_gpio_data,
	},
};
enum { I2C9_MAX17042};
static struct i2c_board_info i2c9_devs[] __initdata = {
	[I2C9_MAX17042] = {
		I2C_BOARD_INFO("max17042", 0x36),
		.platform_data = &nuri_battery_platform_data,
	},
};

/* MAX8903 Secondary Charger */
static struct regulator_consumer_supply supplies_max8903[] = {
	REGULATOR_SUPPLY("vinchg2", "charger-manager.0"),
};

static struct regulator_init_data max8903_charger_en_data = {
	.constraints = {
		.name		= "VOUT_CHARGER",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.boot_on	= 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(supplies_max8903),
	.consumer_supplies = supplies_max8903,
};

static struct fixed_voltage_config max8903_charger_en = {
	.supply_name = "VOUT_CHARGER",
	.microvolts = 5000000, /* Assume 5VDC */
	.gpio = EXYNOS4_GPY4(5), /* TA_EN negaged */
	.enable_high = 0, /* Enable = Low */
	.enabled_at_boot = 1,
	.init_data = &max8903_charger_en_data,
};

static struct platform_device max8903_fixed_reg_dev = {
	.name = "reg-fixed-voltage",
	.id = FIXED_REG_ID_MAX8903,
	.dev = { .platform_data	= &max8903_charger_en },
};

static struct max8903_pdata nuri_max8903 = {
	/*
	 * cen: don't control with the driver, let it be
	 * controlled by regulator above
	 */
	.dok = EXYNOS4_GPX1(4), /* TA_nCONNECTED */
	/* uok, usus: not connected */
	.chg = EXYNOS4_GPE2(0), /* TA_nCHG */
	/* flt: vcc_1.8V_pda */
	.dcm = EXYNOS4_GPL0(1), /* CURR_ADJ */

	.dc_valid = true,
	.usb_valid = false, /* USB is not wired to MAX8903 */
};

static struct platform_device nuri_max8903_device = {
	.name			= "max8903-charger",
	.dev			= {
		.platform_data	= &nuri_max8903,
	},
};

static struct device *nuri_cm_devices[] = {
	&s3c_device_i2c5.dev,
	&s3c_device_adc.dev,
	NULL, /* Reserved for UART */
	NULL,
};

static void __init nuri_power_init(void)
{
	int gpio;
	int irq_base = IRQ_GPIO_END + 1;
	int ta_en = 0;

	nuri_max8997_pdata.irq_base = irq_base;
	irq_base += MAX8997_IRQ_NR;

	gpio = EXYNOS4_GPX0(7);
	gpio_request(gpio, "AP_PMIC_IRQ");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = EXYNOS4_GPX2(3);
	gpio_request(gpio, "FUEL_ALERT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = nuri_max8903.dok;
	gpio_request(gpio, "TA_nCONNECTED");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	ta_en = gpio_get_value(gpio) ? 0 : 1;

	gpio = nuri_max8903.chg;
	gpio_request(gpio, "TA_nCHG");
	gpio_direction_input(gpio);

	gpio = nuri_max8903.dcm;
	gpio_request(gpio, "CURR_ADJ");
	gpio_direction_output(gpio, ta_en);
}

/* USB EHCI */
static struct s5p_ehci_platdata nuri_ehci_pdata;

static void __init nuri_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &nuri_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

static struct platform_device *nuri_devices[] __initdata = {
	/* Samsung Platform Devices */
	&s3c_device_i2c5, /* PMIC should initialize first */
	&emmc_fixed_voltage,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_wdt,
	&s3c_device_timer[0],
	&s5p_device_ehci,
	&s3c_device_i2c3,
	&i2c9_gpio,
	&s3c_device_adc,
	&s3c_device_rtc,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,
	&exynos4_device_pd[PD_MFC],

	/* NURI Devices */
	&nuri_gpio_keys,
	&nuri_lcd_device,
	&nuri_backlight_device,
	&max8903_fixed_reg_dev,
	&nuri_max8903_device,
};

static void __init nuri_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(nuri_uartcfgs, ARRAY_SIZE(nuri_uartcfgs));
}

static void __init nuri_reserve(void)
{
	s5p_mfc_reserve_mem(0x43000000, 8 << 20, 0x51000000, 8 << 20);
}

static void __init nuri_machine_init(void)
{
	nuri_sdhci_init();
	nuri_tsp_init();
	nuri_power_init();

	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));
	s3c_i2c3_set_platdata(&i2c3_data);
	i2c_register_board_info(3, i2c3_devs, ARRAY_SIZE(i2c3_devs));
	s3c_i2c5_set_platdata(NULL);
	i2c5_devs[I2C5_MAX8997].irq = gpio_to_irq(EXYNOS4_GPX0(7));
	i2c_register_board_info(5, i2c5_devs, ARRAY_SIZE(i2c5_devs));
	i2c9_devs[I2C9_MAX17042].irq = gpio_to_irq(EXYNOS4_GPX2(3));
	i2c_register_board_info(9, i2c9_devs, ARRAY_SIZE(i2c9_devs));

	nuri_ehci_init();
	clk_xusbxti.rate = 24000000;

	/* Last */
	platform_add_devices(nuri_devices, ARRAY_SIZE(nuri_devices));
	s5p_device_mfc.dev.parent = &exynos4_device_pd[PD_MFC].dev;
}

MACHINE_START(NURI, "NURI")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= nuri_map_io,
	.init_machine	= nuri_machine_init,
	.timer		= &exynos4_timer,
	.reserve        = &nuri_reserve,
MACHINE_END
