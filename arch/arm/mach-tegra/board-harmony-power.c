/*
 * Copyright (C) 2010 NVIDIA, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps6586x.h>

#include <mach/irqs.h>

#include "board-harmony.h"

static struct regulator_consumer_supply tps658621_ldo0_supply[] = {
	REGULATOR_SUPPLY("pex_clk", NULL),
};

static struct regulator_init_data ldo0_data = {
	.constraints = {
		.min_uV = 3300 * 1000,
		.max_uV = 3300 * 1000,
		.valid_modes_mask = (REGULATOR_MODE_NORMAL |
				     REGULATOR_MODE_STANDBY),
		.valid_ops_mask = (REGULATOR_CHANGE_MODE |
				   REGULATOR_CHANGE_STATUS |
				   REGULATOR_CHANGE_VOLTAGE),
		.apply_uV = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(tps658621_ldo0_supply),
	.consumer_supplies = tps658621_ldo0_supply,
};

#define HARMONY_REGULATOR_INIT(_id, _minmv, _maxmv)			\
	static struct regulator_init_data _id##_data = {		\
		.constraints = {					\
			.min_uV = (_minmv)*1000,			\
			.max_uV = (_maxmv)*1000,			\
			.valid_modes_mask = (REGULATOR_MODE_NORMAL |	\
					     REGULATOR_MODE_STANDBY),	\
			.valid_ops_mask = (REGULATOR_CHANGE_MODE |	\
					   REGULATOR_CHANGE_STATUS |	\
					   REGULATOR_CHANGE_VOLTAGE),	\
		},							\
	}

HARMONY_REGULATOR_INIT(sm0, 725, 1500);
HARMONY_REGULATOR_INIT(sm1, 725, 1500);
HARMONY_REGULATOR_INIT(sm2, 3000, 4550);
HARMONY_REGULATOR_INIT(ldo1, 725, 1500);
HARMONY_REGULATOR_INIT(ldo2, 725, 1500);
HARMONY_REGULATOR_INIT(ldo3, 1250, 3300);
HARMONY_REGULATOR_INIT(ldo4, 1700, 2475);
HARMONY_REGULATOR_INIT(ldo5, 1250, 3300);
HARMONY_REGULATOR_INIT(ldo6, 1250, 3300);
HARMONY_REGULATOR_INIT(ldo7, 1250, 3300);
HARMONY_REGULATOR_INIT(ldo8, 1250, 3300);
HARMONY_REGULATOR_INIT(ldo9, 1250, 3300);

#define TPS_REG(_id, _data)			\
	{					\
		.id = TPS6586X_ID_##_id,	\
		.name = "tps6586x-regulator",	\
		.platform_data = _data,		\
	}

static struct tps6586x_subdev_info tps_devs[] = {
	TPS_REG(SM_0, &sm0_data),
	TPS_REG(SM_1, &sm1_data),
	TPS_REG(SM_2, &sm2_data),
	TPS_REG(LDO_0, &ldo0_data),
	TPS_REG(LDO_1, &ldo1_data),
	TPS_REG(LDO_2, &ldo2_data),
	TPS_REG(LDO_3, &ldo3_data),
	TPS_REG(LDO_4, &ldo4_data),
	TPS_REG(LDO_5, &ldo5_data),
	TPS_REG(LDO_6, &ldo6_data),
	TPS_REG(LDO_7, &ldo7_data),
	TPS_REG(LDO_8, &ldo8_data),
	TPS_REG(LDO_9, &ldo9_data),
};

static struct tps6586x_platform_data tps_platform = {
	.irq_base	= TEGRA_NR_IRQS,
	.num_subdevs	= ARRAY_SIZE(tps_devs),
	.subdevs	= tps_devs,
	.gpio_base	= HARMONY_GPIO_TPS6586X(0),
};

static struct i2c_board_info __initdata harmony_regulators[] = {
	{
		I2C_BOARD_INFO("tps6586x", 0x34),
		.irq		= INT_EXTERNAL_PMU,
		.platform_data	= &tps_platform,
	},
};

int __init harmony_regulator_init(void)
{
	i2c_register_board_info(3, harmony_regulators, 1);

	return 0;
}
