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
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mmc/host.h>
#include <linux/fb.h>
#include <linux/pwm_backlight.h>

#include <video/platform_lcd.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/exynos4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>
#include <plat/ehci.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/iic.h>

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

/* GPIO I2C 5 (PMIC) */
static struct i2c_board_info i2c5_devs[] __initdata = {
	/* max8997, To be updated */
};

/* USB EHCI */
static struct s5p_ehci_platdata nuri_ehci_pdata;

static void __init nuri_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &nuri_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

static struct platform_device *nuri_devices[] __initdata = {
	/* Samsung Platform Devices */
	&emmc_fixed_voltage,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_wdt,
	&s3c_device_timer[0],
	&s5p_device_ehci,
	&s3c_device_i2c3,

	/* NURI Devices */
	&nuri_gpio_keys,
	&nuri_lcd_device,
	&nuri_backlight_device,
};

static void __init nuri_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(nuri_uartcfgs, ARRAY_SIZE(nuri_uartcfgs));
}

static void __init nuri_machine_init(void)
{
	nuri_sdhci_init();
	nuri_tsp_init();

	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));
	s3c_i2c3_set_platdata(&i2c3_data);
	i2c_register_board_info(3, i2c3_devs, ARRAY_SIZE(i2c3_devs));
	i2c_register_board_info(5, i2c5_devs, ARRAY_SIZE(i2c5_devs));

	nuri_ehci_init();
	clk_xusbxti.rate = 24000000;

	/* Last */
	platform_add_devices(nuri_devices, ARRAY_SIZE(nuri_devices));
}

MACHINE_START(NURI, "NURI")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= nuri_map_io,
	.init_machine	= nuri_machine_init,
	.timer		= &exynos4_timer,
MACHINE_END
