/*
 * arch/sh/mm/mmap.c
 *
 * Copyright (C) 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/io.h>
#include <linux/mm.h>
#include <asm/page.h>

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This
 * might go away in the future.
 */
int valid_phys_addr_range(unsigned long addr, size_t count)
{
	if (addr < __MEMORY_START)
		return 0;
	if (addr + count > __pa(high_memory))
		return 0;

	return 1;
}

int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return 1;
}
