/*
 *  arch/blackfin/mach-bf537/board/eth_mac.c
 *
 *  Copyright (C) 2007 Analog Devices, Inc.
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
#include <linux/module.h>
#include <asm/blackfin.h>

#if	defined(CONFIG_GENERIC_BOARD) \
	|| defined(CONFIG_BFIN537_STAMP)

/*
 * Currently the MAC address is saved in Flash by U-Boot
 */
#define FLASH_MAC	0x203f0000

void get_bf537_ether_addr(char *addr)
{
	unsigned int flash_mac = (unsigned int) FLASH_MAC;
	*(u32 *)(&(addr[0])) = bfin_read32(flash_mac);
	flash_mac += 4;
	*(u16 *)(&(addr[4])) = bfin_read16(flash_mac);
}

#else

/*
 * Provide MAC address function for other specific board setting
 */
void get_bf537_ether_addr(char *addr)
{
	printk(KERN_WARNING "%s: No valid Ethernet MAC address found\n",__FILE__);
}

#endif

EXPORT_SYMBOL(get_bf537_ether_addr);
