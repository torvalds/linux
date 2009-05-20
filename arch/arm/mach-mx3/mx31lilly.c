/*
 *  LILLY-1131 module support
 *
 *    Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  based on code for other MX31 boards,
 *
 *    Copyright 2005-2007 Freescale Semiconductor
 *    Copyright (c) 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 *    Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx3.h>
#include <mach/board-mx31lilly.h>

#include "devices.h"

/*
 * This file contains module-specific initialization routines for LILLY-1131.
 * Initialization of peripherals found on the baseboard is implemented in the
 * appropriate baseboard support code.
 */

static int mx31lilly_baseboard;
core_param(mx31lilly_baseboard, mx31lilly_baseboard, int, 0444);

static void __init mx31lilly_board_init(void)
{
	switch (mx31lilly_baseboard) {
	case MX31LILLY_NOBOARD:
		break;
	default:
		printk(KERN_ERR "Illegal mx31lilly_baseboard type %d\n",
			mx31lilly_baseboard);
	}
}

static void __init mx31lilly_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer mx31lilly_timer = {
	.init	= mx31lilly_timer_init,
};

MACHINE_START(LILLY1131, "INCO startec LILLY-1131")
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io		= mx31_map_io,
	.init_irq	= mxc_init_irq,
	.init_machine	= mx31lilly_board_init,
	.timer		= &mx31lilly_timer,
MACHINE_END

