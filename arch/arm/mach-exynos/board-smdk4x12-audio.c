/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/wm8994/pdata.h>

#include <plat/gpio-cfg.h>
#include <plat/devs.h>
#include <plat/iic.h>

#include "board-smdk4x12.h"

static struct regulator_consumer_supply wm8994_fixed_voltage0_supplies[] = {
	REGULATOR_SUPPLY("AVDD2", "1-001a"),
	REGULATOR_SUPPLY("CPVDD", "1-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage1_supplies[] = {
	REGULATOR_SUPPLY("SPKVDD1", "1-001a"),
	REGULATOR_SUPPLY("SPKVDD2", "1-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage2_supplies =
	REGULATOR_SUPPLY("DBVDD", "1-001a");

static struct regulator_init_data wm8994_fixed_voltage0_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage0_supplies),
	.consumer_supplies	= wm8994_fixed_voltage0_supplies,
};

static struct regulator_init_data wm8994_fixed_voltage1_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage1_supplies),
	.consumer_supplies	= wm8994_fixed_voltage1_supplies,
};

static struct regulator_init_data wm8994_fixed_voltage2_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_fixed_voltage2_supplies,
};

static struct fixed_voltage_config wm8994_fixed_voltage0_config = {
	.supply_name	= "VDD_1.8V",
	.microvolts	= 1800000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage0_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage1_config = {
	.supply_name	= "DC_5V",
	.microvolts	= 5000000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage1_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage2_config = {
	.supply_name	= "VDD_3.3V",
	.microvolts	= 3300000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage2_init_data,
};

static struct platform_device wm8994_fixed_voltage0 = {
	.name		= "reg-fixed-voltage",
	.id		= 0,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage0_config,
	},
};

static struct platform_device wm8994_fixed_voltage1 = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage1_config,
	},
};

static struct platform_device wm8994_fixed_voltage2 = {
	.name		= "reg-fixed-voltage",
	.id		= 2,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage2_config,
	},
};

static struct regulator_consumer_supply wm8994_avdd1_supply =
	REGULATOR_SUPPLY("AVDD1", "1-001a");

static struct regulator_consumer_supply wm8994_dcvdd_supply =
	REGULATOR_SUPPLY("DCVDD", "1-001a");

static struct regulator_init_data wm8994_ldo1_data = {
	.constraints	= {
		.name		= "AVDD1",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_avdd1_supply,
};

static struct regulator_init_data wm8994_ldo2_data = {
	.constraints	= {
		.name		= "DCVDD",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_dcvdd_supply,
};

static struct wm8994_pdata wm8994_platform_data = {
	/* configure gpio1 function: 0x0001(Logic level input/output) */
	.gpio_defaults[0] = 0x0001,
	/* If the i2s0 and i2s2 is enabled simultaneously */
	.gpio_defaults[7] = 0x8100, /* GPIO8  DACDAT3 in */
	.gpio_defaults[8] = 0x0100, /* GPIO9  ADCDAT3 out */
	.gpio_defaults[9] = 0x0100, /* GPIO10 LRCLK3  out */
	.gpio_defaults[10] = 0x0100,/* GPIO11 BCLK3   out */
	.ldo[0] = { 1, &wm8994_ldo1_data }, /* LDO1,LDO2 Enable */
	.ldo[1] = { 0, &wm8994_ldo2_data },
};

static struct i2c_board_info i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("wm8994", 0x1a),
		.platform_data	= &wm8994_platform_data,
	},
};

#ifdef CONFIG_SND_SAMSUNG_PCM
static struct platform_device exynos_smdk_pcm = {
	.name = "samsung-smdk-pcm",
	.id = -1,
};
#endif

static struct platform_device *smdk4x12_audio_devices[] __initdata = {
	&s3c_device_i2c1,
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos4_device_i2s0,
#endif
#ifdef CONFIG_SND_SAMSUNG_PCM
	&exynos4_device_pcm0,
	&exynos_smdk_pcm,
#endif
#ifdef CONFIG_SND_SAMSUNG_SPDIF
	&exynos4_device_spdif,
#endif
#if defined(CONFIG_SND_SAMSUNG_RP) || defined(CONFIG_SND_SAMSUNG_ALP)
	&exynos4_device_srp,
#endif
	&wm8994_fixed_voltage0,
	&wm8994_fixed_voltage1,
	&wm8994_fixed_voltage2,
	&samsung_asoc_dma,
	&samsung_asoc_idma,
};

void __init exynos4_smdk4x12_audio_init(void)
{
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	platform_add_devices(smdk4x12_audio_devices,
			ARRAY_SIZE(smdk4x12_audio_devices));
}
