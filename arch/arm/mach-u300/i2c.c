/*
 * arch/arm/mach-u300/i2c.c
 *
 * Copyright (C) 2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 *
 * Register board i2c devices
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/mfd/ab3100.h>
#include <linux/regulator/machine.h>
#include <linux/amba/bus.h>
#include <mach/irqs.h>

/*
 * Initial settings of ab3100 registers.
 * Common for below LDO regulator settings are that
 * bit 7-5 controls voltage. Bit 4 turns regulator ON(1) or OFF(0).
 * Bit 3-2 controls sleep enable and bit 1-0 controls sleep mode.
 */

/* LDO_A 0x16: 2.75V, ON, SLEEP_A, SLEEP OFF GND */
#define LDO_A_SETTING		0x16
/* LDO_C 0x10: 2.65V, ON, SLEEP_A or B, SLEEP full power */
#define LDO_C_SETTING		0x10
/* LDO_D 0x10: 2.65V, ON, sleep mode not used */
#define LDO_D_SETTING		0x10
/* LDO_E 0x10: 1.8V, ON, SLEEP_A or B, SLEEP full power */
#define LDO_E_SETTING		0x10
/* LDO_E SLEEP 0x00: 1.8V, not used, SLEEP_A or B, not used */
#define LDO_E_SLEEP_SETTING	0x00
/* LDO_F 0xD0: 2.5V, ON, SLEEP_A or B, SLEEP full power */
#define LDO_F_SETTING		0xD0
/* LDO_G 0x00: 2.85V, OFF, SLEEP_A or B, SLEEP full power */
#define LDO_G_SETTING		0x00
/* LDO_H 0x18: 2.75V, ON, SLEEP_B, SLEEP full power */
#define LDO_H_SETTING		0x18
/* LDO_K 0x00: 2.75V, OFF, SLEEP_A or B, SLEEP full power */
#define LDO_K_SETTING		0x00
/* LDO_EXT 0x00: Voltage not set, OFF, not used, not used */
#define LDO_EXT_SETTING		0x00
/* BUCK 0x7D: 1.2V, ON, SLEEP_A and B, SLEEP low power */
#define BUCK_SETTING	0x7D
/* BUCK SLEEP 0xAC: 1.05V, Not used, SLEEP_A and B, Not used */
#define BUCK_SLEEP_SETTING	0xAC

#ifdef CONFIG_AB3100_CORE
static struct regulator_consumer_supply supply_ldo_c[] = {
	{
		.dev_name = "ab3100-codec",
		.supply = "vaudio", /* Powers the codec */
	},
};

/*
 * This one needs to be a supply so we can turn it off
 * in order to shut down the system.
 */
static struct regulator_consumer_supply supply_ldo_d[] = {
	{
		.supply = "vana15", /* Powers the SoC (CPU etc) */
	},
};

static struct regulator_consumer_supply supply_ldo_g[] = {
	{
		.dev_name = "mmci",
		.supply = "vmmc", /* Powers MMC/SD card */
	},
};

static struct regulator_consumer_supply supply_ldo_h[] = {
	{
		.dev_name = "xgam_pdi",
		.supply = "vdisp", /* Powers camera, display etc */
	},
};

static struct regulator_consumer_supply supply_ldo_k[] = {
	{
		.dev_name = "irda",
		.supply = "vir", /* Power IrDA */
	},
};

/*
 * This is a placeholder for whoever wish to use the
 * external power.
 */
static struct regulator_consumer_supply supply_ldo_ext[] = {
	{
		.supply = "vext", /* External power */
	},
};

/* Preset (hardware defined) voltages for these regulators */
#define LDO_A_VOLTAGE 2750000
#define LDO_C_VOLTAGE 2650000
#define LDO_D_VOLTAGE 2650000

static struct ab3100_platform_data ab3100_plf_data = {
	.reg_constraints = {
		/* LDO A routing and constraints */
		{
			.constraints = {
				.name = "vrad",
				.min_uV = LDO_A_VOLTAGE,
				.max_uV = LDO_A_VOLTAGE,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.always_on = 1,
				.boot_on = 1,
			},
		},
		/* LDO C routing and constraints */
		{
			.constraints = {
				.min_uV = LDO_C_VOLTAGE,
				.max_uV = LDO_C_VOLTAGE,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
			},
			.num_consumer_supplies = ARRAY_SIZE(supply_ldo_c),
			.consumer_supplies = supply_ldo_c,
		},
		/* LDO D routing and constraints */
		{
			.constraints = {
				.min_uV = LDO_D_VOLTAGE,
				.max_uV = LDO_D_VOLTAGE,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask = REGULATOR_CHANGE_STATUS,
				/*
				 * Actually this is boot_on but we need
				 * to reference count it externally to
				 * be able to shut down the system.
				 */
			},
			.num_consumer_supplies = ARRAY_SIZE(supply_ldo_d),
			.consumer_supplies = supply_ldo_d,
		},
		/* LDO E routing and constraints */
		{
			.constraints = {
				.name = "vio",
				.min_uV = 1800000,
				.max_uV = 1800000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.always_on = 1,
				.boot_on = 1,
			},
		},
		/* LDO F routing and constraints */
		{
			.constraints = {
				.name = "vana25",
				.min_uV = 2500000,
				.max_uV = 2500000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.always_on = 1,
				.boot_on = 1,
			},
		},
		/* LDO G routing and constraints */
		{
			.constraints = {
				.min_uV = 1500000,
				.max_uV = 2850000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask =
				REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS,
			},
			.num_consumer_supplies = ARRAY_SIZE(supply_ldo_g),
			.consumer_supplies = supply_ldo_g,
		},
		/* LDO H routing and constraints */
		{
			.constraints = {
				.min_uV = 1200000,
				.max_uV = 2750000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask =
				REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS,
			},
			.num_consumer_supplies = ARRAY_SIZE(supply_ldo_h),
			.consumer_supplies = supply_ldo_h,
		},
		/* LDO K routing and constraints */
		{
			.constraints = {
				.min_uV = 1800000,
				.max_uV = 2750000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask =
				REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS,
			},
			.num_consumer_supplies = ARRAY_SIZE(supply_ldo_k),
			.consumer_supplies = supply_ldo_k,
		},
		/* External regulator interface. No fixed voltage specified.
		 * If we knew the voltage of the external regulator and it
		 * was connected on the board, we could add the (fixed)
		 * voltage for it here.
		 */
		{
			.constraints = {
				.min_uV = 0,
				.max_uV = 0,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask =
				REGULATOR_CHANGE_STATUS,
			},
			.num_consumer_supplies = ARRAY_SIZE(supply_ldo_ext),
			.consumer_supplies = supply_ldo_ext,
		},
		/* Buck converter routing and constraints */
		{
			.constraints = {
				.name = "vcore",
				.min_uV = 1200000,
				.max_uV = 1800000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask =
				REGULATOR_CHANGE_VOLTAGE,
				.always_on = 1,
				.boot_on = 1,
			},
		},
	},
	.reg_initvals = {
		LDO_A_SETTING,
		LDO_C_SETTING,
		LDO_E_SETTING,
		LDO_E_SLEEP_SETTING,
		LDO_F_SETTING,
		LDO_G_SETTING,
		LDO_H_SETTING,
		LDO_K_SETTING,
		LDO_EXT_SETTING,
		BUCK_SETTING,
		BUCK_SLEEP_SETTING,
		LDO_D_SETTING,
	},
};
#endif

static struct i2c_board_info __initdata bus0_i2c_board_info[] = {
#ifdef CONFIG_AB3100_CORE
	{
		.type = "ab3100",
		.addr = 0x48,
		.irq = IRQ_U300_IRQ0_EXT,
		.platform_data = &ab3100_plf_data,
	},
#else
	{ },
#endif
};

static struct i2c_board_info __initdata bus1_i2c_board_info[] = {
#ifdef CONFIG_MACH_U300_BS335
	{
		.type = "fwcam",
		.addr = 0x10,
	},
	{
		.type = "fwcam",
		.addr = 0x5d,
	},
#else
	{ },
#endif
};

void __init u300_i2c_register_board_devices(void)
{
	i2c_register_board_info(0, bus0_i2c_board_info,
				ARRAY_SIZE(bus0_i2c_board_info));
	/*
	 * This makes the core shut down all unused regulators
	 * after all the initcalls have completed.
	 */
	regulator_has_full_constraints();
	i2c_register_board_info(1, bus1_i2c_board_info,
				ARRAY_SIZE(bus1_i2c_board_info));
}
