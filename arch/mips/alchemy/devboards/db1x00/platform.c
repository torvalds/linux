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
#include <asm/mach-db1x00/bcsr.h>
#include "../platform.h"

/* DB1xxx PCMCIA interrupt sources:
 * CD0/1 	GPIO0/3
 * STSCHG0/1	GPIO1/4
 * CARD0/1	GPIO2/5
 * Db1550:	0/1, 21/22, 3/5
 */

#define DB1XXX_HAS_PCMCIA
#define F_SWAPPED (bcsr_read(BCSR_STATUS) & BCSR_STATUS_DB1000_SWAPBOOT)

#if defined(CONFIG_MIPS_DB1000)
#define DB1XXX_PCMCIA_CD0	AU1000_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1000_GPIO1_INT
#define DB1XXX_PCMCIA_CARD0	AU1000_GPIO2_INT
#define DB1XXX_PCMCIA_CD1	AU1000_GPIO3_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1000_GPIO4_INT
#define DB1XXX_PCMCIA_CARD1	AU1000_GPIO5_INT
#define BOARD_FLASH_SIZE	0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#elif defined(CONFIG_MIPS_DB1100)
#define DB1XXX_PCMCIA_CD0	AU1100_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1100_GPIO1_INT
#define DB1XXX_PCMCIA_CARD0	AU1100_GPIO2_INT
#define DB1XXX_PCMCIA_CD1	AU1100_GPIO3_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1100_GPIO4_INT
#define DB1XXX_PCMCIA_CARD1	AU1100_GPIO5_INT
#define BOARD_FLASH_SIZE	0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#elif defined(CONFIG_MIPS_DB1500)
#define DB1XXX_PCMCIA_CD0	AU1500_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1500_GPIO1_INT
#define DB1XXX_PCMCIA_CARD0	AU1500_GPIO2_INT
#define DB1XXX_PCMCIA_CD1	AU1500_GPIO3_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1500_GPIO4_INT
#define DB1XXX_PCMCIA_CARD1	AU1500_GPIO5_INT
#define BOARD_FLASH_SIZE	0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#elif defined(CONFIG_MIPS_DB1550)
#define DB1XXX_PCMCIA_CD0	AU1550_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1550_GPIO21_INT
#define DB1XXX_PCMCIA_CARD0	AU1550_GPIO3_INT
#define DB1XXX_PCMCIA_CD1	AU1550_GPIO1_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1550_GPIO22_INT
#define DB1XXX_PCMCIA_CARD1	AU1550_GPIO5_INT
#define BOARD_FLASH_SIZE	0x08000000 /* 128MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#else
/* other board: no PCMCIA */
#undef DB1XXX_HAS_PCMCIA
#undef F_SWAPPED
#define F_SWAPPED 0
#if defined(CONFIG_MIPS_BOSPORUS)
#define BOARD_FLASH_SIZE	0x01000000 /* 16MB */
#define BOARD_FLASH_WIDTH	2 /* 16-bits */
#elif defined(CONFIG_MIPS_MIRAGE)
#define BOARD_FLASH_SIZE	0x04000000 /* 64MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#endif
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
	db1x_register_norflash(BOARD_FLASH_SIZE, BOARD_FLASH_WIDTH, F_SWAPPED);
	return 0;
}
device_initcall(db1xxx_dev_init);
