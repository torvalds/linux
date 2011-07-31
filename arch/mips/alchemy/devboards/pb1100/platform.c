/*
 * Pb1100 board platform device registration
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
#include <asm/mach-db1x00/bcsr.h>

#include "../platform.h"

static int __init pb1100_dev_init(void)
{
	int swapped;

	/* PCMCIA. single socket, identical to Pb1500 */
	db1x_register_pcmcia_socket(PCMCIA_ATTR_PHYS_ADDR,
				    PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
				    PCMCIA_MEM_PHYS_ADDR,
				    PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
				    PCMCIA_IO_PHYS_ADDR,
				    PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
				    AU1100_GPIO11_INT,	 /* card */
				    AU1100_GPIO9_INT,	 /* insert */
				    /*AU1100_GPIO10_INT*/0, /* stschg */
				    0,			 /* eject */
				    0);			 /* id */

	swapped = bcsr_read(BCSR_STATUS) &  BCSR_STATUS_DB1000_SWAPBOOT;
	db1x_register_norflash(64 * 1024 * 1024, 4, swapped);

	return 0;
}
device_initcall(pb1100_dev_init);
