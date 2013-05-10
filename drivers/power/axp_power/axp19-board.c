/*
 * drivers/power/axp_power/axp19-board.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/i2c.h>
#include <mach/irqs.h>
#include <linux/power_supply.h>
#include <linux/apm_bios.h>
#include <linux/apm-emulation.h>
#include <linux/mfd/axp-mfd.h>


#include "axp-cfg.h"


/* Reverse engineered partly from Platformx drivers */
enum axp_regls{

	vcc_ldo1,
	vcc_ldo2,
	vcc_ldo3,
	vcc_ldo4,
	vcc_ldo5,

	vcc_buck1,
	vcc_buck2,
	vcc_buck3,
	vcc_ldoio0,
};

/* The values of the various regulator constraints are obviously dependent
 * on exactly what is wired to each ldo.  Unfortunately this information is
 * not generally available.  More information has been requested from Xbow
 * but as of yet they haven't been forthcoming.
 *
 * Some of these are clearly Stargate 2 related (no way of plugging
 * in an lcd on the IM2 for example!).
 */

static struct regulator_consumer_supply ldo1_data[] = {
		{
			.supply = "axp19_rtc",
		},
	};


static struct regulator_consumer_supply ldo2_data[] = {
		{
			.supply = "axp19_analog/fm",
		},
	};

static struct regulator_consumer_supply ldo3_data[] = {
		{
			.supply = "axp19_pll/sdram",
		},
	};

static struct regulator_consumer_supply ldo4_data[] = {
		{
			.supply = "axp19_hdmi",
		},
	};

static struct regulator_consumer_supply ldoio0_data[] = {
		{
			.supply = "axp19_mic",
		},
	};

static struct regulator_consumer_supply buck1_data[] = {
		{
			.supply = "axp19_io",
		},
	};

static struct regulator_consumer_supply buck2_data[] = {
		{
			.supply = "axp19_core",
		},
	};

static struct regulator_consumer_supply buck3_data[] = {
		{
			.supply = "axp19_ddr",
		},
	};



static struct regulator_init_data axp_regl_init_data[] = {
	[vcc_ldo1] = {
		.constraints = { /* board default 1.25V */
			.name = "axp19_ldo1",
			.min_uV =  AXP19LDO1 * 1000,
			.max_uV =  AXP19LDO1 * 1000,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo1_data),
		.consumer_supplies = ldo1_data,
	},
	[vcc_ldo2] = {
		.constraints = { /* board default 3.0V */
			.name = "axp19_ldo2",
			.min_uV = 1800000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo2_data),
		.consumer_supplies = ldo2_data,

	},
	[vcc_ldo3] = {
		.constraints = {/* default is 1.8V */
			.name = "axp19_ldo3",
			.min_uV =  700 * 1000,
			.max_uV =  3500* 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo3_data),
		.consumer_supplies = ldo3_data,

	},
	[vcc_ldo4] = {
		.constraints = {
			/* board default is 3.3V */
			.name = "axp19_ldo4",
			.min_uV = 1800000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo4_data),
		.consumer_supplies = ldo4_data,

	},
	[vcc_buck1] = {
		.constraints = { /* default 3.3V */
			.name = "axp19_buck1",
			.min_uV = 700000,
			.max_uV = 3500000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck1_data),
		.consumer_supplies = buck1_data,


	},
	[vcc_buck2] = {
		.constraints = { /* default 1.24V */
			.name = "axp19_buck2",
			.min_uV = 700 * 1000,
			.max_uV = 2275 * 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck2_data),
		.consumer_supplies = buck2_data,


	},
	[vcc_buck3] = {
		.constraints = { /* default 2.5V */
			.name = "axp19_buck3",
			.min_uV = 700 * 1000,
			.max_uV = 3500 * 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(buck3_data),
		.consumer_supplies = buck3_data,
	},
	[vcc_ldoio0] = {
		.constraints = { /* default 2.5V */
			.name = "axp19_ldoio0",
			.min_uV = 1800 * 1000,
			.max_uV = 3300 * 1000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldoio0_data),
		.consumer_supplies = ldoio0_data,
	},
};

static struct axp_funcdev_info axp_regldevs[] = {
	{
		.name = "axp19-regulator",
		.id = AXP19_ID_LDO1,
		.platform_data = &axp_regl_init_data[vcc_ldo1],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_LDO2,
		.platform_data = &axp_regl_init_data[vcc_ldo2],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_LDO3,
		.platform_data = &axp_regl_init_data[vcc_ldo3],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_LDO4,
		.platform_data = &axp_regl_init_data[vcc_ldo4],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_BUCK1,
		.platform_data = &axp_regl_init_data[vcc_buck1],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_BUCK2,
		.platform_data = &axp_regl_init_data[vcc_buck2],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_BUCK3,
		.platform_data = &axp_regl_init_data[vcc_buck3],
	}, {
		.name = "axp19-regulator",
		.id = AXP19_ID_LDOIO0,
		.platform_data = &axp_regl_init_data[vcc_ldoio0],
	},
};

static struct power_supply_info battery_data ={
        .name ="PTI PL336078",
		.technology = POWER_SUPPLY_TECHNOLOGY_LION,
		.voltage_max_design = 4200000,
	    .voltage_min_design = 2700000,
	    .charge_full_design = 1450,
	    .energy_full_design = 1450,
	    .use_for_apm = 1,
};

static void axp_battery_low(void)
{
#if defined(CONFIG_APM_EMULATION)
	apm_queue_event(APM_LOW_BATTERY);
#endif
}

static void axp_battery_critical(void)
{
#if defined(CONFIG_APM_EMULATION)
	apm_queue_event(APM_CRITICAL_SUSPEND);
#endif
}



static struct axp_supply_init_data axp_sply_init_data = {
        .battery_info = &battery_data,
        .chgcur = 700,
        .chgvol = 4200,
        .chgend =  60,
        .chgen = 1,
        //.limit_on = 1,
        .sample_time = 25,
        .chgpretime = 40,
        .chgcsttime = 480,

		.battery_low = axp_battery_low,
	    .battery_critical = axp_battery_critical,
};

static struct axp_funcdev_info axp_splydev[]={
   	{   .name = "axp19-supplyer",
		.id = AXP19_ID_SUPPLY,
        .platform_data = &axp_sply_init_data,
    },
};

static struct axp_funcdev_info axp_gpiodev[]={
   	{   .name = "axp19-gpio",
   		.id = AXP19_ID_GPIO,
    },
};

static struct axp_platform_data axp_pdata = {
	.num_regl_devs = ARRAY_SIZE(axp_regldevs),
	.num_sply_devs = ARRAY_SIZE(axp_splydev),
	.num_gpio_devs = ARRAY_SIZE(axp_gpiodev),
	.regl_devs = axp_regldevs,
	.sply_devs = axp_splydev,
	.gpio_devs = axp_gpiodev,
};

static struct i2c_board_info __initdata axp_mfd_i2c_board_info[] = {
	{
		.type = "axp19_mfd",
		.addr = AXP19_ADDR,
		.platform_data = &axp_pdata,
		.irq = SW_INT_IRQNO_ENMI,
	},
};

static int __init axp_board_init(void)
{
	return i2c_register_board_info(AXP19_I2CBUS, axp_mfd_i2c_board_info,
				ARRAY_SIZE(axp_mfd_i2c_board_info));
}
fs_initcall(axp_board_init);

MODULE_DESCRIPTION("Krosspower axp board");
MODULE_AUTHOR("Donglu Zhang Krosspower");
MODULE_LICENSE("GPL");

