// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/mmap.c
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/cpufeature.h>
#include <asm/page.h>

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This might go
 * away in the future.
 */
int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	/*
	 * Check whether addr is covered by a memory region without the
	 * MEMBLOCK_NOMAP attribute, and whether that region covers the
	 * entire range. In theory, this could lead to false negatives
	 * if the range is covered by distinct but adjacent memory regions
	 * that only differ in other attributes. However, few of such
	 * attributes have been defined, and it is debatable whether it
	 * follows that /dev/mem read() calls should be able traverse
	 * such boundaries.
	 */
	return memblock_is_region_memory(addr, size) &&
	       memblock_is_map_memory(addr);
}

/*
 * Do not allow /dev/mem mappings beyond the supported physical range.
 */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return !(((pfn << PAGE_SHIFT) + size) & ~PHYS_MASK);
}

static int __init adjust_protection_map(void)
{
	/*
	 * With Enhanced PAN we can honour the execute-only permissions as
	 * there is no PAN override with such mappings.
	 */
	if (cpus_have_const_cap(ARM64_HAS_EPAN)) {
		protection_map[VM_EXEC] = PAGE_EXECONLY;
		protection_map[VM_EXEC | VM_SHARED] = PAGE_EXECONLY;
	}

	return 0;
}
arch_initcall(adjust_protection_map);
