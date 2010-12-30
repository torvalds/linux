/*
 *  linux/arch/arm/mach-clps711x/p720t.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd
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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/syspld.h>

#include "common.h"

/*
 * Map the P720T system PLD.  It occupies two address spaces:
 *  SYSPLD_PHYS_BASE and SYSPLD_PHYS_BASE + 0x00400000
 * We map both here.
 */
static struct map_desc p720t_io_desc[] __initdata = {
	{
		.virtual	= SYSPLD_VIRT_BASE,
		.pfn		= __phys_to_pfn(SYSPLD_PHYS_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	}, {
		.virtual	= 0xfe400000,
		.pfn		= __phys_to_pfn(0x10400000),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	}
};

static void __init
fixup_p720t(struct machine_desc *desc, struct tag *tag,
	    char **cmdline, struct meminfo *mi)
{
	/*
	 * Our bootloader doesn't setup any tags (yet).
	 */
	if (tag->hdr.tag != ATAG_CORE) {
		tag->hdr.tag = ATAG_CORE;
		tag->hdr.size = tag_size(tag_core);
		tag->u.core.flags = 0;
		tag->u.core.pagesize = PAGE_SIZE;
		tag->u.core.rootdev = 0x0100;

		tag = tag_next(tag);
		tag->hdr.tag = ATAG_MEM;
		tag->hdr.size = tag_size(tag_mem32);
		tag->u.mem.size = 4096;
		tag->u.mem.start = PHYS_OFFSET;

		tag = tag_next(tag);
		tag->hdr.tag = ATAG_NONE;
		tag->hdr.size = 0;
	}
}

static void __init p720t_map_io(void)
{
	clps711x_map_io();
	iotable_init(p720t_io_desc, ARRAY_SIZE(p720t_io_desc));
}

MACHINE_START(P720T, "ARM-Prospector720T")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.boot_params	= 0xc0000100,
	.fixup		= fixup_p720t,
	.map_io		= p720t_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
MACHINE_END

static int p720t_hw_init(void)
{
	/*
	 * Power down as much as possible in case we don't
	 * have the drivers loaded.
	 */
	PLD_LCDEN = 0;
	PLD_PWR  &= ~(PLD_S4_ON|PLD_S3_ON|PLD_S2_ON|PLD_S1_ON);

	PLD_KBD   = 0;
	PLD_IO    = 0;
	PLD_IRDA  = 0;
	PLD_CODEC = 0;
	PLD_TCH   = 0;
	PLD_SPI   = 0;
#ifndef CONFIG_DEBUG_LL
	PLD_COM2  = 0;
	PLD_COM1  = 0;
#endif

	return 0;
}

__initcall(p720t_hw_init);

