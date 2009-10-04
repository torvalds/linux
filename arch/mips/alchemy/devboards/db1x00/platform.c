/*
 * DBAu1xxx board platform device registration
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
#include <linux/platform_device.h>

#include <asm/mach-au1x00/au1xxx.h>
#include "../platform.h"

#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100) || \
    defined(CONFIG_MIPS_DB1500) || defined(CONFIG_MIPS_DB1550)
#define DB1XXX_HAS_PCMCIA
#endif

/* DB1xxx PCMCIA interrupt sources:
 * CD0/1 	GPIO0/3
 * STSCHG0/1	GPIO1/4
 * CARD0/1	GPIO2/5
 * Db1550:	0/1, 21/22, 3/5
 */
#ifndef CONFIG_MIPS_DB1550
/* Db1000, Db1100, Db1500 */
#define DB1XXX_PCMCIA_CD0	AU1000_GPIO_0
#define DB1XXX_PCMCIA_STSCHG0	AU1000_GPIO_1
#define DB1XXX_PCMCIA_CARD0	AU1000_GPIO_2
#define DB1XXX_PCMCIA_CD1	AU1000_GPIO_3
#define DB1XXX_PCMCIA_STSCHG1	AU1000_GPIO_4
#define DB1XXX_PCMCIA_CARD1	AU1000_GPIO_5
#else
#define DB1XXX_PCMCIA_CD0	AU1000_GPIO_0
#define DB1XXX_PCMCIA_STSCHG0	AU1500_GPIO_21
#define DB1XXX_PCMCIA_CARD0	AU1000_GPIO_3
#define DB1XXX_PCMCIA_CD1	AU1000_GPIO_1
#define DB1XXX_PCMCIA_STSCHG1	AU1500_GPIO_22
#define DB1XXX_PCMCIA_CARD1	AU1000_GPIO_5
#endif

static int __init db1xxx_dev_init(void)
{
#ifdef DB1XXX_HAS_PCMCIA
	db1x_register_pcmcia_socket(PCMCIA_ATTR_PSEUDO_PHYS,
				    PCMCIA_ATTR_PSEUDO_PHYS + 0x00040000 - 1,
				    PCMCIA_MEM_PSEUDO_PHYS,
				    PCMCIA_MEM_PSEUDO_PHYS  + 0x00040000 - 1,
				    PCMCIA_IO_PSEUDO_PHYS,
				    PCMCIA_IO_PSEUDO_PHYS   + 0x00001000 - 1,
				    DB1XXX_PCMCIA_CARD0,
				    DB1XXX_PCMCIA_CD0,
				    /*DB1XXX_PCMCIA_STSCHG0*/0,
				    0,
				    0);

	db1x_register_pcmcia_socket(PCMCIA_ATTR_PSEUDO_PHYS + 0x00400000,
				    PCMCIA_ATTR_PSEUDO_PHYS + 0x00440000 - 1,
				    PCMCIA_MEM_PSEUDO_PHYS  + 0x00400000,
				    PCMCIA_MEM_PSEUDO_PHYS  + 0x00440000 - 1,
				    PCMCIA_IO_PSEUDO_PHYS   + 0x00400000,
				    PCMCIA_IO_PSEUDO_PHYS   + 0x00401000 - 1,
				    DB1XXX_PCMCIA_CARD1,
				    DB1XXX_PCMCIA_CD1,
				    /*DB1XXX_PCMCIA_STSCHG1*/0,
				    0,
				    1);
#endif
	return 0;
}
device_initcall(db1xxx_dev_init);
