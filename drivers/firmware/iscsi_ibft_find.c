/*
 *  Copyright 2007-2010 Red Hat, Inc.
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
#include <linux/efi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/iscsi_ibft.h>

#include <asm/mmzone.h>

/*
 * Physical location of iSCSI Boot Format Table.
 */
struct acpi_table_ibft *ibft_addr;
EXPORT_SYMBOL_GPL(ibft_addr);

#define IBFT_SIGN "iBFT"
#define IBFT_SIGN_LEN 4
#define IBFT_START 0x80000 /* 512kB */
#define IBFT_END 0x100000 /* 1MB */
#define VGA_MEM 0xA0000 /* VGA buffer */
#define VGA_SIZE 0x20000 /* 128kB */

#ifdef CONFIG_ACPI
static int __init acpi_find_ibft(struct acpi_table_header *header)
{
	ibft_addr = (struct acpi_table_ibft *)header;
	return 0;
}
#endif /* CONFIG_ACPI */

static int __init find_ibft_in_mem(void)
{
	unsigned long pos;
	unsigned int len = 0;
	void *virt;

	for (pos = IBFT_START; pos < IBFT_END; pos += 16) {
		/* The table can't be inside the VGA BIOS reserved space,
		 * so skip that area */
		if (pos == VGA_MEM)
			pos += VGA_SIZE;
		virt = isa_bus_to_virt(pos);
		if (memcmp(virt, IBFT_SIGN, IBFT_SIGN_LEN) == 0) {
			unsigned long *addr =
			    (unsigned long *)isa_bus_to_virt(pos + 4);
			len = *addr;
			/* if the length of the table extends past 1M,
			 * the table cannot be valid. */
			if (pos + len <= (IBFT_END-1)) {
				ibft_addr = (struct acpi_table_ibft *)virt;
				break;
			}
		}
	}
	return len;
}
/*
 * Routine used to find the iSCSI Boot Format Table. The logical
 * kernel address is set in the ibft_addr global variable.
 */
unsigned long __init find_ibft_region(unsigned long *sizep)
{

	ibft_addr = NULL;

#ifdef CONFIG_ACPI
	/*
	 * One spec says "IBFT", the other says "iBFT". We have to check
	 * for both.
	 */
	if (!ibft_addr)
		acpi_table_parse(ACPI_SIG_IBFT, acpi_find_ibft);
	if (!ibft_addr)
		acpi_table_parse(IBFT_SIGN, acpi_find_ibft);
#endif /* CONFIG_ACPI */

	/* iBFT 1.03 section 1.4.3.1 mandates that UEFI machines will
	 * only use ACPI for this */

	if (!ibft_addr && !efi_enabled)
		find_ibft_in_mem();

	if (ibft_addr) {
		*sizep = PAGE_ALIGN(ibft_addr->header.length);
		return (u64)isa_virt_to_bus(ibft_addr);
	}

	*sizep = 0;
	return 0;
}
