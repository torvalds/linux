/*
 * linux/arch/arm/mach-omap2/board-n8x0.c
 *
 * Copyright (C) 2005-2009 Nokia Corporation
 * Author: Juha Yrjola <juha.yrjola@nokia.com>
 *
 * Modified from mach-omap2/board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/stddef.h>
#include <linux/spi/spi.h>
#include <linux/usb/musb.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/board.h>
#include <plat/common.h>
#include <mach/irqs.h>
#include <plat/mcspi.h>
#include <plat/onenand.h>
#include <plat/serial.h>

static struct omap2_mcspi_device_config p54spi_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel = 1,
};

static struct spi_board_info n800_spi_board_info[] __initdata = {
	{
		.modalias	= "p54spi",
		.bus_num	= 2,
		.chip_select	= 0,
		.max_speed_hz   = 48000000,
		.controller_data = &p54spi_mcspi_config,
	},
};

#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)

static struct mtd_partition onenand_partitions[] = {
	{
		.name           = "bootloader",
		.offset         = 0,
		.size           = 0x20000,
		.mask_flags     = MTD_WRITEABLE,	/* Force read-only */
	},
	{
		.name           = "config",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x60000,
	},
	{
		.name           = "kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x200000,
	},
	{
		.name           = "initfs",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x400000,
	},
	{
		.name           = "rootfs",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_onenand_platform_data board_onenand_data = {
	.cs		= 0,
	.gpio_irq	= 26,
	.parts		= onenand_partitions,
	.nr_parts	= ARRAY_SIZE(onenand_partitions),
	.flags		= ONENAND_SYNC_READ,
};

static void __init n8x0_onenand_init(void)
{
	gpmc_onenand_init(&board_onenand_data);
}

#else

static void __init n8x0_onenand_init(void) {}

#endif

static void __init n8x0_map_io(void)
{
	omap2_set_globals_242x();
	omap2_map_common_io();
}

static void __init n8x0_init_irq(void)
{
	omap2_init_common_hw(NULL, NULL);
	omap_init_irq();
	omap_gpio_init();
}

static void __init n8x0_init_machine(void)
{
	/* FIXME: add n810 spi devices */
	spi_register_board_info(n800_spi_board_info,
				ARRAY_SIZE(n800_spi_board_info));

	omap_serial_init();
	n8x0_onenand_init();
}

MACHINE_START(NOKIA_N800, "Nokia N800")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= n8x0_map_io,
	.init_irq	= n8x0_init_irq,
	.init_machine	= n8x0_init_machine,
	.timer		= &omap_timer,
MACHINE_END

MACHINE_START(NOKIA_N810, "Nokia N810")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= n8x0_map_io,
	.init_irq	= n8x0_init_irq,
	.init_machine	= n8x0_init_machine,
	.timer		= &omap_timer,
MACHINE_END

MACHINE_START(NOKIA_N810_WIMAX, "Nokia N810 WiMAX")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= n8x0_map_io,
	.init_irq	= n8x0_init_irq,
	.init_machine	= n8x0_init_machine,
	.timer		= &omap_timer,
MACHINE_END
