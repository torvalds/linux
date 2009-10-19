/*
 * Pb1550 board platform device registration
 *
 * Copyright (C) 2009 Manuel Lauss
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-pb1x00/pb1550.h>
#include <asm/mach-db1x00/bcsr.h>

#include "../platform.h"

static int __init pb1550_dev_init(void)
{
	int swapped;

	/* Pb1550, like all others, also has statuschange irqs; however they're
	* wired up on one of the Au1550's shared GPIO201_205 line, which also
	* services the PCMCIA card interrupts.  So we ignore statuschange and
	* use the GPIO201_205 exclusively for card interrupts, since a) pcmcia
	* drivers are used to shared irqs and b) statuschange isn't really use-
	* ful anyway.
	*/
	db1x_register_pcmcia_socket(PCMCIA_ATTR_PSEUDO_PHYS,
				    PCMCIA_ATTR_PSEUDO_PHYS + 0x00040000 - 1,
				    PCMCIA_MEM_PSEUDO_PHYS,
				    PCMCIA_MEM_PSEUDO_PHYS  + 0x00040000 - 1,
				    PCMCIA_IO_PSEUDO_PHYS,
				    PCMCIA_IO_PSEUDO_PHYS   + 0x00001000 - 1,
				    AU1550_GPIO201_205_INT,
				    AU1550_GPIO0_INT,
				    0,
				    0,
				    0);

	db1x_register_pcmcia_socket(PCMCIA_ATTR_PSEUDO_PHYS + 0x00800000,
				    PCMCIA_ATTR_PSEUDO_PHYS + 0x00840000 - 1,
				    PCMCIA_MEM_PSEUDO_PHYS  + 0x00800000,
				    PCMCIA_MEM_PSEUDO_PHYS  + 0x00840000 - 1,
				    PCMCIA_IO_PSEUDO_PHYS   + 0x00800000,
				    PCMCIA_IO_PSEUDO_PHYS   + 0x00801000 - 1,
				    AU1550_GPIO201_205_INT,
				    AU1550_GPIO1_INT,
				    0,
				    0,
				    1);

	swapped = bcsr_read(BCSR_STATUS) & BCSR_STATUS_PB1550_SWAPBOOT;
	db1x_register_norflash(128 * 1024 * 1024, 4, swapped);

	return 0;
}
device_initcall(pb1550_dev_init);
