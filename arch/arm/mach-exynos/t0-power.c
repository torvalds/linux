/*
 * t0-power.c - Power Management of T0 Project
 *
 *  Copyright (C) 2011 Samsung Electrnoics
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
#include <mach/gpio-midas.h>
#include <mach/irqs.h>

#include <linux/mfd/max8997.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77693.h>

#if defined(CONFIG_REGULATOR_S5M8767)
#include <linux/mfd/s5m87xx/s5m-pmic.h>
#include <linux/mfd/s5m87xx/s5m-core.h>
#endif


#ifdef CONFIG_REGULATOR_MAX77686
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
	REGULATOR_SUPPLY("touchkey", NULL),
};

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.0v", NULL),
	REGULATOR_SUPPLY("VDD10", "s5p-mipi-dsim.0"),
	REGULATOR_SUPPLY("vdd", "exynos4-hdmi"),
	REGULATOR_SUPPLY("vdd_pll", "exynos4-hdmi"),
};

static struct regulator_consumer_supply ldo9_supply[] = {
	REGULATOR_SUPPLY("cam_isp_mipi_1.2v", NULL),
};

static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
	REGULATOR_SUPPLY("VDD18", "s5p-mipi-dsim.0"),
	REGULATOR_SUPPLY("vdd_osc", "exynos4-hdmi"),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("vabb1_1.95v", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("votg_3.0v", NULL),
};

static struct regulator_consumer_supply ldo13_supply[] = {
	REGULATOR_SUPPLY("vcc_1.8v_lcd", NULL),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vabb2_1.95v", NULL),
};

#if defined(CONFIG_MACH_T0_CHN_CTC)
static struct regulator_consumer_supply ldo17_supply[] = {
	REGULATOR_SUPPLY("vcc_1.8v_usb", NULL),
};
#else
static struct regulator_consumer_supply ldo17_supply[] = {
#if defined(CONFIG_TARGET_LOCALE_USA)
	REGULATOR_SUPPLY("vcc_adc_1.8v", NULL),
#else
	REGULATOR_SUPPLY("cam_sensor_core_1.2v", NULL),
#endif
};
#endif

static struct regulator_consumer_supply ldo18_supply[] = {
	REGULATOR_SUPPLY("cam_isp_sensor_1.8v", NULL),
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
	REGULATOR_SUPPLY("vlcd_3.3v", NULL),
	REGULATOR_SUPPLY("VCI", "s6e8aa0"),
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
REGULATOR_INIT(ldo9, "CAM_ISP_MIPI_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo10, "VMIPI_1.8V", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo11, "VABB1_1.95V", 1950000, 1950000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "VUOTG_3.0V", 3000000, 3000000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo13, "VCC_1.8V_LCD", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo14, "VABB2_1.95V", 1950000, 1950000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
#if defined(CONFIG_MACH_T0_CHN_CTC)
REGULATOR_INIT(ldo17, "VCC_1.8V_USB", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
#else
#if defined(CONFIG_TARGET_LOCALE_USA)
REGULATOR_INIT(ldo17, "VCC_ADC_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo17, "CAM_SENSOR_CORE_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
#endif
#endif
REGULATOR_INIT(ldo18, "CAM_ISP_SENSOR_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo19, "VT_CAM_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo21, "VTF_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo23, "TSP_AVDD_3.3V", 3300000, 3300000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo24, "VDD_1.8V_TSP", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo25, "VCC_3.0V_LCD", 3000000, 3000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo26, "VCC_MOTOR_3.0V", 3000000, 3000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);

static struct regulator_init_data max77686_buck1_data = {
	.constraints = {
		.name = "vdd_mif range",
		.min_uV = 850000,
		.max_uV = 1200000,
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
		.min_uV = 825000,
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
		.max_uV = 1200000,
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
	{MAX77686_LDO13, &ldo13_init_data,},
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
	[MAX77686_LDO8] = {MAX77686_LDO8, MAX77686_OPMODE_STANDBY},
	[MAX77686_LDO10] = {MAX77686_LDO10, MAX77686_OPMODE_STANDBY},
	[MAX77686_LDO11] = {MAX77686_LDO11, MAX77686_OPMODE_STANDBY},
	[MAX77686_LDO12] = {MAX77686_LDO12, MAX77686_OPMODE_STANDBY},
	[MAX77686_LDO13] = {MAX77686_LDO13, MAX77686_OPMODE_NORMAL},
	[MAX77686_LDO14] = {MAX77686_LDO14, MAX77686_OPMODE_STANDBY},
#if defined(CONFIG_MACH_T0_CHN_CTC)
	[MAX77686_LDO17] = {MAX77686_LDO17, MAX77686_OPMODE_NORMAL},
#endif
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
	.wtsr_smpl = MAX77686_WTSR_ENABLE | MAX77686_SMPL_ENABLE,

	.buck234_gpio_dvs = {
		/* Use DVS2 register of each bucks to supply stable power
		 * after sudden reset */
		{GPIO_PMIC_DVS1, 1},
		{GPIO_PMIC_DVS2, 0},
		{GPIO_PMIC_DVS3, 0},
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
};

#endif /* CONFIG_REGULATOR_MAX77686 */

void midas_power_set_muic_pdata(void *pdata, int gpio)
{
	gpio_request(gpio, "AP_PMIC_IRQ");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
}

void midas_power_gpio_init(void)
{
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
	REGULATOR_SUPPLY("vinchg1", NULL),
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
#endif /* CONFIG_MFD_MAX77693 */

#if defined(CONFIG_REGULATOR_S5M8767)
/* S5M8767 Regulator */

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

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.0v", NULL),
	REGULATOR_SUPPLY("VDD10", "s5p-mipi-dsim.0"),
	REGULATOR_SUPPLY("vdd", "exynos4-hdmi"),
	REGULATOR_SUPPLY("vdd_pll", "exynos4-hdmi"),
};

static struct regulator_consumer_supply ldo9_supply[] = {
	REGULATOR_SUPPLY("cam_isp_mipi_1.2v", NULL),
};

static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
	REGULATOR_SUPPLY("VDD18", "s5p-mipi-dsim.0"),
	REGULATOR_SUPPLY("vdd_osc", "exynos4-hdmi"),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("vabb1_1.95v", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("votg_3.0v", NULL),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vabb2_1.95v", NULL),
};

static struct regulator_consumer_supply ldo19_supply[] = {
	REGULATOR_SUPPLY("touch_1.8v", NULL),
};

static struct regulator_consumer_supply ldo20_supply[] = {
	REGULATOR_SUPPLY("vlcd_3.3v", NULL),
	REGULATOR_SUPPLY("VCI", "s6e8aa0"),
};
static struct regulator_consumer_supply ldo21_supply[] = {
	REGULATOR_SUPPLY("vmotor", NULL),
};

static struct regulator_consumer_supply ldo22_supply[] = {
	REGULATOR_SUPPLY("vcc_1.8v", NULL),
	REGULATOR_SUPPLY("touchkey", NULL),
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
	REGULATOR_SUPPLY("cam_isp_sensor_1.8v", NULL),
};

static struct regulator_consumer_supply ldo27_supply[] = {
	REGULATOR_SUPPLY("vt_cam_1.8v", NULL),
};

static struct regulator_consumer_supply ldo28_supply[] = {
	REGULATOR_SUPPLY("vcc_1.8v_lcd", NULL),
};

static struct regulator_consumer_supply s5m8767_buck1[] = {
	REGULATOR_SUPPLY("vdd_mif", NULL),
	REGULATOR_SUPPLY("vdd_mif", "exynos4212-busfreq"),
};

static struct regulator_consumer_supply s5m8767_buck2 =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply s5m8767_buck3[] = {
	REGULATOR_SUPPLY("vdd_int", NULL),
	REGULATOR_SUPPLY("vdd_int", "exynoss4412-busfreq"),
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

REGULATOR_INIT(ldo3, "VCC_1.8V_AP", 1800000, 1800000, 1, 0, 0);
REGULATOR_INIT(ldo8, "VMIPI_1.0V", 1000000, 1000000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo9, "CAM_ISP_MIPI_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo10, "VMIPI_1.8V", 1800000, 1800000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo11, "VABB1_1.95V", 1950000, 1950000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "VUOTG_3.0V", 3000000, 3000000, 1,
	       REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo14, "VABB2_1.95V", 1950000, 1950000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo19, "VDD_1.8V_TSP", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo20, "VCC_3.0V_LCD", 3000000, 3000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo21, "VCC_MOTOR_3.0V", 3000000, 3000000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo22, "VTOUCH_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo23, "VTF_2.8V", 2800000, 2800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo24, "TSP_AVDD_3.3V", 3300000, 3300000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo25, "CAM_SENSOR_CORE_1.2V", 1200000, 1200000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo26, "CAM_ISP_SENSOR_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo27, "VT_CAM_1.8V", 1800000, 1800000, 0,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo28, "VCC_1.8V_LCD", 1800000, 1800000, 0,
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
		.always_on = 1,
		.apply_uV = 1,
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
		.max_uV = 1100000,
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
		.max_uV = 1075000,
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
	{S5M8767_LDO3, &ldo3_init_data,},
	{S5M8767_LDO8, &ldo8_init_data,},
	{S5M8767_LDO9, &ldo9_init_data,},
	{S5M8767_LDO10, &ldo10_init_data,},
	{S5M8767_LDO11, &ldo11_init_data,},
	{S5M8767_LDO12, &ldo12_init_data,},
	{S5M8767_LDO14, &ldo14_init_data,},
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
	[S5M8767_BUCK1] = {S5M8767_BUCK1, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK2] = {S5M8767_BUCK2, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK3] = {S5M8767_BUCK3, S5M_OPMODE_STANDBY},
	[S5M8767_BUCK4] = {S5M8767_BUCK4, S5M_OPMODE_STANDBY},
	[S5M8767_LDO3] = {S5M8767_LDO3, S5M_OPMODE_NORMAL},
	[S5M8767_LDO8] = {S5M8767_LDO8, S5M_OPMODE_STANDBY},
	[S5M8767_LDO10] = {S5M8767_LDO10, S5M_OPMODE_STANDBY},
	[S5M8767_LDO11] = {S5M8767_LDO11, S5M_OPMODE_STANDBY},
	[S5M8767_LDO12] = {S5M8767_LDO12, S5M_OPMODE_STANDBY},
	[S5M8767_LDO14] = {S5M8767_LDO14, S5M_OPMODE_STANDBY},
};

struct s5m_platform_data exynos4_s5m8767_info = {
	.device_type	= S5M8767X,
	.num_regulators = ARRAY_SIZE(s5m8767_regulators),
	.regulators = s5m8767_regulators,
	.buck2_ramp_enable	= true,
	.buck3_ramp_enable	= true,
	.buck4_ramp_enable	= true,
	.irq_gpio	= GPIO_PMIC_IRQ,
	.irq_base	= IRQ_BOARD_PMIC_START,
	.wakeup		= 1,

	.opmode_data = s5m8767_opmode_data,
	.wtsr_smpl		= 1,

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

	.buck_ramp_delay = 25,
	.buck_default_idx = 1,

	.buck_gpios[0]		= EXYNOS4212_GPM3(0),
	.buck_gpios[1]		= EXYNOS4212_GPM3(1),
	.buck_gpios[2]		= EXYNOS4212_GPM3(2),

	.buck_ds[0]		= EXYNOS4_GPF3(1),
	.buck_ds[1]		= EXYNOS4_GPF3(2),
	.buck_ds[2]		= EXYNOS4_GPF3(3),

	.buck1_init		= 1000000,
	.buck2_init		= 1100000,
	.buck3_init		= 1000000,
	.buck4_init		= 1000000,
};

/* End of S5M8767 */
#endif

void midas_power_init(void)
{
	printk(KERN_INFO "%s\n", __func__);
}
