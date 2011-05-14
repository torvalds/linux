/* linux/arch/arm/mach-exynos4/mach-universal_c210.c
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
#include <linux/mfd/max8998.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/max8952.h>
#include <linux/mmc/host.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/exynos4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/iic.h>
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

static struct regulator_consumer_supply max8952_consumer =
	REGULATOR_SUPPLY("vddarm", NULL);

static struct max8952_platform_data universal_max8952_pdata __initdata = {
	.gpio_vid0	= EXYNOS4_GPX0(3),
	.gpio_vid1	= EXYNOS4_GPX0(4),
	.gpio_en	= -1, /* Not controllable, set "Always High" */
	.default_mode	= 0, /* vid0 = 0, vid1 = 0 */
	.dvs_mode	= { 48, 32, 28, 18 }, /* 1.25, 1.20, 1.05, 0.95V */
	.sync_freq	= 0, /* default: fastest */
	.ramp_speed	= 0, /* default: fastest */

	.reg_data	= {
		.constraints	= {
			.name		= "VARM_1.2V",
			.min_uV		= 770000,
			.max_uV		= 1400000,
			.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
			.always_on	= 1,
			.boot_on	= 1,
		},
		.num_consumer_supplies	= 1,
		.consumer_supplies	= &max8952_consumer,
	},
};

static struct regulator_consumer_supply lp3974_buck1_consumer =
	REGULATOR_SUPPLY("vddint", NULL);

static struct regulator_consumer_supply lp3974_buck2_consumer =
	REGULATOR_SUPPLY("vddg3d", NULL);

static struct regulator_init_data lp3974_buck1_data = {
	.constraints	= {
		.name		= "VINT_1.1V",
		.min_uV		= 750000,
		.max_uV		= 1500000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_STATUS,
		.boot_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &lp3974_buck1_consumer,
};

static struct regulator_init_data lp3974_buck2_data = {
	.constraints	= {
		.name		= "VG3D_1.1V",
		.min_uV		= 750000,
		.max_uV		= 1500000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_STATUS,
		.boot_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &lp3974_buck2_consumer,
};

static struct regulator_init_data lp3974_buck3_data = {
	.constraints	= {
		.name		= "VCC_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_buck4_data = {
	.constraints	= {
		.name		= "VMEM_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo2_data = {
	.constraints	= {
		.name		= "VALIVE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo3_data = {
	.constraints	= {
		.name		= "VUSB+MIPI_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo4_data = {
	.constraints	= {
		.name		= "VADC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo5_data = {
	.constraints	= {
		.name		= "VTF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo6_data = {
	.constraints	= {
		.name		= "LDO6",
		.min_uV		= 2000000,
		.max_uV		= 2000000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo7_data = {
	.constraints	= {
		.name		= "VLCD+VMIPI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo8_data = {
	.constraints	= {
		.name		= "VUSB+VDAC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo9_data = {
	.constraints	= {
		.name		= "VCC_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo10_data = {
	.constraints	= {
		.name		= "VPLL_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.boot_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo11_data = {
	.constraints	= {
		.name		= "CAM_AF_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo12_data = {
	.constraints	= {
		.name		= "PS_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo13_data = {
	.constraints	= {
		.name		= "VHIC_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo14_data = {
	.constraints	= {
		.name		= "CAM_I_HOST_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo15_data = {
	.constraints	= {
		.name		= "CAM_S_DIG+FM33_CORE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo16_data = {
	.constraints	= {
		.name		= "CAM_S_ANA_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_ldo17_data = {
	.constraints	= {
		.name		= "VCC_3.0V_LCD",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.boot_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_32khz_ap_data = {
	.constraints	= {
		.name		= "32KHz AP",
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_32khz_cp_data = {
	.constraints	= {
		.name		= "32KHz CP",
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_vichg_data = {
	.constraints	= {
		.name		= "VICHG",
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_esafeout1_data = {
	.constraints	= {
		.name		= "SAFEOUT1",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data lp3974_esafeout2_data = {
	.constraints	= {
		.name		= "SAFEOUT2",
		.boot_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct max8998_regulator_data lp3974_regulators[] = {
	{ MAX8998_LDO2,  &lp3974_ldo2_data },
	{ MAX8998_LDO3,  &lp3974_ldo3_data },
	{ MAX8998_LDO4,  &lp3974_ldo4_data },
	{ MAX8998_LDO5,  &lp3974_ldo5_data },
	{ MAX8998_LDO6,  &lp3974_ldo6_data },
	{ MAX8998_LDO7,  &lp3974_ldo7_data },
	{ MAX8998_LDO8,  &lp3974_ldo8_data },
	{ MAX8998_LDO9,  &lp3974_ldo9_data },
	{ MAX8998_LDO10, &lp3974_ldo10_data },
	{ MAX8998_LDO11, &lp3974_ldo11_data },
	{ MAX8998_LDO12, &lp3974_ldo12_data },
	{ MAX8998_LDO13, &lp3974_ldo13_data },
	{ MAX8998_LDO14, &lp3974_ldo14_data },
	{ MAX8998_LDO15, &lp3974_ldo15_data },
	{ MAX8998_LDO16, &lp3974_ldo16_data },
	{ MAX8998_LDO17, &lp3974_ldo17_data },
	{ MAX8998_BUCK1, &lp3974_buck1_data },
	{ MAX8998_BUCK2, &lp3974_buck2_data },
	{ MAX8998_BUCK3, &lp3974_buck3_data },
	{ MAX8998_BUCK4, &lp3974_buck4_data },
	{ MAX8998_EN32KHZ_AP, &lp3974_32khz_ap_data },
	{ MAX8998_EN32KHZ_CP, &lp3974_32khz_cp_data },
	{ MAX8998_ENVICHG, &lp3974_vichg_data },
	{ MAX8998_ESAFEOUT1, &lp3974_esafeout1_data },
	{ MAX8998_ESAFEOUT2, &lp3974_esafeout2_data },
};

static struct max8998_platform_data universal_lp3974_pdata = {
	.num_regulators		= ARRAY_SIZE(lp3974_regulators),
	.regulators		= lp3974_regulators,
	.buck1_voltage1		= 1100000,	/* INT */
	.buck1_voltage2		= 1000000,
	.buck1_voltage3		= 1100000,
	.buck1_voltage4		= 1000000,
	.buck1_set1		= EXYNOS4_GPX0(5),
	.buck1_set2		= EXYNOS4_GPX0(6),
	.buck2_voltage1		= 1200000,	/* G3D */
	.buck2_voltage2		= 1100000,
	.buck1_default_idx	= 0,
	.buck2_set3		= EXYNOS4_GPE2(0),
	.buck2_default_idx	= 0,
	.wakeup			= true,
};

/* GPIO I2C 5 (PMIC) */
static struct i2c_board_info i2c5_devs[] __initdata = {
	{
		I2C_BOARD_INFO("max8952", 0xC0 >> 1),
		.platform_data	= &universal_max8952_pdata,
	}, {
		I2C_BOARD_INFO("lp3974", 0xCC >> 1),
		.platform_data	= &universal_lp3974_pdata,
	},
};

/* GPIO KEYS */
static struct gpio_keys_button universal_gpio_keys_tables[] = {
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
		.code			= KEY_CONFIG,
		.gpio			= EXYNOS4_GPX2(2),	/* XEINT18 */
		.desc			= "gpio-keys: KEY_CONFIG",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_CAMERA,
		.gpio			= EXYNOS4_GPX2(3),	/* XEINT19 */
		.desc			= "gpio-keys: KEY_CAMERA",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_OK,
		.gpio			= EXYNOS4_GPX3(5),	/* XEINT29 */
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
	.gpio			= EXYNOS4_GPE1(3),
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
	.ext_cd_gpio		= EXYNOS4_GPX3(4),      /* XEINT_28 */
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
	&s3c_device_i2c5,

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

	s3c_i2c5_set_platdata(NULL);
	i2c_register_board_info(5, i2c5_devs, ARRAY_SIZE(i2c5_devs));

	/* Last */
	platform_add_devices(universal_devices, ARRAY_SIZE(universal_devices));
}

MACHINE_START(UNIVERSAL_C210, "UNIVERSAL_C210")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= universal_map_io,
	.init_machine	= universal_machine_init,
	.timer		= &exynos4_timer,
MACHINE_END
