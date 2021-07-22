// SPDX-License-Identifier: GPL-2.0
/*
 * Re-map IO memory to kernel address space so that we can access it.
 *
 * These functions should only be used when it is necessary to map a
 * physical address space into the kernel address space before ioremap()
 * can be used, e.g. early in boot before paging_init().
 *
 * Copyright (C) 2009  Matt Fleming
 */

#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/proc_fs.h>
#include <asm/fixmap.h>
#include <asm/page.h>
#include <asm/addrspace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include "ioremap.h"

struct ioremap_map {
	void __iomem *addr;
	unsigned long size;
	unsigned long fixmap_addr;
};

static struct ioremap_map ioremap_maps[FIX_N_IOREMAPS];

void __init ioremap_fixed_init(void)
{
	struct ioremap_map *map;
	int i;

	for (i = 0; i < FIX_N_IOREMAPS; i++) {
		map = &ioremap_maps[i];
		map->fixmap_addr = __fix_to_virt(FIX_IOREMAP_BEGIN + i);
	}
}

void __init __iomem *
ioremap_fixed(phys_addr_t phys_addr, unsigned long size, pgprot_t prot)
{
	enum fixed_addresses idx0, idx;
	struct ioremap_map *map;
	unsigned int nrpages;
	unsigned long offset;
	int i, slot;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(phys_addr + size) - phys_addr;

	slot = -1;
	for (i = 0; i < FIX_N_IOREMAPS; i++) {
		map = &ioremap_maps[i];
		if (!map->addr) {
			map->size = size;
			slot = i;
			break;
		}
	}

	if (slot < 0)
		return NULL;

	/*
	 * Mappings have to fit in the FIX_IOREMAP area.
	 */
	nrpages = size >> PAGE_SHIFT;
	if (nrpages > FIX_N_IOREMAPS)
		return NULL;

	/*
	 * Ok, go for it..
	 */
	idx0 = FIX_IOREMAP_BEGIN + slot;
	idx = idx0;
	while (nrpages > 0) {
		pgprot_val(prot) |= _PAGE_WIRED;
		__set_fixmap(idx, phys_addr, prot);
		phys_addr += PAGE_SIZE;
		idx++;
		--nrpages;
	}

	map->addr = (void __iomem *)(offset + map->fixmap_addr);
	return map->addr;
}

int iounmap_fixed(void __iomem *addr)
{
	enum fixed_addresses idx;
	struct ioremap_map *map;
	unsigned int nrpages;
	int i, slot;

	slot = -1;
	for (i = 0; i < FIX_N_IOREMAPS; i++) {
		map = &ioremap_maps[i];
		if (map->addr == addr) {
			slot = i;
			break;
		}
	}

	/*
	 * If we don't match, it's not for us.
	 */
	if (slot < 0)
		return -EINVAL;

	nrpages = map->size >> PAGE_SHIFT;

	idx = FIX_IOREMAP_BEGIN + slot + nrpages - 1;
	while (nrpages > 0) {
		__clear_fixmap(idx, __pgprot(_PAGE_WIRED));
		--idx;
		--nrpages;
	}

	map->size = 0;
	map->addr = NULL;

	return 0;
}
