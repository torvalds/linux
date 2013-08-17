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

#include <asm/io.h>

#include <plat/devs.h>
#include <plat/iic.h>
#include <plat/gpio-cfg.h>

#include <mach/regs-pmu.h>
#include <mach/irqs.h>
#include <mach/hs-iic.h>
#include <mach/devfreq.h>
#include <mach/tmu.h>

#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mps11.h>

#include "board-smdk5410.h"

#define SMDK5410_PMIC_EINT	IRQ_EINT(26)

static struct regulator_consumer_supply s2m_buck1_consumer =
	REGULATOR_SUPPLY("vdd_mif", NULL);

static struct regulator_consumer_supply s2m_buck2_consumer =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply s2m_buck3_consumer =
	REGULATOR_SUPPLY("vdd_int", NULL);

static struct regulator_consumer_supply s2m_buck4_consumer =
	REGULATOR_SUPPLY("vdd_g3d", NULL);

static struct regulator_consumer_supply s2m_buck6_consumer =
	REGULATOR_SUPPLY("vdd_kfc", NULL);

static struct regulator_consumer_supply s2m_buck10_consumer =
	REGULATOR_SUPPLY("vmmc", "dw_mmc.2");

static struct regulator_consumer_supply s2m_ldo5_consumer =
	REGULATOR_SUPPLY("vdd_ldo5", NULL);

static struct regulator_consumer_supply s2m_ldo6_consumer =
	REGULATOR_SUPPLY("vdd_ldo6", NULL);

static struct regulator_consumer_supply s2m_ldo7_consumer =
	REGULATOR_SUPPLY("vdd_ldo7", NULL);

static struct regulator_consumer_supply s2m_ldo8_consumer =
	REGULATOR_SUPPLY("vdd_ldo8", NULL);

static struct regulator_consumer_supply s2m_ldo9_consumer =
	REGULATOR_SUPPLY("vdd_ldo9", NULL);

static struct regulator_consumer_supply s2m_ldo10_consumer =
	REGULATOR_SUPPLY("vdd_ldo10", NULL);

static struct regulator_consumer_supply s2m_ldo11_consumer =
	REGULATOR_SUPPLY("vdd_ldo11", NULL);

static struct regulator_consumer_supply s2m_ldo12_consumer =
	REGULATOR_SUPPLY("vdd_ldo12", NULL);

static struct regulator_consumer_supply s2m_ldo13_consumer =
	REGULATOR_SUPPLY("vqmmc", "dw_mmc.2");

static struct regulator_consumer_supply s2m_ldo15_consumer =
	REGULATOR_SUPPLY("vdd_ldo15", NULL);

static struct regulator_consumer_supply s2m_ldo16_consumer =
	REGULATOR_SUPPLY("vdd_ldo16", NULL);

static struct regulator_consumer_supply s2m_ldo17_consumer =
	REGULATOR_SUPPLY("tsp_avdd", NULL);

static struct regulator_consumer_supply s2m_ldo24_consumer =
	REGULATOR_SUPPLY("tsp_io", NULL);

static struct regulator_init_data s2m_buck1_data = {
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
	.consumer_supplies	= &s2m_buck1_consumer,
};

static struct regulator_init_data s2m_buck2_data = {
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
	.consumer_supplies	= &s2m_buck2_consumer,
};

static struct regulator_init_data s2m_buck3_data = {
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
	.consumer_supplies	= &s2m_buck3_consumer,
};

static struct regulator_init_data s2m_buck4_data = {
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
	.consumer_supplies	= &s2m_buck4_consumer,
};

static struct regulator_init_data s2m_buck6_data = {
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
	.consumer_supplies	= &s2m_buck6_consumer,
};

static struct regulator_init_data s2m_buck10_data = {
	.constraints	= {
		.name		= "vdd_mmc range",
		.min_uV		= 1800000,
		.max_uV		= 2850000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_buck10_consumer,
};

static struct regulator_init_data s2m_ldo5_data = {
	.constraints	= {
		.name		= "vdd_ldo5 range",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo5_consumer,
};

static struct regulator_init_data s2m_ldo6_data = {
	.constraints	= {
		.name		= "vdd_ldo6 range",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo6_consumer,
};

static struct regulator_init_data s2m_ldo7_data = {
	.constraints	= {
		.name		= "vdd_ldo7 range",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo7_consumer,
};

static struct regulator_init_data s2m_ldo8_data = {
	.constraints	= {
		.name		= "vdd_ldo8 range",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo8_consumer,
};

static struct regulator_init_data s2m_ldo9_data = {
	.constraints	= {
		.name		= "vdd_ldo9 range",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo9_consumer,
};

static struct regulator_init_data s2m_ldo10_data = {
	.constraints	= {
		.name		= "vdd_ldo10 range",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo10_consumer,
};

static struct regulator_init_data s2m_ldo11_data = {
	.constraints	= {
		.name		= "vdd_ldo11 range",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo11_consumer,
};

static struct regulator_init_data s2m_ldo12_data = {
	.constraints	= {
		.name		= "vdd_ldo12 range",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo12_consumer,
};

static struct regulator_init_data s2m_ldo13_data = {
	.constraints	= {
		.name		= "vdd_ldo13 range",
		.min_uV		= 1800000,
		.max_uV		= 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo13_consumer,
};

static struct regulator_init_data s2m_ldo15_data = {
	.constraints	= {
		.name		= "vdd_ldo15 range",
		.min_uV		= 3100000,
		.max_uV		= 3100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo15_consumer,
};

static struct regulator_init_data s2m_ldo16_data = {
	.constraints	= {
		.name		= "vdd_ldo16 range",
		.min_uV		= 2200000,
		.max_uV		= 2200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo16_consumer,
};

static struct regulator_init_data s2m_ldo17_data = {
	.constraints	= {
		.name		= "tsp_avdd range",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo17_consumer,
};

static struct regulator_init_data s2m_ldo24_data = {
	.constraints	= {
		.name		= "tsp_io range",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &s2m_ldo24_consumer,
};

static struct sec_regulator_data exynos_regulators[] = {
	{S2MPS11_BUCK1, &s2m_buck1_data},
	{S2MPS11_BUCK2, &s2m_buck2_data},
	{S2MPS11_BUCK3, &s2m_buck3_data},
	{S2MPS11_BUCK4, &s2m_buck4_data},
	{S2MPS11_BUCK6, &s2m_buck6_data},
	{S2MPS11_BUCK10, &s2m_buck10_data},
	{S2MPS11_LDO5, &s2m_ldo5_data},
	{S2MPS11_LDO6, &s2m_ldo6_data},
	{S2MPS11_LDO7, &s2m_ldo7_data},
	{S2MPS11_LDO8, &s2m_ldo8_data},
	{S2MPS11_LDO9, &s2m_ldo9_data},
	{S2MPS11_LDO10, &s2m_ldo10_data},
	{S2MPS11_LDO11, &s2m_ldo11_data},
	{S2MPS11_LDO12, &s2m_ldo12_data},
	{S2MPS11_LDO13, &s2m_ldo13_data},
	{S2MPS11_LDO15, &s2m_ldo15_data},
	{S2MPS11_LDO16, &s2m_ldo16_data},
	{S2MPS11_LDO17, &s2m_ldo17_data},
	{S2MPS11_LDO24, &s2m_ldo24_data},
};

struct sec_opmode_data s2mps11_opmode_data[S2MPS11_REG_MAX] = {
	[S2MPS11_BUCK1] = {S2MPS11_BUCK1, SEC_OPMODE_STANDBY},
	[S2MPS11_BUCK2] = {S2MPS11_BUCK2, SEC_OPMODE_STANDBY},
	[S2MPS11_BUCK3] = {S2MPS11_BUCK3, SEC_OPMODE_STANDBY},
	[S2MPS11_BUCK4] = {S2MPS11_BUCK4, SEC_OPMODE_STANDBY},
	[S2MPS11_BUCK6] = {S2MPS11_BUCK6, SEC_OPMODE_STANDBY},
	[S2MPS11_BUCK10] = {S2MPS11_BUCK10, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO5] = {S2MPS11_LDO5, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO6] = {S2MPS11_LDO6, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO7] = {S2MPS11_LDO7, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO8] = {S2MPS11_LDO8, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO9] = {S2MPS11_LDO9, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO10] = {S2MPS11_LDO10, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO11] = {S2MPS11_LDO11, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO12] = {S2MPS11_LDO12, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO13] = {S2MPS11_LDO13, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO15] = {S2MPS11_LDO15, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO16] = {S2MPS11_LDO16, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO17] = {S2MPS11_LDO17, SEC_OPMODE_STANDBY},
	[S2MPS11_LDO24] = {S2MPS11_LDO24, SEC_OPMODE_STANDBY},
};

static int sec_cfg_irq(void)
{
	unsigned int pin = irq_to_gpio(SMDK5410_PMIC_EINT);

	s3c_gpio_cfgpin(pin, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(pin, S3C_GPIO_PULL_UP);

	return 0;
}

static struct sec_pmic_platform_data exynos5_s2m_pdata = {
	.device_type		= S2MPS11X,
	.irq_base		= IRQ_BOARD_START,
	.num_regulators		= ARRAY_SIZE(exynos_regulators),
	.regulators		= exynos_regulators,
	.cfg_pmic_irq		= sec_cfg_irq,
	.wakeup			= 1,
	.wtsr_smpl		= 1,
	.opmode_data		= s2mps11_opmode_data,
	.buck16_ramp_delay	= 12,
	.buck2_ramp_delay	= 12,
	.buck34_ramp_delay	= 12,
	.buck2_ramp_enable	= 1,
	.buck3_ramp_enable	= 1,
	.buck4_ramp_enable	= 1,
	.buck6_ramp_enable	= 1,
};

static struct i2c_board_info hs_i2c_devs0[] __initdata = {
	{
		I2C_BOARD_INFO("sec-pmic", 0xCC >> 1),
		.platform_data	= &exynos5_s2m_pdata,
		.irq		= SMDK5410_PMIC_EINT,
	},
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
		.freq_clip_max = 1200 * 1000,
		.temp_level = 105,
	},
	.freq_tab[1] = {
		.freq_clip_max = 900 * 1000,
		.temp_level = 110,
	},
	.freq_tab[2] = {
		.freq_clip_max = 600 * 1000,
		.temp_level = 112,
	},
	.freq_tab[3] = {
		.freq_clip_max = 400 * 1000,
		.temp_level = 114,
	},
	.freq_tab[4] = {
		.freq_clip_max = 200 * 1000,
		.temp_level = 115,
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
	.speed_mode = HSI2C_HIGH_SPD,
	.fast_speed = 400000,
	.high_speed = 2500000,
	.cfg_gpio = NULL,
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
};

void __init exynos5_smdk5410_power_init(void)
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
}
