/*
 *  linux/arch/arm/mach-clps711x/fortunet.c
 *
 *  Derived from linux/arch/arm/mach-integrator/arch.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
#include <linux/initrd.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include <asm/memory.h>

#include "common.h"

struct meminfo memmap = {
	.nr_banks	= 1,
	.bank		= {
		{
			.start	= 0xC0000000,
			.size	= 0x01000000,
		},
	},
};

typedef struct tag_IMAGE_PARAMS
{
	int	ramdisk_ok;
	int	ramdisk_address;
	int	ramdisk_size;
	int	ram_size;
	int	extra_param_type;
	int	extra_param_ptr;
	int	command_line;
} IMAGE_PARAMS;

#define IMAGE_PARAMS_PHYS	0xC01F0000

static void __init
fortunet_fixup(struct tag *tags, char **cmdline, struct meminfo *mi)
{
	IMAGE_PARAMS *ip = phys_to_virt(IMAGE_PARAMS_PHYS);
	*cmdline = phys_to_virt(ip->command_line);
#ifdef CONFIG_BLK_DEV_INITRD
	if(ip->ramdisk_ok)
	{
		initrd_start = __phys_to_virt(ip->ramdisk_address);
		initrd_end = initrd_start + ip->ramdisk_size;
	}
#endif
	memmap.bank[0].size = ip->ram_size;
	*mi = memmap;
}

MACHINE_START(FORTUNET, "ARM-FortuNet")
	/* Maintainer: FortuNet Inc. */
	.nr_irqs	= CLPS711X_NR_IRQS,
	.fixup		= fortunet_fixup,
	.map_io		= clps711x_map_io,
	.init_early	= clps711x_init_early,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.handle_irq	= clps711x_handle_irq,
	.restart	= clps711x_restart,
MACHINE_END
