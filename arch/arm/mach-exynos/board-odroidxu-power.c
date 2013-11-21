/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/gpio.h>
#include <linux/platform_data/exynos_thermal.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/system.h>

#include <plat/devs.h>
#include <plat/iic.h>
#include <plat/gpio-cfg.h>

#include <mach/regs-pmu.h>
#include <mach/irqs.h>
#include <mach/hs-iic.h>
#include <mach/devfreq.h>
#include <mach/tmu.h>

#if defined(CONFIG_MFD_MAX77802)
#include <linux/mfd/max77802.h>
#endif

#include "common.h"
#include "board-odroidxu.h"

#if defined(CONFIG_USB_HSIC_USB3503)
#include <linux/platform_data/usb3503.h>

static struct usb3503_platform_data usb3503_pdata = {
	.initial_mode	= 	USB3503_MODE_HUB,
	.ref_clk		= 	USB3503_REFCLK_24M,
	.gpio_intn		= 	EXYNOS5410_GPX0(7),
	.gpio_connect	= 	EXYNOS5410_GPX0(6),
	.gpio_reset		= 	EXYNOS5410_GPX1(4),
};
#endif

#if defined(CONFIG_REGULATOR_MAX77802)
/* max77802 */
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
static struct regulator_consumer_supply max77802_buck1 =
	REGULATOR_SUPPLY("vdd_mif", NULL);

static struct regulator_consumer_supply max77802_buck2 =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply max77802_buck3 =
	REGULATOR_SUPPLY("vdd_int", NULL);

static struct regulator_consumer_supply max77802_buck4 =
	REGULATOR_SUPPLY("vdd_g3d", NULL);

static struct regulator_consumer_supply max77802_buck5 =
	REGULATOR_SUPPLY("vdd_mem2", NULL);

static struct regulator_consumer_supply max77802_buck6 =
	REGULATOR_SUPPLY("vdd_kfc", NULL);

static struct regulator_consumer_supply max77802_buck8 =
    REGULATOR_SUPPLY("vmmc", "dw_mmc.0");


//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
static struct regulator_consumer_supply ldo1_supply[] = {
	REGULATOR_SUPPLY("vdd_alive", NULL),
};

static struct regulator_consumer_supply ldo2_supply[] = {
	REGULATOR_SUPPLY("vddq_M1_M2", NULL),
};

static struct regulator_consumer_supply ldo3_supply[] = {
	REGULATOR_SUPPLY("vddq_gpio", NULL),
};

static struct regulator_consumer_supply ldo4_supply[] = {
	REGULATOR_SUPPLY("vqmmc", "dw_mmc.2"),
};

static struct regulator_consumer_supply ldo5_supply[] = {
	REGULATOR_SUPPLY("vdd18_hsic", NULL),
};

static struct regulator_consumer_supply ldo6_supply[] = {
	REGULATOR_SUPPLY("vdd18_BPLL", NULL),
};

static struct regulator_consumer_supply ldo7_supply[] = {
	REGULATOR_SUPPLY("vddq_lcd", NULL),
};

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vdd10_hdmi", NULL),
};

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vdd18_mipi", NULL),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("vddq_mmc01", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("vdd33_USB30", NULL),
};

static struct regulator_consumer_supply ldo13_supply[] = {
	REGULATOR_SUPPLY("vddq_abb0", NULL),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vddq_abb1", NULL),
};

static struct regulator_consumer_supply ldo15_supply[] = {
	REGULATOR_SUPPLY("vdd10_USB30", NULL),
};

static struct regulator_consumer_supply ldo17_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_core", NULL),
};

static struct regulator_consumer_supply ldo18_supply[] = {
	REGULATOR_SUPPLY("LDO18", NULL),
};

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
static struct regulator_consumer_supply ldo20_supply[] = {
	REGULATOR_SUPPLY("vqmmc", "dw_mmc.0"),
};

static struct regulator_consumer_supply ldo21_supply[] = {
	REGULATOR_SUPPLY("vmmc", "dw_mmc.2"),
};

static struct regulator_consumer_supply ldo23_supply[] = {
	REGULATOR_SUPPLY("DP_P3V3", NULL),
};

static struct regulator_consumer_supply ldo24_supply[] = {
	REGULATOR_SUPPLY("cam_af_2.8v", NULL),
};

static struct regulator_consumer_supply ldo25_supply[] = {
	REGULATOR_SUPPLY("ETH_P3V3", NULL),
};

static struct regulator_consumer_supply ldo26_supply[] = {
	REGULATOR_SUPPLY("USB30_EXTCLK", NULL),
};

static struct regulator_consumer_supply ldo30_supply[] = {
	REGULATOR_SUPPLY("vddq_E12", NULL),
};

static struct regulator_consumer_supply ldo32_supply[] = {
	REGULATOR_SUPPLY("vs_power_meter", NULL),
};


#if 0
static struct regulator_consumer_supply max77802_enp32khz[] = {
	REGULATOR_SUPPLY("lpo_in", "bcm47511"),
	REGULATOR_SUPPLY("lpo", "bcm4334_bluetooth"),
};
#endif

static struct regulator_init_data max77802_buck1_data = {
	.constraints	= {
		.name		= "vdd_mif range",
		.min_uV		=  800000,
		.max_uV		= 1300000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS,
		.always_on = 1,
		.boot_on = 1,
		.state_mem	= {
			.uV		= 1100000,
			.mode		= REGULATOR_MODE_NORMAL,
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies = &max77802_buck1,
};

static struct regulator_init_data max77802_buck2_data = {
	.constraints	= {
		.name		= "vdd_arm range",
		.min_uV		=  800000,
		.max_uV		= 1500000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.always_on = 1,
		.boot_on = 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies = &max77802_buck2,
};

static struct regulator_init_data max77802_buck3_data = {
	.constraints	= {
		.name		= "vdd_int range",
		.min_uV		=  800000,
		.max_uV		= 1400000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS,
		.always_on = 1,
		.boot_on = 1,
		.state_mem	= {
			.uV		= 1100000,
			.mode		= REGULATOR_MODE_NORMAL,
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max77802_buck3,
};

static struct regulator_init_data max77802_buck4_data = {
	.constraints	= {
		.name		= "vdd_g3d range",
		.min_uV		=  800000,
		.max_uV		= 1400000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.always_on = 1,
		.boot_on = 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max77802_buck4,
};

static struct regulator_init_data max77802_buck5_data = {
	.constraints	= {
		.name		= "vdd_mem2 range",
		.min_uV		=  800000,
		.max_uV		= 1500000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.always_on = 1,
		.boot_on = 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max77802_buck5,
};

static struct regulator_init_data max77802_buck6_data = {
	.constraints	= {
		.name		= "vdd_kfc range",
		.min_uV		=  800000,
		.max_uV		= 1500000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.always_on = 1,
		.boot_on = 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max77802_buck6,
};

static struct regulator_init_data max77802_buck8_data = {
       .constraints    = {
               .name           = "vdd_emmc range",
               .min_uV         = 2850000,
               .max_uV         = 2850000,
               .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
                                 REGULATOR_CHANGE_STATUS,  
               .always_on = 1,
               .boot_on = 1,  
               .state_mem      = {
                       .disabled       = 1,
               },
       },
       .num_consumer_supplies  = 1,
       .consumer_supplies      = &max77802_buck8,
};
  
static struct regulator_init_data max77802_buck9_data = {
       .constraints    = {
               .name           = "vdd_emmc range",
               .min_uV         = 3000000,
               .max_uV         = 3000000,
               .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
                                 REGULATOR_CHANGE_STATUS,  
               .always_on = 1,
               .boot_on = 1,  
               .state_mem      = {
                       .disabled       = 1,
               },
       },
};


#if 0
static struct regulator_init_data max77802_enp32khz_data = {
	.constraints = {
		.name = "32KHZ_PMIC",
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.enabled	= 1,
			.disabled	= 0,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77802_enp32khz),
	.consumer_supplies = max77802_enp32khz,
};
#endif

#define REGULATOR_INIT(_ldo, _name, _min_uV, _max_uV, _always_on, _ops_mask, \
		       _disabled)					\
	static struct regulator_init_data _ldo##_init_data = {		\
		.constraints = {					\
			.name = _name,					\
			.min_uV = _min_uV,				\
			.max_uV = _max_uV,				\
			.always_on	= _always_on,			\
			.boot_on	= _always_on,			\
			.apply_uV	= 1,				\
			.valid_ops_mask = _ops_mask,			\
			.state_mem = {					\
				.disabled	= _disabled,		\
				.enabled	= !(_disabled),		\
			}						\
		},							\
		.num_consumer_supplies = ARRAY_SIZE(_ldo##_supply),	\
		.consumer_supplies = &_ldo##_supply[0],			\
	};

REGULATOR_INIT(ldo1, "vdd_alive",		1000000, 1000000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo2, "vddq_M1_M2",		1200000, 1200000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo3, "vddq_gpio", 		1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo4, "vdd_mmc2_1v8",   1800000, 3000000, 1, REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo5, "vdd18_hsic", 		1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo6, "vdd18_BPLL", 		1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo7, "vddq_lcd", 		1800000, 1800000, 0, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo8, "vdd10_hdmi",	 	1000000, 1000000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo10, "vdd18_mipi", 	1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo11, "vddq_mmc01", 	1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo12, "vdd33_USB30", 	3300000, 3300000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo13, "vddq_abb0", 		1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo14, "vddq_abb1", 		1800000, 1800000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo15, "vdd10_USB30", 	1000000, 1000000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo18, "LDO18",			1800000, 1800000, 0, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo20, "vdd_emmc_1v8",  1800000, 3000000, 1, REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo21, "VDDF_2V8",		2850000, 2850000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo23, "DP_P3V3", 		3300000, 3300000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo25, "ETH_P3V3",	 	3300000, 3300000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo26, "USB30_EXTCLK", 	3300000, 3300000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo30, "vddq_E12", 		1200000, 1200000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo32, "vs_power_meter", 3300000, 3300000, 1, REGULATOR_CHANGE_STATUS, 0);

#if defined(CONFIG_VIDEO_S5K4ECGX)
REGULATOR_INIT(ldo17, "cam_sensor_core",1200000, 1200000, 1, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo24, "cam_af_2.8v",	2800000, 2800000, 1, REGULATOR_CHANGE_STATUS, 0);
#else
REGULATOR_INIT(ldo17, "cam_sensor_core",1200000, 1200000, 0, REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo24, "cam_af_2.8v",	2800000, 2800000, 0, REGULATOR_CHANGE_STATUS, 0);
#endif

static struct max77802_regulator_data max77802_regulators[] = {
	{MAX77802_BUCK1, &max77802_buck1_data,},
	{MAX77802_BUCK2, &max77802_buck2_data,},
	{MAX77802_BUCK3, &max77802_buck3_data,},
	{MAX77802_BUCK4, &max77802_buck4_data,},
	{MAX77802_BUCK5, &max77802_buck5_data,},
	{MAX77802_BUCK6, &max77802_buck6_data,},
    {MAX77802_BUCK8, &max77802_buck8_data,},
    {MAX77802_BUCK9, &max77802_buck9_data,},

	{MAX77802_LDO1, &ldo1_init_data,},
	{MAX77802_LDO2, &ldo2_init_data,},
	{MAX77802_LDO3, &ldo3_init_data,},
	{MAX77802_LDO4, &ldo4_init_data,},
	{MAX77802_LDO5, &ldo5_init_data,},
	{MAX77802_LDO6, &ldo6_init_data,},
	{MAX77802_LDO7, &ldo7_init_data,},
	{MAX77802_LDO8, &ldo8_init_data,},
	{MAX77802_LDO10, &ldo10_init_data,},
	{MAX77802_LDO11, &ldo11_init_data,},
	{MAX77802_LDO12, &ldo12_init_data,},
	{MAX77802_LDO13, &ldo13_init_data,},
	{MAX77802_LDO14, &ldo14_init_data,},
	{MAX77802_LDO15, &ldo15_init_data,},
	{MAX77802_LDO17, &ldo17_init_data,},
	{MAX77802_LDO18, &ldo18_init_data,},
	{MAX77802_LDO20, &ldo20_init_data,},
	{MAX77802_LDO21, &ldo21_init_data,},
	{MAX77802_LDO23, &ldo23_init_data,},
	{MAX77802_LDO24, &ldo24_init_data,},
	{MAX77802_LDO25, &ldo25_init_data,},
	{MAX77802_LDO26, &ldo26_init_data,},
	{MAX77802_LDO30, &ldo30_init_data,},
	{MAX77802_LDO32, &ldo32_init_data,},
#if 0
	{MAX77802_P32KH, &max77802_enp32khz_data,},
#endif
};

struct max77802_opmode_data max77802_opmode_data[MAX77802_REG_MAX] = {
	[MAX77802_LDO1] = {MAX77802_LDO1, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO2] = {MAX77802_LDO2, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO3] = {MAX77802_LDO3, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO4] = {MAX77802_LDO4, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO5] = {MAX77802_LDO5, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO6] = {MAX77802_LDO6, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO7] = {MAX77802_LDO7, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO8] = {MAX77802_LDO8, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO10] = {MAX77802_LDO10, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO11] = {MAX77802_LDO11, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO12] = {MAX77802_LDO12, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO13] = {MAX77802_LDO13, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO14] = {MAX77802_LDO14, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO15] = {MAX77802_LDO15, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO17] = {MAX77802_LDO17, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO18] = {MAX77802_LDO18, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO20] = {MAX77802_LDO20, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO21] = {MAX77802_LDO21, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO23] = {MAX77802_LDO23, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO24] = {MAX77802_LDO24, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO25] = {MAX77802_LDO25, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO26] = {MAX77802_LDO26, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO30] = {MAX77802_LDO30, MAX77802_OPMODE_NORMAL},
	[MAX77802_LDO32] = {MAX77802_LDO32, MAX77802_OPMODE_NORMAL},
	[MAX77802_BUCK1] = {MAX77802_BUCK1, MAX77802_OPMODE_NORMAL},
	[MAX77802_BUCK2] = {MAX77802_BUCK2, MAX77802_OPMODE_NORMAL},
	[MAX77802_BUCK3] = {MAX77802_BUCK3, MAX77802_OPMODE_NORMAL},
	[MAX77802_BUCK4] = {MAX77802_BUCK4, MAX77802_OPMODE_NORMAL},
	[MAX77802_BUCK6] = {MAX77802_BUCK6, MAX77802_OPMODE_NORMAL},
    [MAX77802_BUCK8] = {MAX77802_BUCK8, MAX77802_OPMODE_NORMAL},
    [MAX77802_BUCK9] = {MAX77802_BUCK9, MAX77802_OPMODE_NORMAL},

};

struct max77802_platform_data exynos5_max77802_info = {
	.num_regulators = ARRAY_SIZE(max77802_regulators),
	.regulators = max77802_regulators,
	.irq_gpio	= EXYNOS5410_GPX0(4),
	.irq_base	= IRQ_BOARD_START,
	.wakeup		= 1,

	.opmode_data = max77802_opmode_data,
	.ramp_rate = MAX77802_RAMP_RATE_25MV,
	.wtsr_smpl = MAX77802_WTSR_ENABLE | MAX77802_SMPL_ENABLE,

	.buck12346_gpio_dvs = {
		/* Use DVS2 register of each bucks to supply stable power
		 * after sudden reset */
		{EXYNOS5410_GPX0(2), 1},
		{EXYNOS5410_GPX0(1), 0},
		{EXYNOS5410_GPX0(0), 0},
	},
	.buck1_voltage[0] = 1100000,	/* 1.1V */
	.buck1_voltage[1] = 1100000,	/* 1.1V */
	.buck1_voltage[2] = 1100000,	/* 1.1V */
	.buck1_voltage[3] = 1100000,	/* 1.1V */
	.buck1_voltage[4] = 1100000,	/* 1.1V */
	.buck1_voltage[5] = 1100000,	/* 1.1V */
	.buck1_voltage[6] = 1100000,	/* 1.1V */
	.buck1_voltage[7] = 1100000,	/* 1.1V */

	.buck2_voltage[0] = 1100000,	/* 1.1V */
	.buck2_voltage[1] = 1100000,	/* 1.1V */
	.buck2_voltage[2] = 1100000,	/* 1.1V */
	.buck2_voltage[3] = 1100000,	/* 1.1V */
	.buck2_voltage[4] = 1100000,	/* 1.1V */
	.buck2_voltage[5] = 1100000,	/* 1.1V */
	.buck2_voltage[6] = 1100000,	/* 1.1V */
	.buck2_voltage[7] = 1100000,	/* 1.1V */

	.buck3_voltage[0] = 1100000,	/* 1.1V */
	.buck3_voltage[1] = 1000000,	/* 1.0V */
	.buck3_voltage[2] = 1100000,	/* 1.1V */
	.buck3_voltage[3] = 1100000,	/* 1.1V */
	.buck3_voltage[4] = 1100000,	/* 1.1V */
	.buck3_voltage[5] = 1100000,	/* 1.1V */
	.buck3_voltage[6] = 1100000,	/* 1.1V */
	.buck3_voltage[7] = 1100000,	/* 1.1V */

	.buck4_voltage[0] = 1100000,	/* 1.1V */
	.buck4_voltage[1] = 1000000,	/* 1.0V */
	.buck4_voltage[2] = 1100000,	/* 1.1V */
	.buck4_voltage[3] = 1100000,	/* 1.1V */
	.buck4_voltage[4] = 1100000,	/* 1.1V */
	.buck4_voltage[5] = 1100000,	/* 1.1V */
	.buck4_voltage[6] = 1100000,	/* 1.1V */
	.buck4_voltage[7] = 1100000,	/* 1.1V */

	.buck6_voltage[0] = 1100000,	/* 1.1V */
	.buck6_voltage[1] = 1000000,	/* 1.0V */
	.buck6_voltage[2] = 1100000,	/* 1.1V */
	.buck6_voltage[3] = 1100000,	/* 1.1V */
	.buck6_voltage[4] = 1100000,	/* 1.1V */
	.buck6_voltage[5] = 1100000,	/* 1.1V */
	.buck6_voltage[6] = 1100000,	/* 1.1V */
	.buck6_voltage[7] = 1100000,	/* 1.1V */
};
#endif

#if defined(CONFIG_ODROIDXU_SP)
#include <linux/platform_data/ina231.h>

static  struct ina231_pd  sensor_arm = {
    .name           = "sensor_arm",
    .enable         = 0,
    .max_A          = 9,    // unit = A
    .shunt_R_mohm   = 10,   // unit = m ohm
    .config         = INA231_CONFIG(VSH_CT(eVSH_CT_8244uS)      | \
                                    VBUS_CT(eVBUS_CT_8244uS)    | \
                                    AVG_BIT(eAVG_16)            | \
                                    eSHUNT_BUS_VOLT_CONTINUOUS),
    .update_period  = CONVERSION_DELAY(eVSH_CON_8244uS, eVBUS_CON_8244uS, eAVG_CON_16),   // unit = usec
};

static  struct ina231_pd  sensor_mem = {
    .name           = "sensor_mem",
    .enable         = 0,
    .max_A          = 3,    // unit = A
    .shunt_R_mohm   = 10,   // unit = m ohm
    .config         = INA231_CONFIG(VSH_CT(eVSH_CT_8244uS)      | \
                                    VBUS_CT(eVBUS_CT_8244uS)    | \
                                    AVG_BIT(eAVG_16)            | \
                                    eSHUNT_BUS_VOLT_CONTINUOUS),
    .update_period  = CONVERSION_DELAY(eVSH_CON_8244uS, eVBUS_CON_8244uS, eAVG_CON_16),   // unit = usec
};

static  struct ina231_pd  sensor_kfc = {
    .name           = "sensor_kfc",
    .enable         = 0,
    .max_A          = 2,    // unit = A
    .shunt_R_mohm   = 10,   // unit = m ohm
    .config         = INA231_CONFIG(VSH_CT(eVSH_CT_8244uS)      | \
                                    VBUS_CT(eVBUS_CT_8244uS)    | \
                                    AVG_BIT(eAVG_16)            | \
                                    eSHUNT_BUS_VOLT_CONTINUOUS),
    .update_period  = CONVERSION_DELAY(eVSH_CON_8244uS, eVBUS_CON_8244uS, eAVG_CON_16),   // unit = usec
};

static  struct ina231_pd  sensor_g3d = {
    .name           = "sensor_g3d",
    .enable         = 0,
    .max_A          = 5,    // unit = A
    .shunt_R_mohm   = 10,   // unit = m ohm
    .config         = INA231_CONFIG(VSH_CT(eVSH_CT_8244uS)      | \
                                    VBUS_CT(eVBUS_CT_8244uS)    | \
                                    AVG_BIT(eAVG_16)            | \
                                    eSHUNT_BUS_VOLT_CONTINUOUS),
    .update_period  = CONVERSION_DELAY(eVSH_CON_8244uS, eVBUS_CON_8244uS, eAVG_CON_16),   // unit = usec
};
#endif

static struct i2c_board_info hs_i2c_devs0[] __initdata = {
	{
		I2C_BOARD_INFO("max77802", 0x12 >> 1),
		.platform_data = &exynos5_max77802_info,
	},
#if defined(CONFIG_USB_HSIC_USB3503)
	{
		I2C_BOARD_INFO("usb3503", (0x10 >> 1)),
		.platform_data	= &usb3503_pdata,
	},
#endif
#if defined(CONFIG_ODROIDXU_SP)
	{
		I2C_BOARD_INFO(INA231_I2C_NAME, (0x80 >> 1)),
		.platform_data = &sensor_arm,
	},
	{
		I2C_BOARD_INFO(INA231_I2C_NAME, (0x82 >> 1)),
		.platform_data = &sensor_mem,
	},
	{
		I2C_BOARD_INFO(INA231_I2C_NAME, (0x88 >> 1)),
		.platform_data = &sensor_g3d,
	},
	{
		I2C_BOARD_INFO(INA231_I2C_NAME, (0x8A >> 1)),
		.platform_data = &sensor_kfc,
	},
#endif

};

#ifdef CONFIG_BATTERY_SAMSUNG
static struct platform_device samsung_device_battery = {
	.name	= "samsung-fake-battery",
	.id	= -1,
};
#endif

#ifdef CONFIG_PM_DEVFREQ
static struct platform_device exynos5_mif_devfreq = {
	.name	= "exynos5-busfreq-mif",
	.id	= -1,
};

static struct platform_device exynos5_int_devfreq = {
	.name	= "exynos5-busfreq-int",
	.id	= -1,
};

static struct exynos_devfreq_platdata smdk5410_qos_mif_pd __initdata = {
	.default_qos = 160000,
};

static struct exynos_devfreq_platdata smdk5410_qos_int_pd __initdata = {
	.default_qos = 100000,
};
#endif

#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
static struct exynos_tmu_platform_data exynos5_tmu_data = {
	.trigger_levels[0] = 105,
	.trigger_levels[1] = 110,
	.trigger_levels[2] = 115,
	.trigger_levels[3] = 120,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 1,
	.gain = 5,
	.reference_voltage = 16,
	.noise_cancel_mode = 4,
	.cal_type = TYPE_ONE_POINT_TRIMMING,
	.efuse_value = 55,
	.freq_tab[0] = {
		.freq_clip_max = 1600 * 1000,
		.temp_level = 100,
	},
	.freq_tab[1] = {
		.freq_clip_max = 1400 * 1000,
		.temp_level = 105,
	},
	.freq_tab[2] = {
		.freq_clip_max = 1000 * 1000,
		.temp_level = 110,
	},
	.freq_tab[3] = {
		.freq_clip_max = 400 * 1000,
		.temp_level = 115,
	},
	.freq_tab[4] = {
		.freq_clip_max = 200 * 1000,
		.temp_level = 120,
	},
	.size[THERMAL_TRIP_ACTIVE] = 1,
	.size[THERMAL_TRIP_PASSIVE] = 3,
	.size[THERMAL_TRIP_HOT] = 1,
	.freq_tab_count = 5,
	.type = SOC_ARCH_EXYNOS5,
};
#else
static struct exynos_tmu_platform_data exynos5_tmu_data = {
	.trigger_levels[0] = 85,
	.trigger_levels[1] = 90,
	.trigger_levels[2] = 110,
	.trigger_levels[3] = 105,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 1,
	.gain = 5,
	.reference_voltage = 16,
	.noise_cancel_mode = 4,
	.cal_type = TYPE_ONE_POINT_TRIMMING,
	.efuse_value = 55,
	.freq_tab[0] = {
		.freq_clip_max = 1600 * 1000,
		.temp_level = 85,
	},
	.freq_tab[1] = {
		.freq_clip_max = 1400 * 1000,
		.temp_level = 90,
	},
	.freq_tab[2] = {
		.freq_clip_max = 1200 * 1000,
		.temp_level = 95,
	},
	.freq_tab[3] = {
		.freq_clip_max = 800 * 1000,
		.temp_level = 100,
	},
	.freq_tab[4] = {
		.freq_clip_max = 400 * 1000,
		.temp_level = 110,
	},
	.size[THERMAL_TRIP_ACTIVE] = 1,
	.size[THERMAL_TRIP_PASSIVE] = 3,
	.size[THERMAL_TRIP_HOT] = 1,
	.freq_tab_count = 5,
	.type = SOC_ARCH_EXYNOS5,
};
#endif

struct exynos5_platform_i2c hs_i2c0_data __initdata = {
	.bus_number = 4,
	.operation_mode = HSI2C_POLLING,
	.speed_mode = HSI2C_FAST_SPD,
	.fast_speed = 400000,
	.high_speed = 2500000,
	.cfg_gpio = NULL,
};


#if defined(CONFIG_ODROIDXU_FAN)
#include	<linux/platform_data/odroid_fan.h>
struct odroid_fan_platform_data odroid_fan_pdata = {
	.pwm_gpio = EXYNOS5410_GPB2(0),
	.pwm_func = S3C_GPIO_SFN(2),
	
	.pwm_id = 0,
	.pwm_periode_ns = 20972,		// Freq 22KHz,
	.pwm_duty = 255,				// max=255, 
};

static struct platform_device odroidxu_fan = {
	.name   = "odroidxu-fan",
	.id     = -1,  
	.dev	= {
		.parent = &s3c_device_timer[0].dev,
		.platform_data = &odroid_fan_pdata,
	}
};
#endif

static struct platform_device odroid_sysfs = {
    .name = "odroid-sysfs",
    .id = -1,
};

static struct platform_device *smdk5410_power_devices[] __initdata = {
	&exynos5_device_hs_i2c0,
#ifdef CONFIG_BATTERY_SAMSUNG
	&samsung_device_battery,
#endif
#ifdef CONFIG_PM_DEVFREQ
	&exynos5_mif_devfreq,
	&exynos5_int_devfreq,
#endif
	&exynos5410_device_tmu,
	
#if defined(CONFIG_ODROIDXU_FAN)
	&s3c_device_timer[0],
	&odroidxu_fan,
#endif
	&odroid_sysfs,
};

static void odroidxu_power_off(void)
{
	int poweroff_try = 0;

	local_irq_disable();

	printk("%s : set PS_HOLD low\n", __func__);

	while (1) {
		/* Check reboot charging */
		if (poweroff_try >= 5) {
			/* To enter LP charging */
			writel(0x0, EXYNOS_INFORM2);

			flush_cache_all();
			outer_flush_all();
			exynos5_restart(0, 0);

			pr_emerg("%s: waiting for reboot\n", __func__);
			while (1)
				;
		}
		/* power off code
		 * PS_HOLD Out/High -->
		 * Low PS_HOLD_CONTROL, R/W, 0x1002_330C
		 */
		writel(readl(EXYNOS_PS_HOLD_CONTROL) & 0xFFFFFEFF,
		       EXYNOS_PS_HOLD_CONTROL);

		++poweroff_try;
		printk("%s: Should not reach here! (poweroff_try:%d)\n",__func__, poweroff_try);

		mdelay(1000);
	}
}

void __init exynos5_odroidxu_power_init(void)
{
	exynos5_hs_i2c0_set_platdata(&hs_i2c0_data);
	i2c_register_board_info(4, hs_i2c_devs0, ARRAY_SIZE(hs_i2c_devs0));
#ifdef CONFIG_PM_DEVFREQ
	s3c_set_platdata(&smdk5410_qos_mif_pd, sizeof(struct exynos_devfreq_platdata),
			&exynos5_mif_devfreq);

	s3c_set_platdata(&smdk5410_qos_int_pd, sizeof(struct exynos_devfreq_platdata),
			&exynos5_int_devfreq);
#endif

	s3c_set_platdata(&exynos5_tmu_data, sizeof(struct exynos_tmu_platform_data),
			&exynos5410_device_tmu);

	platform_add_devices(smdk5410_power_devices,
			ARRAY_SIZE(smdk5410_power_devices));

	pm_power_off = odroidxu_power_off;
}
