/*
 *  linux/arch/arm/mach-clps711x/core.c
 *
 *  Core support for the CLPS711x-based machines.
 *
 *  Copyright (C) 2001,2011 Deep Blue Solutions Ltd
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

#include <linux/init.h>
#include <linux/sizes.h>

#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include <mach/hardware.h>

#include "common.h"

/*
 * This maps the generic CLPS711x registers
 */
static struct map_desc clps711x_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)CLPS711X_VIRT_BASE,
		.pfn		= __phys_to_pfn(CLPS711X_PHYS_BASE),
		.length		= 48 * SZ_1K,
		.type		= MT_DEVICE,
	}
};

void __init clps711x_map_io(void)
{
	iotable_init(clps711x_io_desc, ARRAY_SIZE(clps711x_io_desc));
}

void __init clps711x_init_irq(void)
{
	clps711x_intc_init(CLPS711X_PHYS_BASE, SZ_16K);
}

void __init clps711x_timer_init(void)
{
	clps711x_clk_init(CLPS711X_VIRT_BASE);
	clps711x_clksrc_init(CLPS711X_VIRT_BASE + TC1D,
			     CLPS711X_VIRT_BASE + TC2D, IRQ_TC2OI);
}

void clps711x_restart(enum reboot_mode mode, const char *cmd)
{
	soft_restart(0);
}
