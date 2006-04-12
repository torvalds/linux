/*
 * linux/arch/arm/mach-omap1/board-ams-delta.c
 *
 * Modified from board-generic.c
 *
 * Board specific inits for the Amstrad E3 (codename Delta) videophone
 *
 * Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/board-ams-delta.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/board.h>
#include <asm/arch/common.h>

static u8 ams_delta_latch1_reg;
static u16 ams_delta_latch2_reg;

void ams_delta_latch1_write(u8 mask, u8 value)
{
	ams_delta_latch1_reg &= ~mask;
	ams_delta_latch1_reg |= value;
	*(volatile __u8 *) AMS_DELTA_LATCH1_VIRT = ams_delta_latch1_reg;
}

void ams_delta_latch2_write(u16 mask, u16 value)
{
	ams_delta_latch2_reg &= ~mask;
	ams_delta_latch2_reg |= value;
	*(volatile __u16 *) AMS_DELTA_LATCH2_VIRT = ams_delta_latch2_reg;
}

static void __init ams_delta_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static struct map_desc ams_delta_io_desc[] __initdata = {
	// AMS_DELTA_LATCH1
	{
		.virtual	= AMS_DELTA_LATCH1_VIRT,
		.pfn		= __phys_to_pfn(AMS_DELTA_LATCH1_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	// AMS_DELTA_LATCH2
	{
		.virtual	= AMS_DELTA_LATCH2_VIRT,
		.pfn		= __phys_to_pfn(AMS_DELTA_LATCH2_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	// AMS_DELTA_MODEM
	{
		.virtual	= AMS_DELTA_MODEM_VIRT,
		.pfn		= __phys_to_pfn(AMS_DELTA_MODEM_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	}
};

static struct omap_uart_config ams_delta_uart_config __initdata = {
	.enabled_uarts = 1,
};

static struct omap_board_config_kernel ams_delta_config[] = {
	{ OMAP_TAG_UART,	&ams_delta_uart_config },
};

static void __init ams_delta_init(void)
{
	iotable_init(ams_delta_io_desc, ARRAY_SIZE(ams_delta_io_desc));

	omap_board_config = ams_delta_config;
	omap_board_config_size = ARRAY_SIZE(ams_delta_config);
	omap_serial_init();

	/* Clear latch2 (NAND, LCD, modem enable) */
	ams_delta_latch2_write(~0, 0);
}

static void __init ams_delta_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(AMS_DELTA, "Amstrad E3 (Delta)")
	/* Maintainer: Jonathan McDowell <noodles@earth.li> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= ams_delta_map_io,
	.init_irq	= ams_delta_init_irq,
	.init_machine	= ams_delta_init,
	.timer		= &omap_timer,
MACHINE_END

EXPORT_SYMBOL(ams_delta_latch1_write);
EXPORT_SYMBOL(ams_delta_latch2_write);
