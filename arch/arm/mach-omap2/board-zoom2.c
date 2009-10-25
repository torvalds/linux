/*
 * Copyright (C) 2009 Texas Instruments Inc.
 * Mikkel Christensen <mlc@ti.com>
 *
 * Modified from mach-omap2/board-ldp.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/i2c/twl4030.h>
#include <linux/regulator/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/usb.h>
#include <mach/keypad.h>

#include "mmc-twl4030.h"
#include "sdram-micron-mt46h32m32lf-6.h"

/* Zoom2 has Qwerty keyboard*/
static int board_keymap[] = {
	KEY(0, 0, KEY_E),
	KEY(1, 0, KEY_R),
	KEY(2, 0, KEY_T),
	KEY(3, 0, KEY_HOME),
	KEY(6, 0, KEY_I),
	KEY(7, 0, KEY_LEFTSHIFT),
	KEY(0, 1, KEY_D),
	KEY(1, 1, KEY_F),
	KEY(2, 1, KEY_G),
	KEY(3, 1, KEY_SEND),
	KEY(6, 1, KEY_K),
	KEY(7, 1, KEY_ENTER),
	KEY(0, 2, KEY_X),
	KEY(1, 2, KEY_C),
	KEY(2, 2, KEY_V),
	KEY(3, 2, KEY_END),
	KEY(6, 2, KEY_DOT),
	KEY(7, 2, KEY_CAPSLOCK),
	KEY(0, 3, KEY_Z),
	KEY(1, 3, KEY_KPPLUS),
	KEY(2, 3, KEY_B),
	KEY(3, 3, KEY_F1),
	KEY(6, 3, KEY_O),
	KEY(7, 3, KEY_SPACE),
	KEY(0, 4, KEY_W),
	KEY(1, 4, KEY_Y),
	KEY(2, 4, KEY_U),
	KEY(3, 4, KEY_F2),
	KEY(4, 4, KEY_VOLUMEUP),
	KEY(6, 4, KEY_L),
	KEY(7, 4, KEY_LEFT),
	KEY(0, 5, KEY_S),
	KEY(1, 5, KEY_H),
	KEY(2, 5, KEY_J),
	KEY(3, 5, KEY_F3),
	KEY(5, 5, KEY_VOLUMEDOWN),
	KEY(6, 5, KEY_M),
	KEY(4, 5, KEY_ENTER),
	KEY(7, 5, KEY_RIGHT),
	KEY(0, 6, KEY_Q),
	KEY(1, 6, KEY_A),
	KEY(2, 6, KEY_N),
	KEY(3, 6, KEY_BACKSPACE),
	KEY(6, 6, KEY_P),
	KEY(7, 6, KEY_UP),
	KEY(6, 7, KEY_SELECT),
	KEY(7, 7, KEY_DOWN),
	KEY(0, 7, KEY_PROG1),	/*MACRO 1 <User defined> */
	KEY(1, 7, KEY_PROG2),	/*MACRO 2 <User defined> */
	KEY(2, 7, KEY_PROG3),	/*MACRO 3 <User defined> */
	KEY(3, 7, KEY_PROG4),	/*MACRO 4 <User defined> */
	0
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data zoom2_kp_twl4030_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 8,
	.rep		= 1,
};

static struct omap_board_config_kernel zoom2_config[] __initdata = {
};

static struct regulator_consumer_supply zoom2_vmmc1_supply = {
	.supply		= "vmmc",
};

static struct regulator_consumer_supply zoom2_vsim_supply = {
	.supply		= "vmmc_aux",
};

static struct regulator_consumer_supply zoom2_vmmc2_supply = {
	.supply		= "vmmc",
};

/* VMMC1 for OMAP VDD_MMC1 (i/o) and MMC1 card */
static struct regulator_init_data zoom2_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = 1,
	.consumer_supplies      = &zoom2_vmmc1_supply,
};

/* VMMC2 for MMC2 card */
static struct regulator_init_data zoom2_vmmc2 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 1850000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = 1,
	.consumer_supplies      = &zoom2_vmmc2_supply,
};

/* VSIM for OMAP VDD_MMC1A (i/o for DAT4..DAT7) */
static struct regulator_init_data zoom2_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = 1,
	.consumer_supplies      = &zoom2_vsim_supply,
};

static struct twl4030_hsmmc_info mmc[] __initdata = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_wp	= -EINVAL,
	},
	{
		.mmc		= 2,
		.wires		= 4,
		.gpio_wp	= -EINVAL,
	},
	{}      /* Terminator */
};

static int zoom2_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ),
	 * gpio + 1 is "mmc1_cd" (input/IRQ)
	 */
	mmc[0].gpio_cd = gpio + 0;
	mmc[1].gpio_cd = gpio + 1;
	twl4030_mmc_init(mmc);

	/* link regulators to MMC adapters ... we "know" the
	 * regulators will be set up only *after* we return.
	*/
	zoom2_vmmc1_supply.dev = mmc[0].dev;
	zoom2_vsim_supply.dev = mmc[0].dev;
	zoom2_vmmc2_supply.dev = mmc[1].dev;

	return 0;
}


static int zoom2_batt_table[] = {
/* 0 C*/
30800, 29500, 28300, 27100,
26000, 24900, 23900, 22900, 22000, 21100, 20300, 19400, 18700, 17900,
17200, 16500, 15900, 15300, 14700, 14100, 13600, 13100, 12600, 12100,
11600, 11200, 10800, 10400, 10000, 9630,  9280,  8950,  8620,  8310,
8020,  7730,  7460,  7200,  6950,  6710,  6470,  6250,  6040,  5830,
5640,  5450,  5260,  5090,  4920,  4760,  4600,  4450,  4310,  4170,
4040,  3910,  3790,  3670,  3550
};

static struct twl4030_bci_platform_data zoom2_bci_data = {
	.battery_tmp_tbl	= zoom2_batt_table,
	.tblsize		= ARRAY_SIZE(zoom2_batt_table),
};

static struct twl4030_usb_data zoom2_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static void __init omap_zoom2_init_irq(void)
{
	omap_board_config = zoom2_config;
	omap_board_config_size = ARRAY_SIZE(zoom2_config);
	omap2_init_common_hw(mt46h32m32lf6_sdrc_params,
				 mt46h32m32lf6_sdrc_params);
	omap_init_irq();
	omap_gpio_init();
}

static struct twl4030_gpio_platform_data zoom2_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.setup		= zoom2_twl_gpio_setup,
};

static struct twl4030_madc_platform_data zoom2_madc_data = {
	.irq_line	= 1,
};

static struct twl4030_platform_data zoom2_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.bci		= &zoom2_bci_data,
	.madc		= &zoom2_madc_data,
	.usb		= &zoom2_usb_data,
	.gpio		= &zoom2_gpio_data,
	.keypad		= &zoom2_kp_twl4030_data,
	.vmmc1          = &zoom2_vmmc1,
	.vmmc2          = &zoom2_vmmc2,
	.vsim           = &zoom2_vsim,

};

static struct i2c_board_info __initdata zoom2_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags		= I2C_CLIENT_WAKE,
		.irq		= INT_34XX_SYS_NIRQ,
		.platform_data	= &zoom2_twldata,
	},
};

static int __init omap_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, zoom2_i2c_boardinfo,
			ARRAY_SIZE(zoom2_i2c_boardinfo));
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

extern int __init omap_zoom2_debugboard_init(void);

static void __init omap_zoom2_init(void)
{
	omap_i2c_init();
	omap_serial_init();
	omap_zoom2_debugboard_init();
	usb_musb_init();
}

static void __init omap_zoom2_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_ZOOM2, "OMAP Zoom2 board")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_zoom2_map_io,
	.init_irq	= omap_zoom2_init_irq,
	.init_machine	= omap_zoom2_init,
	.timer		= &omap_timer,
MACHINE_END
