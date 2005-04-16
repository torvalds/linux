/*
 *  linux/arch/arm/mach-clps711x/autcpu12.c
 *
 * (c) 2001 Thomas Gleixner, autronix automation <gleixner@autronix.de>
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

#include <asm/hardware.h>
#include <asm/sizes.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <asm/arch/autcpu12.h>

#include "common.h"

/*
 * The on-chip registers are given a size of 1MB so that a section can
 * be used to map them; this saves a page table.  This is the place to
 * add mappings for ROM, expansion memory, PCMCIA, etc.  (if static
 * mappings are chosen for those areas).
 *
*/

static struct map_desc autcpu12_io_desc[] __initdata = {
 /* virtual, physical, length, type */
 /* memory-mapped extra io and CS8900A Ethernet chip */
 /* ethernet chip */
 	{ AUTCPU12_VIRT_CS8900A, AUTCPU12_PHYS_CS8900A, SZ_1M, MT_DEVICE }
};

void __init autcpu12_map_io(void)
{
        clps711x_map_io();
        iotable_init(autcpu12_io_desc, ARRAY_SIZE(autcpu12_io_desc));
}

MACHINE_START(AUTCPU12, "autronix autcpu12")
	MAINTAINER("Thomas Gleixner")
        BOOT_MEM(0xc0000000, 0x80000000, 0xff000000)
	BOOT_PARAMS(0xc0020000)
	MAPIO(autcpu12_map_io)
	INITIRQ(clps711x_init_irq)
	.timer		= &clps711x_timer,
MACHINE_END

