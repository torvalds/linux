/*
 * linux/arch/arm/mach-omap2/board-ldp.c
 *
 * Copyright (C) 2008 Texas Instruments Inc.
 * Nishant Kamat <nskamat@ti.com>
 *
 * Modified from mach-omap2/board-3430sdp.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board-ldp.h>
#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpmc.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <mach/control.h>

static void __init omap_ldp_init_irq(void)
{
	omap2_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static struct omap_uart_config ldp_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel ldp_config[] __initdata = {
	{ OMAP_TAG_UART,	&ldp_uart_config },
};

static int __init omap_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, NULL, 0);
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static void __init omap_ldp_init(void)
{
	omap_i2c_init();
	omap_board_config = ldp_config;
	omap_board_config_size = ARRAY_SIZE(ldp_config);
	omap_serial_init();
}

static void __init omap_ldp_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_LDP, "OMAP LDP board")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_ldp_map_io,
	.init_irq	= omap_ldp_init_irq,
	.init_machine	= omap_ldp_init,
	.timer		= &omap_timer,
MACHINE_END
