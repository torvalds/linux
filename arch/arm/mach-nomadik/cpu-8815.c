/*
 * Copyright STMicroelectronics, 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/mach/map.h>
#include <asm/hardware/vic.h>

/* All SoC devices live in the same area (see hardware.h) */
static struct map_desc nomadik_io_desc[] __initdata = {
	{
		.virtual =	NOMADIK_IO_VIRTUAL,
		.pfn =		__phys_to_pfn(NOMADIK_IO_PHYSICAL),
		.length =	NOMADIK_IO_SIZE,
		.type = 	MT_DEVICE,
	}
	/* static ram and secured ram may be added later */
};

void __init cpu8815_map_io(void)
{
	iotable_init(nomadik_io_desc, ARRAY_SIZE(nomadik_io_desc));
}

void __init cpu8815_init_irq(void)
{
	/* This modified VIC cell has two register blocks, at 0 and 0x20 */
	vic_init(io_p2v(NOMADIK_IC_BASE + 0x00), IRQ_VIC_START +  0, ~0, 0);
	vic_init(io_p2v(NOMADIK_IC_BASE + 0x20), IRQ_VIC_START + 32, ~0, 0);
}

/*
 * This function is called from the board init ("init_machine").
 * Currently nothing is done as we can't register amba devs so early.
 */
 void __init cpu8815_platform_init(void)
{
	 return;
}
