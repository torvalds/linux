/*
 * naples-power.c - Power Management of MIDAS Project
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  Chiwoong Byun <woong.byun@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-naples.h>
#include <mach/irqs.h>

#include <linux/mfd/max8997.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77693.h>

#if defined(CONFIG_REGULATOR_S5M8767)
#include <linux/mfd/s5m87xx/s5m-pmic.h>
#include <linux/mfd/s5m87xx/s5m-core.h>
#endif

#ifdef CONFIG_REGULATOR_MAX8997
/* MOTOR */
#ifdef CONFIG_VIBETONZ
static void max8997_motor_init(void)
{
	gpio_request(GPIO_VIBTONE_EN, "VIBTONE_EN");
	s3c_gpio_cfgpin(GPIO_VIBTONE_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_VIBTONE_EN, S3C_GPIO_PULL_NONE);
}

static void max8997_motor_en(bool en)
{
	gpio_direction_output(GPIO_VIBTONE_EN, en);
}

static struct max8997_motor_data max8997_motor = {
	.max_timeout = 10000,
	.duty = 44000,
	.period = 44642,
	.reg2 = MOTOR_LRA | EXT_PWM | DIVIDER_128,
	.init_hw = max8997_motor_init,
	.motor_en = max8997_motor_en,
	.pwm_id = 1,
};
#endif

/* max8997 */
static struct regulator_consumer_supply ldo1_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
	REGULATOR_SUPPLY("VDD18", "s5p-mipi-dsim"),
};

#ifdef CONFIG_SND_SOC_WM8994
static struct regulator_consumer_supply ldo6_supply[] = {
	REGULATOR_SUPPLY("AVDD2", NULL),
	REGULATOR_SUPPLY("CPVDD", NULL),
	REGULATOR_SUPPLY("DBVDD1", NULL),
	REGULATOR_SUPPLY("DBVDD2", NULL),
	REGULATOR_SUPPLY("DBVDD3", NULL),
};
#endif

static struct regulator_consumer_supply ldo7_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_core_1.2v", NULL),
};

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("votg_3.0v", NULL),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.0v", NULL),
	REGULATOR_SUPPLY("VDD10", "s5p-mipi-dsim"),
};

static struct regulator_consumer_supply ldo13_supply[] = {
	REGULATOR_SUPPLY("vlcd_3.3v", NULL),
	REGULATOR_SUPPLY("VCI", "s6e8aa0"),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vcc_1.8v", NULL),
};

static struct regulator_consumer_supply ldo15_supply[] = {
	REGULATOR_SUPPLY("vlcd_2.2v", NULL),
	REGULATOR_SUPPLY("VDD3", "s6e8aa0"),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("vt_cam_1.8v", NULL),
};

static struct regulator_consumer_supply ldo16_supply[] = {
	REGULATOR_SUPPLY("cam_isp_sensor_1.8v", NULL),
};

static struct regulator_consumer_supply ldo17_supply[] = {
	REGULATOR_SUPPLY("cam_af_2.8v", NULL),
};

static struct regulator_consumer_supply ldo18_supply[] = {
	REGULATOR_SUPPLY("touch", NULL),
};

static struct regulator_consumer_supply max8997_buck1 =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply max8997_buck2[] = {
	REGULATOR_SUPPLY("vdd_int", NULL),
	REGULATOR_SUPPLY("vdd_int", "exynos4412-busfreq"),
};

static struct regulator_consumer_supply max8997_buck3 =
	REGULATOR_SUPPLY("vdd_g3d", NULL);

static struct regulator_consumer_supply max8997_buck4 =
	REGULATOR_SUPPLY("cam_isp_core_1.2v", NULL);

#define REGULATOR_INIT(_ldo, _name, _min_uV, _max_uV, _always_on, _ops_mask, \
		       _disabled)					\
	static struct regulator_init_data _ldo##_init_data = {		\
		.constraints = {					\
			.name	= _name,				\
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

REGULATOR_INIT(ldo1, "VMIPI_1.8V", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo6, "VCC_1.8V_AP", 1800000, 1800000, 1, 0, 0);
REGULATOR_INIT(ldo7, "CAM_SENSOR_CORE_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo8, "VUOTG_3.0V", 3000000, 3000000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo11, "VMIPI_1.0V", 1000000, 1000000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "VT_CAM_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo13, "VCC_3.3V_LCD", 3300000, 3300000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo14, "VCC_1.8V_IO", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo15, "VDD_2.2V_LCD", 2200000, 2200000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo16, "CAM_ISP_SENSOR_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo17, "CAM_AF_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo18, "TSP_AVDD_3.3V", 3300000, 3300000, 0,
	       REGULATOR_CHANGE_STATUS, 1);

static struct regulator_init_data max8997_buck1_data = {
	.constraints	= {
		.name	= "vdd_arm range",
		.min_uV	= 950000,
		.max_uV	= 1100000,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &max8997_buck1,
};

static struct regulator_init_data max8997_buck2_data = {
	.constraints	= {
		.name	= "vdd_int range",
		.min_uV	= 900000,
		.max_uV	= 1100000,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck2),
	.consumer_supplies = max8997_buck2,
};

static struct regulator_init_data max8997_buck3_data = {
	.constraints	= {
		.name	= "vdd_g3d range",
		.min_uV	= 950000,
		.max_uV	= 1150000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
		REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &max8997_buck3,
};

static struct regulator_init_data max8997_buck4_data = {
	.constraints	= {
		.name	= "CAM_ISP_CORE_1.2V",
		.min_uV	= 1200000,
		.max_uV	= 1200000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &max8997_buck4,
};

static struct max8997_regulator_data max8997_regulators[] = {
	{ MAX8997_BUCK1, &max8997_buck1_data, },
	{ MAX8997_BUCK2, &max8997_buck2_data, },
	{ MAX8997_BUCK3, &max8997_buck3_data, },
	{ MAX8997_BUCK4, &max8997_buck4_data, },
	{ MAX8997_LDO1, &ldo1_init_data, },
	{ MAX8997_LDO6, &ldo6_init_data, },
	{ MAX8997_LDO7, &ldo7_init_data, },
	{ MAX8997_LDO8, &ldo8_init_data, },
	{ MAX8997_LDO11, &ldo11_init_data, },
	{ MAX8997_LDO12, &ldo12_init_data, },
	{ MAX8997_LDO13, &ldo13_init_data, },
	{ MAX8997_LDO14, &ldo14_init_data, },
	{ MAX8997_LDO15, &ldo15_init_data, },
	{ MAX8997_LDO16, &ldo16_init_data, },
	{ MAX8997_LDO17, &ldo17_init_data, },
	{ MAX8997_LDO18, &ldo18_init_data, },
};

struct max8997_platform_data exynos4_max8997_info = {
	.irq_base = IRQ_BOARD_PMIC_START,
	.num_regulators = ARRAY_SIZE(max8997_regulators),
	.regulators = max8997_regulators,
	.buck1_max_vol = 1100000,
	.buck2_max_vol = 1100000,
	.buck5_max_vol = 1100000,
	.buck_set1 = EXYNOS4212_GPJ1(1),
	.buck_set2 = EXYNOS4212_GPJ1(2),
	.buck_set3 = EXYNOS4_GPL0(0),
#ifdef CONFIG_VIBETONZ
	.motor = &max8997_motor,
#endif
};
#elif defined(CONFIG_REGULATOR_MAX77686)
/* max77686 */

#ifdef CONFIG_SND_SOC_WM8994
static struct regulator_consumer_supply ldo3_supply[] = {
	REGULATOR_SUPPLY("AVDD2", NULL),
	REGULATOR_SUPPLY("CPVDD", NULL),
	REGULATOR_SUPPLY("DBVDD1", NULL),
	REGULATOR_SUPPLY("DBVDD2", NULL),
	REGULATOR_SUPPLY("DBVDD3", NULL),
};
#else
static struct regulator_consumer_supply ldo3_supply[] = {};
#endif

static struct regulator_consumer_supply ldo5_supply[] = {
	REGULATOR_SUPPLY("vcc_1.8v", NULL),
};

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.0v", NULL),
	REGULATOR_SUPPLY("VDD10", "s5p-mipi-dsim.0"),
};

static struct regulator_consumer_supply ldo9_supply[] = {
	REGULATOR_SUPPLY("cam_dvdd_1.5v", NULL),
};

static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
	REGULATOR_SUPPLY("VDD18", "s5p-mipi-dsim.0"),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("vabb1_1.9v", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("votg_3.0v", NULL),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vabb2_1.9v", NULL),
};

static struct regulator_consumer_supply ldo17_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_core_1.2v", NULL),
};

static struct regulator_consumer_supply ldo18_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_io_1.8v", NULL),
};

static struct regulator_consumer_supply ldo19_supply[] = {
	REGULATOR_SUPPLY("vt_cam_1.8v", NULL),
};

static struct regulator_consumer_supply ldo21_supply[] = {
	REGULATOR_SUPPLY("vtf_2.8v", NULL),
};

static struct regulator_consumer_supply ldo23_supply[] = {
	REGULATOR_SUPPLY("touch", NULL),
};

static struct regulator_consumer_supply ldo24_supply[] = {
	REGULATOR_SUPPLY("touch_1.8v", NULL),
};

static struct regulator_consumer_supply ldo25_supply[] = {
	REGULATOR_SUPPLY("vcc_3.0v_lcd", NULL),
	REGULATOR_SUPPLY("VCI", "s6e39a0x02"),
};

static struct regulator_consumer_supply ldo26_supply[] = {
	REGULATOR_SUPPLY("vmotor", NULL),
};

static struct regulator_consumer_supply max77686_buck1[] = {
	REGULATOR_SUPPLY("vdd_mif", NULL),
	REGULATOR_SUPPLY("vdd_mif", "exynos4412-busfreq"),
};

static struct regulator_consumer_supply max77686_buck2 =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply max77686_buck3[] = {
	REGULATOR_SUPPLY("vdd_int", NULL),
	REGULATOR_SUPPLY("vdd_int", "exynoss4412-busfreq"),
};

static struct regulator_consumer_supply max77686_buck4[] = {
	REGULATOR_SUPPLY("vdd_g3d", NULL),
	REGULATOR_SUPPLY("vdd_g3d", "mali_dev.0"),
};

static struct regulator_consumer_supply max77686_buck9 =
	REGULATOR_SUPPLY("cam_isp_core_1.2v", NULL);

static struct regulator_consumer_supply max77686_enp32khz[] = {
	REGULATOR_SUPPLY("lpo_in", "bcm47511"),
	REGULATOR_SUPPLY("lpo", "bcm4334_bluetooth"),
};

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

REGULATOR_INIT(ldo3, "VCC_1.8V_AP", 1800000, 1800000, 1, 0, 0);
REGULATOR_INIT(ldo5, "VCC_1.8V_IO", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo8, "VMIPI_1.0V", 1000000, 1000000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo9, "CAM_DVDD_1.5V", 1500000, 1500000, 0,
			REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo10, "VMIPI_1.8V", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo11, "VABB1_1.9V", 1900000, 1900000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "VUOTG_3.0V", 3000000, 3000000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo14, "VABB2_1.9V", 1900000, 1900000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo17, "CAM_SENSOR_CORE_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo18, "CAM_SENSOR_IO_1.8V", 1800000, 1800000, 0,
			REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo19, "VT_CAM_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo21, "VTF_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo23, "TSP_AVDD_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo24, "VDD_1.8V_TSP", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo25, "VCC_3.3V_LCD", 3300000, 3300000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo26, "VCC_MOTOR_3.0V", 3000000, 3000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);

#if defined(CONFIG_MACH_SLP_PQ)
static struct regulator_init_data ldo24_pq11_init_data = {
	.constraints = {
		.name = "VDD_1.8V_TSP",
		.min_uV = 1800000,
		.max_uV = 1800000,
		.always_on = 0,
		.boot_on = 0,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.enabled	= 0,
			.disabled	= 1,
		}
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = ldo24_supply,
};
#endif

static struct regulator_init_data max77686_buck1_data = {
	.constraints = {
		.name = "vdd_mif range",
		.min_uV = 850000,
		.max_uV = 1100000,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck1),
	.consumer_supplies = max77686_buck1,
};

static struct regulator_init_data max77686_buck2_data = {
	.constraints = {
		.name = "vdd_arm range",
		.min_uV = 850000,
		.max_uV = 1500000,
		.apply_uV = 1,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &max77686_buck2,
};

static struct regulator_init_data max77686_buck3_data = {
	.constraints = {
		.name = "vdd_int range",
		.min_uV = 850000,
		.max_uV = 1300000,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck3),
	.consumer_supplies = max77686_buck3,
};

static struct regulator_init_data max77686_buck4_data = {
	.constraints = {
		.name = "vdd_g3d range",
		.min_uV = 850000,
		.max_uV = 1150000,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
		REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck4),
	.consumer_supplies = max77686_buck4,
};

static struct regulator_init_data max77686_buck9_data = {
	.constraints = {
		.name = "CAM_ISP_CORE_1.2V",
		.min_uV = 1000000,
		.max_uV = 1200000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
		REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &max77686_buck9,
};

static struct regulator_init_data max77686_enp32khz_data = {
	.constraints = {
		.name = "32KHZ_PMIC",
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.enabled	= 1,
			.disabled	= 0,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_enp32khz),
	.consumer_supplies = max77686_enp32khz,
};

static struct max77686_regulator_data max77686_regulators[] = {
	{MAX77686_BUCK1, &max77686_buck1_data,},
	{MAX77686_BUCK2, &max77686_buck2_data,},
	{MAX77686_BUCK3, &max77686_buck3_data,},
	{MAX77686_BUCK4, &max77686_buck4_data,},
	{MAX77686_BUCK9, &max77686_buck9_data,},
	{MAX77686_LDO3, &ldo3_init_data,},
	{MAX77686_LDO5, &ldo5_init_data,},
	{MAX77686_LDO8, &ldo8_init_data,},
	{MAX77686_LDO9, &ldo9_init_data,},
	{MAX77686_LDO10, &ldo10_init_data,},
	{MAX77686_LDO11, &ldo11_init_data,},
	{MAX77686_LDO12, &ldo12_init_data,},
	{MAX77686_LDO14, &ldo14_init_data,},
	{MAX77686_LDO17, &ldo17_init_data,},
	{MAX77686_LDO18, &ldo18_init_data,},
	{MAX77686_LDO19, &ldo19_init_data,},
	{MAX77686_LDO21, &ldo21_init_data,},
	{MAX77686_LDO23, &ldo23_init_data,},
	{MAX77686_LDO24, &ldo24_init_data,},
	{MAX77686_LDO25, &ldo25_init_data,},
	{MAX77686_LDO26, &ldo26_init_data,},
	{MAX77686_P32KH, &max77686_enp32khz_data,},
};

struct max77686_opmode_data max77686_opmode_data[MAX77686_REG_MAX] = {
	[MAX77686_LDO3] = {MAX77686_LDO3, MAX77686_OPMODE_NORMAL},
	[MAX77686_LDO12] = {MAX77686_LDO12, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK1] = {MAX77686_BUCK1, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK2] = {MAX77686_BUCK2, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK3] = {MAX77686_BUCK3, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK4] = {MAX77686_BUCK4, MAX77686_OPMODE_STANDBY},
};

struct max77686_platform_data exynos4_max77686_info = {
	.num_regulators = ARRAY_SIZE(max77686_regulators),
	.regulators = max77686_regulators,
	.irq_gpio	= GPIO_PMIC_IRQ,
	.irq_base	= IRQ_BOARD_PMIC_START,
	.wakeup		= 1,

	.opmode_data = max77686_opmode_data,
	.ramp_rate = MAX77686_RAMP_RATE_27MV,

	.buck234_gpio_dvs = {
		GPIO_PMIC_DVS1,
		GPIO_PMIC_DVS2,
		GPIO_PMIC_DVS3,
	},
	.buck234_gpio_selb = {
		GPIO_BUCK2_SEL,
		GPIO_BUCK3_SEL,
		GPIO_BUCK4_SEL,
	},
	.buck2_voltage[0] = 1100000,	/* 1.1V */
	.buck2_voltage[1] = 1100000,	/* 1.1V */
	.buck2_voltage[2] = 1100000,	/* 1.1V */
	.buck2_voltage[3] = 1100000,	/* 1.1V */
	.buck2_voltage[4] = 1100000,	/* 1.1V */
	.buck2_voltage[5] = 1100000,	/* 1.1V */
	.buck2_voltage[6] = 1100000,	/* 1.1V */
	.buck2_voltage[7] = 1100000,	/* 1.1V */

	.buck3_voltage[0] = 1100000,	/* 1.1V */
	.buck3_voltage[1] = 1100000,	/* 1.1V */
	.buck3_voltage[2] = 1100000,	/* 1.1V */
	.buck3_voltage[3] = 1100000,	/* 1.1V */
	.buck3_voltage[4] = 1100000,	/* 1.1V */
	.buck3_voltage[5] = 1100000,	/* 1.1V */
	.buck3_voltage[6] = 1100000,	/* 1.1V */
	.buck3_voltage[7] = 1100000,	/* 1.1V */

	.buck4_voltage[0] = 1100000,	/* 1.1V */
	.buck4_voltage[1] = 1100000,	/* 1.1V */
	.buck4_voltage[2] = 1100000,	/* 1.1V */
	.buck4_voltage[3] = 1100000,	/* 1.1V */
	.buck4_voltage[4] = 1100000,	/* 1.1V */
	.buck4_voltage[5] = 1100000,	/* 1.1V */
	.buck4_voltage[6] = 1100000,	/* 1.1V */
	.buck4_voltage[7] = 1100000,	/* 1.1V */
};

void midas_power_init(void)
{
#if defined(CONFIG_MACH_M0)
	if (system_rev == 0 || system_rev == 3)
#elif defined(CONFIG_MACH_C1)
		if (system_rev <= 1 || system_rev == 3)
#elif defined(CONFIG_MACH_M3)
			if (system_rev == 0)
#endif
				ldo8_init_data.constraints.always_on = 1;
	ldo10_init_data.constraints.always_on = 1;
}
#endif /* CONFIG_REGULATOR_MAX77686 */

void midas_power_set_muic_pdata(void *pdata, int gpio)
{
	gpio_request(gpio, "AP_PMIC_IRQ");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

#ifdef CONFIG_REGULATOR_MAX8997
	exynos4_max8997_info.muic = pdata;
#endif
}

void midas_power_gpio_init(void)
{
#ifdef CONFIG_REGULATOR_MAX8997
	int gpio;

	gpio = EXYNOS4212_GPJ1(1);
	gpio_request(gpio, "BUCK_SET1");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = EXYNOS4212_GPJ1(2);
	gpio_request(gpio, "BUCK_SET2");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = EXYNOS4_GPL0(0);
	gpio_request(gpio, "BUCK_SET3");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
#endif
}

#ifdef CONFIG_MFD_MAX77693
static struct regulator_consumer_supply safeout1_supply[] = {
	REGULATOR_SUPPLY("safeout1", NULL),
};

static struct regulator_consumer_supply safeout2_supply[] = {
	REGULATOR_SUPPLY("safeout2", NULL),
};

static struct regulator_consumer_supply charger_supply[] = {
	REGULATOR_SUPPLY("vinchg1", "charger-manager.0"),
};

static struct regulator_init_data safeout1_init_data = {
	.constraints	= {
		.name		= "safeout1 range",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on	= 0,
		.boot_on	= 1,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(safeout1_supply),
	.consumer_supplies	= safeout1_supply,
};

static struct regulator_init_data safeout2_init_data = {
	.constraints	= {
		.name		= "safeout2 range",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on	= 0,
		.boot_on	= 0,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(safeout2_supply),
	.consumer_supplies	= safeout2_supply,
};

static struct regulator_init_data charger_init_data = {
	.constraints	= {
		.name		= "CHARGER",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
		REGULATOR_CHANGE_CURRENT,
		.boot_on	= 1,
		.min_uA		= 60000,
		.max_uA		= 2580000,
	},
	.num_consumer_supplies	= ARRAY_SIZE(charger_supply),
	.consumer_supplies	= charger_supply,
};

struct max77693_regulator_data max77693_regulators[] = {
	{MAX77693_ESAFEOUT1, &safeout1_init_data,},
	{MAX77693_ESAFEOUT2, &safeout2_init_data,},
	{MAX77693_CHARGER, &charger_init_data,},
};

#if defined(CONFIG_MACH_SLP_PQ)
/* this initcall replace ldo24 from VDD 2.2 to VDD 1.8 for evt1.1 board. */
static int __init regulator_init_with_rev(void)
{
	/* SLP PQ Promixa evt1.1 */
	if (system_rev != 3) {
		ldo24_supply[0].supply = "touch_1.8v";
		ldo24_supply[0].dev_name = NULL;

		memcpy(&ldo24_init_data, &ldo24_pq11_init_data,
		       sizeof(struct regulator_init_data));
	}
	return 0;
}

postcore_initcall(regulator_init_with_rev);
#endif /* CONFIG_MACH_SLP_PQ */
#endif /* CONFIG_MFD_MAX77693 */

#if defined(CONFIG_REGULATOR_S5M8767)
/* S5M8767 Regulator */

static struct regulator_consumer_supply ldo1_supply[] = { };

static struct regulator_consumer_supply ldo2_supply[] = { };

#ifdef CONFIG_SND_SOC_WM8994
static struct regulator_consumer_supply ldo3_supply[] = {
	REGULATOR_SUPPLY("AVDD2", NULL),
	REGULATOR_SUPPLY("CPVDD", NULL),
	REGULATOR_SUPPLY("DBVDD1", NULL),
	REGULATOR_SUPPLY("DBVDD2", NULL),
	REGULATOR_SUPPLY("DBVDD3", NULL),
};
#else
static struct regulator_consumer_supply ldo3_supply[] = {};
#endif

static struct regulator_consumer_supply ldo5_supply[] = { };

static struct regulator_consumer_supply ldo6_supply[] = { };

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.0v", NULL),
	REGULATOR_SUPPLY("VDD10", "s5p-mipi-dsim.0"),
};

static struct regulator_consumer_supply ldo9_supply[] = {
	REGULATOR_SUPPLY("cam_isp_1.8v", NULL),
};

static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("cam_dvdd_1.5v", NULL),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("vabb1_1.8v", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("votg_3.0v", NULL),
};

static struct regulator_consumer_supply ldo13_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
	REGULATOR_SUPPLY("VDD18", "s5p-mipi-dsim.0"),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vabb2_1.8v", NULL),
};

static struct regulator_consumer_supply ldo15_supply[] = { };

static struct regulator_consumer_supply ldo16_supply[] = { };

static struct regulator_consumer_supply ldo17_supply[] = { };

static struct regulator_consumer_supply ldo19_supply[] = {
	REGULATOR_SUPPLY("cam_af_2.8v", NULL),
};

static struct regulator_consumer_supply ldo20_supply[] = {
	REGULATOR_SUPPLY("vcc_3.0v_lcd", NULL),
	REGULATOR_SUPPLY("VCI", "s6e39a0x"),
};

static struct regulator_consumer_supply ldo21_supply[] = {
	REGULATOR_SUPPLY("vmotor", NULL),
};

static struct regulator_consumer_supply ldo22_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_a2.8v", NULL),
};

static struct regulator_consumer_supply ldo23_supply[] = {
	REGULATOR_SUPPLY("vtf_2.8v", NULL),
};

static struct regulator_consumer_supply ldo24_supply[] = {
	REGULATOR_SUPPLY("touch", NULL),
};

static struct regulator_consumer_supply ldo25_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_core_1.2v", NULL),
};

static struct regulator_consumer_supply ldo26_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_io_1.8v", NULL),
};

static struct regulator_consumer_supply ldo27_supply[] = {
	REGULATOR_SUPPLY("vt_cam_1.8v", NULL),
};

static struct regulator_consumer_supply ldo28_supply[] = {
	REGULATOR_SUPPLY("touch_1.8v", NULL),
};

static struct regulator_consumer_supply s5m8767_buck1[] = {
	REGULATOR_SUPPLY("vdd_mif", NULL),
	REGULATOR_SUPPLY("vdd_mif", "exynos4212-busfreq"),
};

static struct regulator_consumer_supply s5m8767_buck2 =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply s5m8767_buck3[] = {
	REGULATOR_SUPPLY("vdd_int", NULL),
	REGULATOR_SUPPLY("vdd_int", "exynos4212-busfreq"),
};

static struct regulator_consumer_supply s5m8767_buck4[] = {
	REGULATOR_SUPPLY("vdd_g3d", NULL),
	REGULATOR_SUPPLY("vdd_g3d", "mali_dev.0"),
};

static struct regulator_consumer_supply s5m8767_buck6 =
	REGULATOR_SUPPLY("cam_isp_core_1.2v", NULL);

static struct regulator_consumer_supply s5m8767_enp32khz[] = {
	REGULATOR_SUPPLY("lpo_in", "bcm47511"),
	REGULATOR_SUPPLY("lpo", "bcm4334_bluetooth"),
};

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
REGULATOR_INIT(ldo1, "VALIVE_1.0V_AP", 1000000, 1000000, 1, 0, 0);
REGULATOR_INIT(ldo2, "VM1M2_1.2V_AP", 1200000, 1200000, 1, 0, 0);
REGULATOR_INIT(ldo3, "VCC_1.8V_AP", 1800000, 1800000, 1, 0, 0);
REGULATOR_INIT(ldo5, "VDDQ_MMC_2.8V", 2800000, 2800000, 1, 0, 0);
REGULATOR_INIT(ldo6, "VMPLL_1.0V_AP", 1000000, 1000000, 1, 0, 0);
REGULATOR_INIT(ldo8, "VMIPI_1.0V", 1000000, 1000000, 0,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo9, "CAM_ISP_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo10, "VT_CAM_DVDD_1.5V", 1500000, 1500000, 0,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo11, "VABB1_1.8V", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "VUOTG_3.0V", 3000000, 3000000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo13, "VMIPI_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo14, "VABB2_1.8V", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo15, "VHSIC_1.0V_AP", 1000000, 1000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo16, "VHSIC_1.8V_AP", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo17, "VCC_2.8V_AP", 2800000, 2800000, 1, 0, 0);
REGULATOR_INIT(ldo19, "CAM_AF_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo20, "VCC_3.0V_LCD", 3300000, 3300000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo21, "VCC_MOTOR_3.0V", 3000000, 3000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo22, "CAM_SENSOR_A2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo23, "VTF_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo24, "TSP_AVDD_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo25, "CAM_SENSOR_CORE_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo26, "CAM_ISP_SENSOR_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo27, "VT_CAM_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo28, "TSP_VDD_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);

static struct regulator_init_data s5m8767_buck1_data = {
	.constraints = {
		.name = "vdd_mif range",
		.min_uV = 850000,
		.max_uV = 1100000,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(s5m8767_buck1),
	.consumer_supplies = s5m8767_buck1,
};

static struct regulator_init_data s5m8767_buck2_data = {
	.constraints = {
		.name = "vdd_arm range",
		.min_uV = 850000,
		.max_uV = 1500000,
		.apply_uV = 1,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &s5m8767_buck2,
};

static struct regulator_init_data s5m8767_buck3_data = {
	.constraints = {
		.name = "vdd_int range",
		.min_uV = 850000,
		.max_uV = 1300000,
		.always_on = 1,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(s5m8767_buck3),
	.consumer_supplies = s5m8767_buck3,
};

static struct regulator_init_data s5m8767_buck4_data = {
	.constraints = {
		.name = "vdd_g3d range",
		.min_uV = 850000,
		.max_uV = 1150000,
		.boot_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
		REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(s5m8767_buck4),
	.consumer_supplies = s5m8767_buck4,
};

static struct regulator_init_data s5m8767_buck6_data = {
	.constraints = {
		.name = "CAM_ISP_CORE_1.2V",
		.min_uV = 1000000,
		.max_uV = 1200000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
		REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &s5m8767_buck6,
};

static struct regulator_init_data s5m8767_enp32khz_data = {
	.constraints = {
		.name = "32KHZ_PMIC",
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.enabled	= 1,
			.disabled	= 0,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(s5m8767_enp32khz),
	.consumer_supplies = s5m8767_enp32khz,
};

static struct s5m_regulator_data s5m8767_regulators[] = {
	{S5M8767_BUCK1, &s5m8767_buck1_data,},
	{S5M8767_BUCK2, &s5m8767_buck2_data,},
	{S5M8767_BUCK3, &s5m8767_buck3_data,},
	{S5M8767_BUCK4, &s5m8767_buck4_data,},
	{S5M8767_BUCK6, &s5m8767_buck6_data,},
	{S5M8767_LDO1, &ldo1_init_data,},
	{S5M8767_LDO2, &ldo2_init_data,},
	{S5M8767_LDO3, &ldo3_init_data,},
	{S5M8767_LDO5, &ldo5_init_data,},
	{S5M8767_LDO6, &ldo6_init_data,},
	{S5M8767_LDO8, &ldo8_init_data,},
	{S5M8767_LDO9, &ldo9_init_data,},
	{S5M8767_LDO10, &ldo10_init_data,},
	{S5M8767_LDO11, &ldo11_init_data,},
	{S5M8767_LDO12, &ldo12_init_data,},
	{S5M8767_LDO13, &ldo13_init_data,},
	{S5M8767_LDO14, &ldo14_init_data,},
	{S5M8767_LDO15, &ldo15_init_data,},
	{S5M8767_LDO16, &ldo16_init_data,},
	{S5M8767_LDO17, &ldo17_init_data,},
	{S5M8767_LDO19, &ldo19_init_data,},
	{S5M8767_LDO20, &ldo20_init_data,},
	{S5M8767_LDO21, &ldo21_init_data,},
	{S5M8767_LDO22, &ldo22_init_data,},
	{S5M8767_LDO23, &ldo23_init_data,},
	{S5M8767_LDO24, &ldo24_init_data,},
	{S5M8767_LDO25, &ldo25_init_data,},
	{S5M8767_LDO26, &ldo26_init_data,},
	{S5M8767_LDO27, &ldo27_init_data,},
	{S5M8767_LDO28, &ldo28_init_data,},
};

struct s5m_opmode_data s5m8767_opmode_data[S5M8767_REG_MAX] = {
	[S5M8767_LDO3] = {S5M8767_LDO3, S5M_OPMODE_NORMAL},
	[S5M8767_LDO12] = {S5M8767_LDO12, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK1] = {S5M8767_BUCK1, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK2] = {S5M8767_BUCK2, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK3] = {S5M8767_BUCK3, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK4] = {S5M8767_BUCK4, S5M_OPMODE_STANDBY},
};

struct s5m_platform_data exynos4_s5m8767_info = {
	.device_type	= S5M8767X,
	.num_regulators = ARRAY_SIZE(s5m8767_regulators),
	.regulators = s5m8767_regulators,
	.irq_gpio	= GPIO_PMIC_IRQ,
	.irq_base	= IRQ_BOARD_PMIC_START,
	.wakeup		= 1,

	.opmode_data = s5m8767_opmode_data,

	.buck2_voltage[0] = 1100000,	/* 1.1V */
	.buck2_voltage[1] = 1100000,	/* 1.1V */
	.buck2_voltage[2] = 1100000,	/* 1.1V */
	.buck2_voltage[3] = 1100000,	/* 1.1V */
	.buck2_voltage[4] = 1100000,	/* 1.1V */
	.buck2_voltage[5] = 1100000,	/* 1.1V */
	.buck2_voltage[6] = 1100000,	/* 1.1V */
	.buck2_voltage[7] = 1100000,	/* 1.1V */

	.buck3_voltage[0] = 1100000,	/* 1.1V */
	.buck3_voltage[1] = 1100000,	/* 1.1V */
	.buck3_voltage[2] = 1100000,	/* 1.1V */
	.buck3_voltage[3] = 1100000,	/* 1.1V */
	.buck3_voltage[4] = 1100000,	/* 1.1V */
	.buck3_voltage[5] = 1100000,	/* 1.1V */
	.buck3_voltage[6] = 1100000,	/* 1.1V */
	.buck3_voltage[7] = 1100000,	/* 1.1V */

	.buck4_voltage[0] = 1100000,	/* 1.1V */
	.buck4_voltage[1] = 1100000,	/* 1.1V */
	.buck4_voltage[2] = 1100000,	/* 1.1V */
	.buck4_voltage[3] = 1100000,	/* 1.1V */
	.buck4_voltage[4] = 1100000,	/* 1.1V */
	.buck4_voltage[5] = 1100000,	/* 1.1V */
	.buck4_voltage[6] = 1100000,	/* 1.1V */
	.buck4_voltage[7] = 1100000,	/* 1.1V */

	.buck_ramp_delay = 10,
	.buck_default_idx	= 3,

	.buck_gpios[0]		= GPIO_BUCK2_SEL,
	.buck_gpios[1]		= GPIO_BUCK3_SEL,
	.buck_gpios[2]		= GPIO_BUCK4_SEL,
/*
	.buck234_gpio_dvs = {
		GPIO_PMIC_DVS1,
		GPIO_PMIC_DVS2,
		GPIO_PMIC_DVS3,
	},
	.buck234_gpio_selb = {
		GPIO_BUCK2_SEL,
		GPIO_BUCK3_SEL,
		GPIO_BUCK4_SEL,
	},
*/
};

void midas_power_init(void)
{
	ldo8_init_data.constraints.always_on = 1;
	ldo10_init_data.constraints.always_on = 1;
}

/* End of S5M8767 */
#endif
