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
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/mfd/max8998.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/max8952.h>
#include <linux/mmc/host.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c/mcs.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/platform_data/s3c-hsotg.h>
#include <drm/exynos_drm.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/iic.h>
#include <plat/gpio-cfg.h>
#include <plat/fb.h>
#include <plat/mfc.h>
#include <plat/sdhci.h>
#include <plat/pd.h>
#include <plat/regs-fb-v4.h>
#include <plat/fimc-core.h>
#include <plat/s5p-time.h>
#include <plat/camport.h>
#include <plat/mipi_csis.h>

#include <mach/map.h>

#include <media/v4l2-mediabus.h>
#include <media/s5p_fimc.h>
#include <media/m5mols.h>
#include <media/s5k6aa.h>

#include "common.h"

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
	REGULATOR_SUPPLY("vdd_arm", NULL);

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
	REGULATOR_SUPPLY("vdd_int", NULL);

static struct regulator_consumer_supply lp3974_buck2_consumer =
	REGULATOR_SUPPLY("vddg3d", NULL);

static struct regulator_consumer_supply lp3974_buck3_consumer[] = {
	REGULATOR_SUPPLY("vdet", "s5p-sdo"),
	REGULATOR_SUPPLY("vdd_reg", "0-003c"),
};

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
	.num_consumer_supplies = ARRAY_SIZE(lp3974_buck3_consumer),
	.consumer_supplies = lp3974_buck3_consumer,
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

static struct regulator_consumer_supply lp3974_ldo3_consumer[] = {
	REGULATOR_SUPPLY("vusb_a", "s3c-hsotg"),
	REGULATOR_SUPPLY("vdd", "exynos4-hdmi"),
	REGULATOR_SUPPLY("vdd_pll", "exynos4-hdmi"),
	REGULATOR_SUPPLY("vdd11", "s5p-mipi-csis.0"),
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
	.num_consumer_supplies = ARRAY_SIZE(lp3974_ldo3_consumer),
	.consumer_supplies = lp3974_ldo3_consumer,
};

static struct regulator_consumer_supply lp3974_ldo4_consumer[] = {
	REGULATOR_SUPPLY("vdd_osc", "exynos4-hdmi"),
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
	.num_consumer_supplies = ARRAY_SIZE(lp3974_ldo4_consumer),
	.consumer_supplies = lp3974_ldo4_consumer,
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

static struct regulator_consumer_supply lp3974_ldo7_consumer[] = {
	REGULATOR_SUPPLY("vdd18", "s5p-mipi-csis.0"),
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
	.num_consumer_supplies	= ARRAY_SIZE(lp3974_ldo7_consumer),
	.consumer_supplies	= lp3974_ldo7_consumer,
};

static struct regulator_consumer_supply lp3974_ldo8_consumer[] = {
	REGULATOR_SUPPLY("vusb_d", "s3c-hsotg"),
	REGULATOR_SUPPLY("vdd33a_dac", "s5p-sdo"),
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
	.num_consumer_supplies = ARRAY_SIZE(lp3974_ldo8_consumer),
	.consumer_supplies = lp3974_ldo8_consumer,
};

static struct regulator_consumer_supply lp3974_ldo9_consumer =
	REGULATOR_SUPPLY("vddio", "0-003c");

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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &lp3974_ldo9_consumer,
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

static struct regulator_consumer_supply lp3974_ldo11_consumer =
	REGULATOR_SUPPLY("dig_28", "0-001f");

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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &lp3974_ldo11_consumer,
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

static struct regulator_consumer_supply lp3974_ldo14_consumer =
	REGULATOR_SUPPLY("dig_18", "0-001f");

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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &lp3974_ldo14_consumer,
};


static struct regulator_consumer_supply lp3974_ldo15_consumer =
	REGULATOR_SUPPLY("dig_12", "0-001f");

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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &lp3974_ldo15_consumer,
};

static struct regulator_consumer_supply lp3974_ldo16_consumer[] = {
	REGULATOR_SUPPLY("vdda", "0-003c"),
	REGULATOR_SUPPLY("a_sensor", "0-001f"),
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
	.num_consumer_supplies	= ARRAY_SIZE(lp3974_ldo16_consumer),
	.consumer_supplies	= lp3974_ldo16_consumer,
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
		.min_uV		= 4800000,
		.max_uV		= 4800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.always_on	= 1,
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


enum fixed_regulator_id {
	FIXED_REG_ID_MMC0,
	FIXED_REG_ID_HDMI_5V,
	FIXED_REG_ID_CAM_S_IF,
	FIXED_REG_ID_CAM_I_CORE,
	FIXED_REG_ID_CAM_VT_DIO,
};

static struct regulator_consumer_supply hdmi_fixed_consumer =
	REGULATOR_SUPPLY("hdmi-en", "exynos4-hdmi");

static struct regulator_init_data hdmi_fixed_voltage_init_data = {
	.constraints		= {
		.name		= "HDMI_5V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &hdmi_fixed_consumer,
};

static struct fixed_voltage_config hdmi_fixed_voltage_config = {
	.supply_name		= "HDMI_EN1",
	.microvolts		= 5000000,
	.gpio			= EXYNOS4_GPE0(1),
	.enable_high		= true,
	.init_data		= &hdmi_fixed_voltage_init_data,
};

static struct platform_device hdmi_fixed_voltage = {
	.name			= "reg-fixed-voltage",
	.id			= FIXED_REG_ID_HDMI_5V,
	.dev			= {
		.platform_data	= &hdmi_fixed_voltage_config,
	},
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

/* I2C3 (TSP) */
static struct mxt_platform_data qt602240_platform_data = {
	.x_line		= 19,
	.y_line		= 11,
	.x_size		= 800,
	.y_size		= 480,
	.blen		= 0x11,
	.threshold	= 0x28,
	.voltage	= 2800000,		/* 2.8V */
	.orient		= MXT_DIAGONAL,
	.irqflags	= IRQF_TRIGGER_FALLING,
};

static struct i2c_board_info i2c3_devs[] __initdata = {
	{
		I2C_BOARD_INFO("qt602240_ts", 0x4a),
		.platform_data = &qt602240_platform_data,
	},
};

static void __init universal_tsp_init(void)
{
	int gpio;

	/* TSP_LDO_ON: XMDMADDR_11 */
	gpio = EXYNOS4_GPE2(3);
	gpio_request_one(gpio, GPIOF_OUT_INIT_HIGH, "TSP_LDO_ON");
	gpio_export(gpio, 0);

	/* TSP_INT: XMDMADDR_7 */
	gpio = EXYNOS4_GPE1(7);
	gpio_request(gpio, "TSP_INT");

	s5p_register_gpio_interrupt(gpio);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	i2c3_devs[0].irq = gpio_to_irq(gpio);
}


/* GPIO I2C 12 (3 Touchkey) */
static uint32_t touchkey_keymap[] = {
	/* MCS_KEY_MAP(value, keycode) */
	MCS_KEY_MAP(0, KEY_MENU),		/* KEY_SEND */
	MCS_KEY_MAP(1, KEY_BACK),		/* KEY_END */
};

static struct mcs_platform_data touchkey_data = {
	.keymap		= touchkey_keymap,
	.keymap_size	= ARRAY_SIZE(touchkey_keymap),
	.key_maxval	= 2,
};

/* GPIO I2C 3_TOUCH 2.8V */
#define I2C_GPIO_BUS_12		12
static struct i2c_gpio_platform_data i2c_gpio12_data = {
	.sda_pin	= EXYNOS4_GPE4(0),	/* XMDMDATA_8 */
	.scl_pin	= EXYNOS4_GPE4(1),	/* XMDMDATA_9 */
};

static struct platform_device i2c_gpio12 = {
	.name		= "i2c-gpio",
	.id		= I2C_GPIO_BUS_12,
	.dev		= {
		.platform_data	= &i2c_gpio12_data,
	},
};

static struct i2c_board_info i2c_gpio12_devs[] __initdata = {
	{
		I2C_BOARD_INFO("mcs5080_touchkey", 0x20),
		.platform_data = &touchkey_data,
	},
};

static void __init universal_touchkey_init(void)
{
	int gpio;

	gpio = EXYNOS4_GPE3(7);			/* XMDMDATA_7 */
	gpio_request(gpio, "3_TOUCH_INT");
	s5p_register_gpio_interrupt(gpio);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	i2c_gpio12_devs[0].irq = gpio_to_irq(gpio);

	gpio = EXYNOS4_GPE3(3);			/* XMDMDATA_3 */
	gpio_request_one(gpio, GPIOF_OUT_INIT_HIGH, "3_TOUCH_EN");
}

static struct s3c2410_platform_i2c universal_i2c0_platdata __initdata = {
	.frequency	= 300 * 1000,
	.sda_delay	= 200,
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
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
	.host_caps2		= MMC_CAP2_BROKEN_VOLTAGE,
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
};

static struct regulator_consumer_supply mmc0_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "exynos4-sdhci.0"),
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
	.id			= FIXED_REG_ID_MMC0,
	.dev			= {
		.platform_data	= &mmc0_fixed_voltage_config,
	},
};

/* SD */
static struct s3c_sdhci_platdata universal_hsmmc2_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.ext_cd_gpio		= EXYNOS4_GPX3(4),      /* XEINT_28 */
	.ext_cd_gpio_invert	= 1,
	.cd_type		= S3C_SDHCI_CD_GPIO,
};

/* WiFi */
static struct s3c_sdhci_platdata universal_hsmmc3_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
};

static void __init universal_sdhci_init(void)
{
	s3c_sdhci0_set_platdata(&universal_hsmmc0_data);
	s3c_sdhci2_set_platdata(&universal_hsmmc2_data);
	s3c_sdhci3_set_platdata(&universal_hsmmc3_data);
}

/* I2C1 */
static struct i2c_board_info i2c1_devs[] __initdata = {
	/* Gyro, To be updated */
};

#ifdef CONFIG_DRM_EXYNOS
static struct exynos_drm_fimd_pdata drm_fimd_pdata = {
	.panel = {
		.timing	= {
			.left_margin	= 16,
			.right_margin	= 16,
			.upper_margin	= 2,
			.lower_margin	= 28,
			.hsync_len	= 2,
			.vsync_len	= 1,
			.xres		= 480,
			.yres		= 800,
			.refresh	= 55,
		},
	},
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB |
			  VIDCON0_CLKSEL_LCD,
	.vidcon1	= VIDCON1_INV_VCLK | VIDCON1_INV_VDEN
			  | VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.default_win	= 3,
	.bpp		= 32,
};
#else
/* Frame Buffer */
static struct s3c_fb_pd_win universal_fb_win0 = {
	.max_bpp	= 32,
	.default_bpp	= 16,
	.xres		= 480,
	.yres		= 800,
	.virtual_x	= 480,
	.virtual_y	= 2 * 800,
};

static struct fb_videomode universal_lcd_timing = {
	.left_margin	= 16,
	.right_margin	= 16,
	.upper_margin	= 2,
	.lower_margin	= 28,
	.hsync_len	= 2,
	.vsync_len	= 1,
	.xres		= 480,
	.yres		= 800,
	.refresh	= 55,
};

static struct s3c_fb_platdata universal_lcd_pdata __initdata = {
	.win[0]		= &universal_fb_win0,
	.vtiming	= &universal_lcd_timing,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB |
			  VIDCON0_CLKSEL_LCD,
	.vidcon1	= VIDCON1_INV_VCLK | VIDCON1_INV_VDEN
			  | VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};
#endif

static struct regulator_consumer_supply cam_vt_dio_supply =
	REGULATOR_SUPPLY("vdd_core", "0-003c");

static struct regulator_init_data cam_vt_dio_reg_init_data = {
	.constraints = { .valid_ops_mask = REGULATOR_CHANGE_STATUS },
	.num_consumer_supplies = 1,
	.consumer_supplies = &cam_vt_dio_supply,
};

static struct fixed_voltage_config cam_vt_dio_fixed_voltage_cfg = {
	.supply_name	= "CAM_VT_D_IO",
	.microvolts	= 2800000,
	.gpio		= EXYNOS4_GPE2(1), /* CAM_PWR_EN2 */
	.enable_high	= 1,
	.init_data	= &cam_vt_dio_reg_init_data,
};

static struct platform_device cam_vt_dio_fixed_reg_dev = {
	.name = "reg-fixed-voltage", .id = FIXED_REG_ID_CAM_VT_DIO,
	.dev = { .platform_data	= &cam_vt_dio_fixed_voltage_cfg },
};

static struct regulator_consumer_supply cam_i_core_supply =
	REGULATOR_SUPPLY("core", "0-001f");

static struct regulator_init_data cam_i_core_reg_init_data = {
	.constraints = { .valid_ops_mask = REGULATOR_CHANGE_STATUS },
	.num_consumer_supplies = 1,
	.consumer_supplies = &cam_i_core_supply,
};

static struct fixed_voltage_config cam_i_core_fixed_voltage_cfg = {
	.supply_name	= "CAM_I_CORE_1.2V",
	.microvolts	= 1200000,
	.gpio		= EXYNOS4_GPE2(2),	/* CAM_8M_CORE_EN */
	.enable_high	= 1,
	.init_data	= &cam_i_core_reg_init_data,
};

static struct platform_device cam_i_core_fixed_reg_dev = {
	.name = "reg-fixed-voltage", .id = FIXED_REG_ID_CAM_I_CORE,
	.dev = { .platform_data	= &cam_i_core_fixed_voltage_cfg },
};

static struct regulator_consumer_supply cam_s_if_supply =
	REGULATOR_SUPPLY("d_sensor", "0-001f");

static struct regulator_init_data cam_s_if_reg_init_data = {
	.constraints = { .valid_ops_mask = REGULATOR_CHANGE_STATUS },
	.num_consumer_supplies = 1,
	.consumer_supplies = &cam_s_if_supply,
};

static struct fixed_voltage_config cam_s_if_fixed_voltage_cfg = {
	.supply_name	= "CAM_S_IF_1.8V",
	.microvolts	= 1800000,
	.gpio		= EXYNOS4_GPE3(0),	/* CAM_PWR_EN1 */
	.enable_high	= 1,
	.init_data	= &cam_s_if_reg_init_data,
};

static struct platform_device cam_s_if_fixed_reg_dev = {
	.name = "reg-fixed-voltage", .id = FIXED_REG_ID_CAM_S_IF,
	.dev = { .platform_data	= &cam_s_if_fixed_voltage_cfg },
};

static struct s5p_platform_mipi_csis mipi_csis_platdata = {
	.clk_rate	= 166000000UL,
	.lanes		= 2,
	.alignment	= 32,
	.hs_settle	= 12,
	.phy_enable	= s5p_csis_phy_enable,
};

#define GPIO_CAM_LEVEL_EN(n)	EXYNOS4_GPE4(n + 3)
#define GPIO_CAM_8M_ISP_INT	EXYNOS4_GPX1(5)	/* XEINT_13 */
#define GPIO_CAM_MEGA_nRST	EXYNOS4_GPE2(5)
#define GPIO_CAM_VGA_NRST	EXYNOS4_GPE4(7)
#define GPIO_CAM_VGA_NSTBY	EXYNOS4_GPE4(6)

static int s5k6aa_set_power(int on)
{
	gpio_set_value(GPIO_CAM_LEVEL_EN(2), !!on);
	return 0;
}

static struct s5k6aa_platform_data s5k6aa_platdata = {
	.mclk_frequency	= 21600000UL,
	.gpio_reset	= { GPIO_CAM_VGA_NRST, 0 },
	.gpio_stby	= { GPIO_CAM_VGA_NSTBY, 0 },
	.bus_type	= V4L2_MBUS_PARALLEL,
	.horiz_flip	= 1,
	.set_power	= s5k6aa_set_power,
};

static struct i2c_board_info s5k6aa_board_info = {
	I2C_BOARD_INFO("S5K6AA", 0x3C),
	.platform_data = &s5k6aa_platdata,
};

static int m5mols_set_power(struct device *dev, int on)
{
	gpio_set_value(GPIO_CAM_LEVEL_EN(1), !on);
	gpio_set_value(GPIO_CAM_LEVEL_EN(2), !!on);
	return 0;
}

static struct m5mols_platform_data m5mols_platdata = {
	.gpio_reset	= GPIO_CAM_MEGA_nRST,
	.reset_polarity	= 0,
	.set_power	= m5mols_set_power,
};

static struct i2c_board_info m5mols_board_info = {
	I2C_BOARD_INFO("M5MOLS", 0x1F),
	.platform_data = &m5mols_platdata,
};

static struct s5p_fimc_isp_info universal_camera_sensors[] = {
	{
		.mux_id		= 0,
		.flags		= V4L2_MBUS_PCLK_SAMPLE_FALLING |
				  V4L2_MBUS_VSYNC_ACTIVE_LOW,
		.bus_type	= FIMC_ITU_601,
		.board_info	= &s5k6aa_board_info,
		.i2c_bus_num	= 0,
		.clk_frequency	= 24000000UL,
	}, {
		.mux_id		= 0,
		.flags		= V4L2_MBUS_PCLK_SAMPLE_FALLING |
				  V4L2_MBUS_VSYNC_ACTIVE_LOW,
		.bus_type	= FIMC_MIPI_CSI2,
		.board_info	= &m5mols_board_info,
		.i2c_bus_num	= 0,
		.clk_frequency	= 24000000UL,
		.csi_data_align	= 32,
	},
};

static struct s5p_platform_fimc fimc_md_platdata = {
	.isp_info	= universal_camera_sensors,
	.num_clients	= ARRAY_SIZE(universal_camera_sensors),
};

static struct gpio universal_camera_gpios[] = {
	{ GPIO_CAM_LEVEL_EN(1),	GPIOF_OUT_INIT_HIGH, "CAM_LVL_EN1" },
	{ GPIO_CAM_LEVEL_EN(2),	GPIOF_OUT_INIT_LOW,  "CAM_LVL_EN2" },
	{ GPIO_CAM_8M_ISP_INT,	GPIOF_IN,            "8M_ISP_INT"  },
	{ GPIO_CAM_MEGA_nRST,	GPIOF_OUT_INIT_LOW,  "CAM_8M_NRST" },
	{ GPIO_CAM_VGA_NRST,	GPIOF_OUT_INIT_LOW,  "CAM_VGA_NRST"  },
	{ GPIO_CAM_VGA_NSTBY,	GPIOF_OUT_INIT_LOW,  "CAM_VGA_NSTBY" },
};

/* USB OTG */
static struct s3c_hsotg_plat universal_hsotg_pdata;

static void __init universal_camera_init(void)
{
	s3c_set_platdata(&mipi_csis_platdata, sizeof(mipi_csis_platdata),
			 &s5p_device_mipi_csis0);
	s3c_set_platdata(&fimc_md_platdata,  sizeof(fimc_md_platdata),
			 &s5p_device_fimc_md);

	if (gpio_request_array(universal_camera_gpios,
			       ARRAY_SIZE(universal_camera_gpios))) {
		pr_err("%s: GPIO request failed\n", __func__);
		return;
	}

	if (!s3c_gpio_cfgpin(GPIO_CAM_8M_ISP_INT, S3C_GPIO_SFN(0xf)))
		m5mols_board_info.irq = gpio_to_irq(GPIO_CAM_8M_ISP_INT);
	else
		pr_err("Failed to configure 8M_ISP_INT GPIO\n");

	/* Free GPIOs controlled directly by the sensor drivers. */
	gpio_free(GPIO_CAM_MEGA_nRST);
	gpio_free(GPIO_CAM_8M_ISP_INT);
	gpio_free(GPIO_CAM_VGA_NRST);
	gpio_free(GPIO_CAM_VGA_NSTBY);

	if (exynos4_fimc_setup_gpio(S5P_CAMPORT_A))
		pr_err("Camera port A setup failed\n");
}

static struct platform_device *universal_devices[] __initdata = {
	/* Samsung Platform Devices */
	&s5p_device_mipi_csis0,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5p_device_fimc3,
	&s5p_device_g2d,
	&mmc0_fixed_voltage,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_i2c0,
	&s3c_device_i2c3,
	&s3c_device_i2c5,
	&s5p_device_i2c_hdmiphy,
	&hdmi_fixed_voltage,
	&s5p_device_hdmi,
	&s5p_device_sdo,
	&s5p_device_mixer,

	/* Universal Devices */
	&i2c_gpio12,
	&universal_gpio_keys,
	&s5p_device_onenand,
	&s5p_device_fimd0,
	&s5p_device_jpeg,
#ifdef CONFIG_DRM_EXYNOS
	&exynos_device_drm,
#endif
	&s3c_device_usb_hsotg,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,
	&cam_vt_dio_fixed_reg_dev,
	&cam_i_core_fixed_reg_dev,
	&cam_s_if_fixed_reg_dev,
	&s5p_device_fimc_md,
};

static void __init universal_map_io(void)
{
	clk_xusbxti.rate = 24000000;
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(universal_uartcfgs, ARRAY_SIZE(universal_uartcfgs));
	s5p_set_timer_source(S5P_PWM2, S5P_PWM4);
}

static void s5p_tv_setup(void)
{
	/* direct HPD to HDMI chip */
	gpio_request_one(EXYNOS4_GPX3(7), GPIOF_IN, "hpd-plug");
	s3c_gpio_cfgpin(EXYNOS4_GPX3(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(EXYNOS4_GPX3(7), S3C_GPIO_PULL_NONE);
}

static void __init universal_reserve(void)
{
	s5p_mfc_reserve_mem(0x43000000, 8 << 20, 0x51000000, 8 << 20);
}

static void __init universal_machine_init(void)
{
	universal_sdhci_init();
	s5p_tv_setup();

	s3c_i2c0_set_platdata(&universal_i2c0_platdata);
	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));

	universal_tsp_init();
	s3c_i2c3_set_platdata(NULL);
	i2c_register_board_info(3, i2c3_devs, ARRAY_SIZE(i2c3_devs));

	s3c_i2c5_set_platdata(NULL);
	s5p_i2c_hdmiphy_set_platdata(NULL);
	i2c_register_board_info(5, i2c5_devs, ARRAY_SIZE(i2c5_devs));

#ifdef CONFIG_DRM_EXYNOS
	s5p_device_fimd0.dev.platform_data = &drm_fimd_pdata;
	exynos4_fimd0_gpio_setup_24bpp();
#else
	s5p_fimd0_set_platdata(&universal_lcd_pdata);
#endif

	universal_touchkey_init();
	i2c_register_board_info(I2C_GPIO_BUS_12, i2c_gpio12_devs,
			ARRAY_SIZE(i2c_gpio12_devs));

	s3c_hsotg_set_platdata(&universal_hsotg_pdata);
	universal_camera_init();

	/* Last */
	platform_add_devices(universal_devices, ARRAY_SIZE(universal_devices));
}

MACHINE_START(UNIVERSAL_C210, "UNIVERSAL_C210")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= universal_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= universal_machine_init,
	.init_late	= exynos_init_late,
	.timer		= &s5p_timer,
	.reserve        = &universal_reserve,
	.restart	= exynos4_restart,
MACHINE_END
