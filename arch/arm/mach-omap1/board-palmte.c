/*
 * linux/arch/arm/mach-omap1/board-palmte.c
 *
 * Modified from board-generic.c
 *
 * Support for the Palm Tungsten E PDA.
 *
 * Original version : Laurent Gonzalez
 *
 * Maintainters : http://palmtelinux.sf.net
 *                palmtelinux-developpers@lists.sf.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/board.h>
#include <asm/arch/common.h>
#include <asm/hardware/clock.h>

static void __init omap_generic_init_irq(void)
{
	omap_init_irq();
}

static struct omap_usb_config palmte_usb_config __initdata = {
	.register_dev	= 1,
	.hmc_mode	= 0,
	.pins[0]	= 3,
};

static struct omap_mmc_config palmte_mmc_config __initdata = {
	.mmc [0] = {
		.enabled 	= 1,
		.wire4		= 1,
		.wp_pin		= OMAP_MPUIO(3),
		.power_pin	= -1,
		.switch_pin	= -1,
	},
};

static struct omap_lcd_config palmte_lcd_config __initdata = {
	.panel_name	= "palmte",
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel palmte_config[] = {
	{ OMAP_TAG_USB, &palmte_usb_config },
	{ OMAP_TAG_MMC,	&palmte_mmc_config },
	{ OMAP_TAG_LCD,	&palmte_lcd_config },
};

static void __init omap_generic_init(void)
{
	omap_board_config = palmte_config;
	omap_board_config_size = ARRAY_SIZE(palmte_config);
}

static void __init omap_generic_map_io(void)
{
	omap_map_common_io();
}

MACHINE_START(OMAP_PALMTE, "OMAP310 based Palm Tungsten E")
	.phys_ram	= 0x10000000,
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_generic_map_io,
	.init_irq	= omap_generic_init_irq,
	.init_machine	= omap_generic_init,
	.timer		= &omap_timer,
MACHINE_END
