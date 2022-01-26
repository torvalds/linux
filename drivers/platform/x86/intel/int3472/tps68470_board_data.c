// SPDX-License-Identifier: GPL-2.0
/*
 * TI TPS68470 PMIC platform data definition.
 *
 * Copyright (c) 2021 Dan Scally <djrscally@gmail.com>
 * Copyright (c) 2021 Red Hat Inc.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/dmi.h>
#include <linux/gpio/machine.h>
#include <linux/platform_data/tps68470.h>
#include <linux/regulator/machine.h>
#include "tps68470.h"

static struct regulator_consumer_supply int347a_core_consumer_supplies[] = {
	REGULATOR_SUPPLY("dvdd", "i2c-INT347A:00"),
};

static struct regulator_consumer_supply int347a_ana_consumer_supplies[] = {
	REGULATOR_SUPPLY("avdd", "i2c-INT347A:00"),
};

static struct regulator_consumer_supply int347a_vcm_consumer_supplies[] = {
	REGULATOR_SUPPLY("vdd", "i2c-INT347A:00-VCM"),
};

static struct regulator_consumer_supply int347a_vsio_consumer_supplies[] = {
	REGULATOR_SUPPLY("dovdd", "i2c-INT347A:00"),
	REGULATOR_SUPPLY("vsio", "i2c-INT347A:00-VCM"),
};

static const struct regulator_init_data surface_go_tps68470_core_reg_init_data = {
	.constraints = {
		.min_uV = 1200000,
		.max_uV = 1200000,
		.apply_uV = true,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(int347a_core_consumer_supplies),
	.consumer_supplies = int347a_core_consumer_supplies,
};

static const struct regulator_init_data surface_go_tps68470_ana_reg_init_data = {
	.constraints = {
		.min_uV = 2815200,
		.max_uV = 2815200,
		.apply_uV = true,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(int347a_ana_consumer_supplies),
	.consumer_supplies = int347a_ana_consumer_supplies,
};

static const struct regulator_init_data surface_go_tps68470_vcm_reg_init_data = {
	.constraints = {
		.min_uV = 2815200,
		.max_uV = 2815200,
		.apply_uV = true,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(int347a_vcm_consumer_supplies),
	.consumer_supplies = int347a_vcm_consumer_supplies,
};

/* Ensure the always-on VIO regulator has the same voltage as VSIO */
static const struct regulator_init_data surface_go_tps68470_vio_reg_init_data = {
	.constraints = {
		.min_uV = 1800600,
		.max_uV = 1800600,
		.apply_uV = true,
		.always_on = true,
	},
};

static const struct regulator_init_data surface_go_tps68470_vsio_reg_init_data = {
	.constraints = {
		.min_uV = 1800600,
		.max_uV = 1800600,
		.apply_uV = true,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(int347a_vsio_consumer_supplies),
	.consumer_supplies = int347a_vsio_consumer_supplies,
};

static const struct tps68470_regulator_platform_data surface_go_tps68470_pdata = {
	.reg_init_data = {
		[TPS68470_CORE] = &surface_go_tps68470_core_reg_init_data,
		[TPS68470_ANA]  = &surface_go_tps68470_ana_reg_init_data,
		[TPS68470_VCM]  = &surface_go_tps68470_vcm_reg_init_data,
		[TPS68470_VIO] = &surface_go_tps68470_vio_reg_init_data,
		[TPS68470_VSIO] = &surface_go_tps68470_vsio_reg_init_data,
	},
};

static struct gpiod_lookup_table surface_go_tps68470_gpios = {
	.dev_id = "i2c-INT347A:00",
	.table = {
		GPIO_LOOKUP("tps68470-gpio", 9, "reset", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("tps68470-gpio", 7, "powerdown", GPIO_ACTIVE_LOW)
	}
};

static const struct int3472_tps68470_board_data surface_go_tps68470_board_data = {
	.dev_name = "i2c-INT3472:05",
	.tps68470_gpio_lookup_table = &surface_go_tps68470_gpios,
	.tps68470_regulator_pdata = &surface_go_tps68470_pdata,
};

static const struct int3472_tps68470_board_data surface_go3_tps68470_board_data = {
	.dev_name = "i2c-INT3472:01",
	.tps68470_gpio_lookup_table = &surface_go_tps68470_gpios,
	.tps68470_regulator_pdata = &surface_go_tps68470_pdata,
};

static const struct dmi_system_id int3472_tps68470_board_data_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Go"),
		},
		.driver_data = (void *)&surface_go_tps68470_board_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Go 2"),
		},
		.driver_data = (void *)&surface_go_tps68470_board_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Go 3"),
		},
		.driver_data = (void *)&surface_go3_tps68470_board_data,
	},
	{ }
};

const struct int3472_tps68470_board_data *int3472_tps68470_get_board_data(const char *dev_name)
{
	const struct int3472_tps68470_board_data *board_data;
	const struct dmi_system_id *match;

	for (match = dmi_first_match(int3472_tps68470_board_data_table);
	     match;
	     match = dmi_first_match(match + 1)) {
		board_data = match->driver_data;
		if (strcmp(board_data->dev_name, dev_name) == 0)
			return board_data;
	}

	return NULL;
}
