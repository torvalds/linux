/*
 * arch/arm/mach-ep93xx/snappercl15.c
 * Bluewater Systems Snapper CL15 system module
 *
 * Copyright (C) 2009 Bluewater Systems Ltd
 * Author: Ryan Mallon
 *
 * NAND code adapted from driver by:
 *   Andre Renaud <andre@bluewatersys.com>
 *   James R. McKaskill
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/fb.h>

#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>

#include <mach/hardware.h>
#include <mach/fb.h>
#include <mach/gpio-ep93xx.h>

#include <asm/hardware/vic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "soc.h"

#define SNAPPERCL15_NAND_BASE	(EP93XX_CS7_PHYS_BASE + SZ_16M)

#define SNAPPERCL15_NAND_WPN	(1 << 8)  /* Write protect (active low) */
#define SNAPPERCL15_NAND_ALE	(1 << 9)  /* Address latch */
#define SNAPPERCL15_NAND_CLE	(1 << 10) /* Command latch */
#define SNAPPERCL15_NAND_CEN	(1 << 11) /* Chip enable (active low) */
#define SNAPPERCL15_NAND_RDY	(1 << 14) /* Device ready */

#define NAND_CTRL_ADDR(chip) 	(chip->IO_ADDR_W + 0x40)

static void snappercl15_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
				      unsigned int ctrl)
{
	struct nand_chip *chip = mtd->priv;
	static u16 nand_state = SNAPPERCL15_NAND_WPN;
	u16 set;

	if (ctrl & NAND_CTRL_CHANGE) {
		set = SNAPPERCL15_NAND_CEN | SNAPPERCL15_NAND_WPN;

		if (ctrl & NAND_NCE)
			set &= ~SNAPPERCL15_NAND_CEN;
		if (ctrl & NAND_CLE)
			set |= SNAPPERCL15_NAND_CLE;
		if (ctrl & NAND_ALE)
			set |= SNAPPERCL15_NAND_ALE;

		nand_state &= ~(SNAPPERCL15_NAND_CEN |
				SNAPPERCL15_NAND_CLE |
				SNAPPERCL15_NAND_ALE);
		nand_state |= set;
		__raw_writew(nand_state, NAND_CTRL_ADDR(chip));
	}

	if (cmd != NAND_CMD_NONE)
		__raw_writew((cmd & 0xff) | nand_state, chip->IO_ADDR_W);
}

static int snappercl15_nand_dev_ready(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;

	return !!(__raw_readw(NAND_CTRL_ADDR(chip)) & SNAPPERCL15_NAND_RDY);
}

static const char *snappercl15_nand_part_probes[] = {"cmdlinepart", NULL};

static struct mtd_partition snappercl15_nand_parts[] = {
	{
		.name		= "Kernel",
		.offset		= 0,
		.size		= SZ_2M,
	},
	{
		.name		= "Filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct platform_nand_data snappercl15_nand_data = {
	.chip = {
		.nr_chips		= 1,
		.part_probe_types	= snappercl15_nand_part_probes,
		.partitions		= snappercl15_nand_parts,
		.nr_partitions		= ARRAY_SIZE(snappercl15_nand_parts),
		.options		= NAND_NO_AUTOINCR,
		.chip_delay		= 25,
	},
	.ctrl = {
		.dev_ready		= snappercl15_nand_dev_ready,
		.cmd_ctrl		= snappercl15_nand_cmd_ctrl,
	},
};

static struct resource snappercl15_nand_resource[] = {
	{
		.start		= SNAPPERCL15_NAND_BASE,
		.end		= SNAPPERCL15_NAND_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device snappercl15_nand_device = {
	.name			= "gen_nand",
	.id			= -1,
	.dev.platform_data	= &snappercl15_nand_data,
	.resource		= snappercl15_nand_resource,
	.num_resources		= ARRAY_SIZE(snappercl15_nand_resource),
};

static struct ep93xx_eth_data __initdata snappercl15_eth_data = {
	.phy_id			= 1,
};

static struct i2c_gpio_platform_data __initdata snappercl15_i2c_gpio_data = {
	.sda_pin		= EP93XX_GPIO_LINE_EEDAT,
	.sda_is_open_drain	= 0,
	.scl_pin		= EP93XX_GPIO_LINE_EECLK,
	.scl_is_open_drain	= 0,
	.udelay			= 0,
	.timeout		= 0,
};

static struct i2c_board_info __initdata snappercl15_i2c_data[] = {
	{
		/* Audio codec */
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	},
};

static struct ep93xxfb_mach_info __initdata snappercl15_fb_info = {
	.num_modes		= EP93XXFB_USE_MODEDB,
	.bpp			= 16,
};

static struct platform_device snappercl15_audio_device = {
	.name		= "snappercl15-audio",
	.id		= -1,
};

static void __init snappercl15_register_audio(void)
{
	ep93xx_register_i2s();
	platform_device_register(&snappercl15_audio_device);
}

static void __init snappercl15_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_eth(&snappercl15_eth_data, 1);
	ep93xx_register_i2c(&snappercl15_i2c_gpio_data, snappercl15_i2c_data,
			    ARRAY_SIZE(snappercl15_i2c_data));
	ep93xx_register_fb(&snappercl15_fb_info);
	snappercl15_register_audio();
	platform_device_register(&snappercl15_nand_device);
}

MACHINE_START(SNAPPER_CL15, "Bluewater Systems Snapper CL15")
	/* Maintainer: Ryan Mallon */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.handle_irq	= vic_handle_irq,
	.timer 		= &ep93xx_timer,
	.init_machine	= snappercl15_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
