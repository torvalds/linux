// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2007 Eugene Konev <ejka@openwrt.org>
 */
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/swap.h>

#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/sections.h>

#include <asm/mach-ar7/ar7.h>

static int __init memsize(void)
{
	u32 size = (64 << 20);
	u32 *addr = (u32 *)KSEG1ADDR(AR7_SDRAM_BASE + size - 4);
	u32 *kernel_end = (u32 *)KSEG1ADDR(CPHYSADDR((u32)&_end));
	u32 *tmpaddr = addr;

	while (tmpaddr > kernel_end) {
		*tmpaddr = (u32)tmpaddr;
		size >>= 1;
		tmpaddr -= size >> 2;
	}

	do {
		tmpaddr += size >> 2;
		if (*tmpaddr != (u32)tmpaddr)
			break;
		size <<= 1;
	} while (size < (64 << 20));

	writel((u32)tmpaddr, &addr);

	return size;
}

void __init prom_meminit(void)
{
	unsigned long pages;

	pages = memsize() >> PAGE_SHIFT;
	add_memory_region(PHYS_OFFSET, pages << PAGE_SHIFT, BOOT_MEM_RAM);
}

void __init prom_free_prom_memory(void)
{
	/* Nothing to free */
}
