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
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/fixmap.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/addrspace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>

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
ioremap_fixed(resource_size_t phys_addr, unsigned long size, pgprot_t prot)
{
	enum fixed_addresses idx0, idx;
	resource_size_t last_addr;
	struct ioremap_map *map;
	unsigned long offset;
	unsigned int nrpages;
	int i, slot;

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

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Fixmap mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

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

void __init iounmap_fixed(void __iomem *addr)
{
	enum fixed_addresses idx;
	unsigned long virt_addr;
	struct ioremap_map *map;
	unsigned long offset;
	unsigned int nrpages;
	int i, slot;
	pgprot_t prot;

	slot = -1;
	for (i = 0; i < FIX_N_IOREMAPS; i++) {
		map = &ioremap_maps[i];
		if (map->addr == addr) {
			slot = i;
			break;
		}
	}

	if (slot < 0)
		return;

	virt_addr = (unsigned long)addr;

	offset = virt_addr & ~PAGE_MASK;
	nrpages = PAGE_ALIGN(offset + map->size - 1) >> PAGE_SHIFT;

	pgprot_val(prot) = _PAGE_WIRED;

	idx = FIX_IOREMAP_BEGIN + slot + nrpages;
	while (nrpages > 0) {
		__clear_fixmap(idx, prot);
		--idx;
		--nrpages;
	}

	map->size = 0;
	map->addr = NULL;
}
