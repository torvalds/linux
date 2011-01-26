/* linux/arch/arm/mach-s5pv310/mach-universal_c210.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mmc/host.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/s5pv310.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define UNIVERSAL_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define UNIVERSAL_ULCON_DEFAULT	S3C2410_LCON_CS8

#define UNIVERSAL_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG256 |	\
				 S5PV210_UFCON_RXTRIG256)

static struct s3c2410_uartcfg universal_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
};

static struct gpio_keys_button universal_gpio_keys_tables[] = {
	{
		.code			= KEY_VOLUMEUP,
		.gpio			= S5PV310_GPX2(0),	/* XEINT16 */
		.desc			= "gpio-keys: KEY_VOLUMEUP",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_VOLUMEDOWN,
		.gpio			= S5PV310_GPX2(1),	/* XEINT17 */
		.desc			= "gpio-keys: KEY_VOLUMEDOWN",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_CONFIG,
		.gpio			= S5PV310_GPX2(2),	/* XEINT18 */
		.desc			= "gpio-keys: KEY_CONFIG",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_CAMERA,
		.gpio			= S5PV310_GPX2(3),	/* XEINT19 */
		.desc			= "gpio-keys: KEY_CAMERA",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_OK,
		.gpio			= S5PV310_GPX3(5),	/* XEINT29 */
		.desc			= "gpio-keys: KEY_OK",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	},
};

static struct gpio_keys_platform_data universal_gpio_keys_data = {
	.buttons	= universal_gpio_keys_tables,
	.nbuttons	= ARRAY_SIZE(universal_gpio_keys_tables),
};

static struct platform_device universal_gpio_keys = {
	.name			= "gpio-keys",
	.dev			= {
		.platform_data	= &universal_gpio_keys_data,
	},
};

/* eMMC */
static struct s3c_sdhci_platdata universal_hsmmc0_data __initdata = {
	.max_width		= 8,
	.host_caps		= (MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_DISABLE),
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static struct regulator_consumer_supply mmc0_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.0"),
};

static struct regulator_init_data mmc0_fixed_voltage_init_data = {
	.constraints		= {
		.name		= "VMEM_VDD_2.8V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(mmc0_supplies),
	.consumer_supplies	= mmc0_supplies,
};

static struct fixed_voltage_config mmc0_fixed_voltage_config = {
	.supply_name		= "MASSMEMORY_EN",
	.microvolts		= 2800000,
	.gpio			= S5PV310_GPE1(3),
	.enable_high		= true,
	.init_data		= &mmc0_fixed_voltage_init_data,
};

static struct platform_device mmc0_fixed_voltage = {
	.name			= "reg-fixed-voltage",
	.id			= 0,
	.dev			= {
		.platform_data	= &mmc0_fixed_voltage_config,
	},
};

/* SD */
static struct s3c_sdhci_platdata universal_hsmmc2_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_DISABLE,
	.ext_cd_gpio		= S5PV310_GPX3(4),      /* XEINT_28 */
	.ext_cd_gpio_invert	= 1,
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

/* WiFi */
static struct s3c_sdhci_platdata universal_hsmmc3_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_DISABLE,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
};

static void __init universal_sdhci_init(void)
{
	s3c_sdhci0_set_platdata(&universal_hsmmc0_data);
	s3c_sdhci2_set_platdata(&universal_hsmmc2_data);
	s3c_sdhci3_set_platdata(&universal_hsmmc3_data);
}

/* I2C0 */
static struct i2c_board_info i2c0_devs[] __initdata = {
	/* Camera, To be updated */
};

/* I2C1 */
static struct i2c_board_info i2c1_devs[] __initdata = {
	/* Gyro, To be updated */
};

static struct platform_device *universal_devices[] __initdata = {
	/* Samsung Platform Devices */
	&mmc0_fixed_voltage,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,

	/* Universal Devices */
	&universal_gpio_keys,
	&s5p_device_onenand,
};

static void __init universal_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(universal_uartcfgs, ARRAY_SIZE(universal_uartcfgs));
}

static void __init universal_machine_init(void)
{
	universal_sdhci_init();

	i2c_register_board_info(0, i2c0_devs, ARRAY_SIZE(i2c0_devs));
	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));

	/* Last */
	platform_add_devices(universal_devices, ARRAY_SIZE(universal_devices));
}

MACHINE_START(UNIVERSAL_C210, "UNIVERSAL_C210")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv310_init_irq,
	.map_io		= universal_map_io,
	.init_machine	= universal_machine_init,
	.timer		= &s5pv310_timer,
MACHINE_END
