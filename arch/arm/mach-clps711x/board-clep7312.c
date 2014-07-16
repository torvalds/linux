/*
 *  linux/arch/arm/mach-clps711x/clep7312.c
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
#include <linux/types.h>
#include <linux/string.h>
#include <linux/memblock.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "common.h"

static void __init
fixup_clep7312(struct tag *tags, char **cmdline)
{
	memblock_add(0xc0000000, 0x01000000);
}

MACHINE_START(CLEP7212, "Cirrus Logic 7212/7312")
	/* Maintainer: Nobody */
	.atag_offset	= 0x0100,
	.fixup		= fixup_clep7312,
	.map_io		= clps711x_map_io,
	.init_early	= clps711x_init_early,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.restart	= clps711x_restart,
MACHINE_END
