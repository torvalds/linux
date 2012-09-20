/*
 * Code for TI8168/TI8148 EVM.
 *
 * Copyright (C) 2010 Texas Instruments, Inc. - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include <plat/usb.h>

static struct omap_musb_board_data musb_board_data = {
	.set_phy_power	= ti81xx_musb_phy_power,
	.interface_type	= MUSB_INTERFACE_ULPI,
	.mode           = MUSB_OTG,
	.power		= 500,
};

static void __init ti81xx_evm_init(void)
{
	omap_serial_init();
	omap_sdrc_init(NULL, NULL);
	usb_musb_init(&musb_board_data);
}

MACHINE_START(TI8168EVM, "ti8168evm")
	/* Maintainer: Texas Instruments */
	.atag_offset	= 0x100,
	.map_io		= ti81xx_map_io,
	.init_early	= ti81xx_init_early,
	.init_irq	= ti81xx_init_irq,
	.timer		= &omap3_timer,
	.init_machine	= ti81xx_evm_init,
	.init_late	= ti81xx_init_late,
	.restart	= omap_prcm_restart,
MACHINE_END

MACHINE_START(TI8148EVM, "ti8148evm")
	/* Maintainer: Texas Instruments */
	.atag_offset	= 0x100,
	.map_io		= ti81xx_map_io,
	.init_early	= ti81xx_init_early,
	.init_irq	= ti81xx_init_irq,
	.timer		= &omap3_timer,
	.init_machine	= ti81xx_evm_init,
	.init_late	= ti81xx_init_late,
	.restart	= omap_prcm_restart,
MACHINE_END
