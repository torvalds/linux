/*
 *  Copyright 2007 Red Hat, Inc.
 *  by Peter Jones <pjones@redhat.com>
 *  Copyright 2007 IBM, Inc.
 *  by Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *  Copyright 2008
 *  by Konrad Rzeszutek <ketuzsezr@darnok.org>
 *
 * This code finds the iSCSI Boot Format Table.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bootmem.h>
#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/mmzone.h>

/*
 * Physical location of iSCSI Boot Format Table.
 */
struct ibft_table_header *ibft_addr;
EXPORT_SYMBOL_GPL(ibft_addr);

#define IBFT_SIGN "iBFT"
#define IBFT_SIGN_LEN 4
#define IBFT_START 0x80000 /* 512kB */
#define IBFT_END 0x100000 /* 1MB */
#define VGA_MEM 0xA0000 /* VGA buffer */
#define VGA_SIZE 0x20000 /* 128kB */


/*
 * Routine used to find the iSCSI Boot Format Table. The logical
 * kernel address is set in the ibft_addr global variable.
 */
void __init reserve_ibft_region(void)
{
	unsigned long pos;
	unsigned int len = 0;
	void *virt;

	ibft_addr = NULL;

	for (pos = IBFT_START; pos < IBFT_END; pos += 16) {
		/* The table can't be inside the VGA BIOS reserved space,
		 * so skip that area */
		if (pos == VGA_MEM)
			pos += VGA_SIZE;
		virt = phys_to_virt(pos);
		if (memcmp(virt, IBFT_SIGN, IBFT_SIGN_LEN) == 0) {
			unsigned long *addr =
			    (unsigned long *)phys_to_virt(pos + 4);
			len = *addr;
			/* if the length of the table extends past 1M,
			 * the table cannot be valid. */
			if (pos + len <= (IBFT_END-1)) {
				ibft_addr = (struct ibft_table_header *)virt;
				break;
			}
		}
	}
	if (ibft_addr)
		reserve_bootmem(pos, PAGE_ALIGN(len), BOOTMEM_DEFAULT);
}
