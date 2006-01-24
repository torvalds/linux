/*
 *  This file contains ioremap and related functions for 64-bit machines.
 *
 *  Derived from arch/ppc64/mm/init.c
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@samba.org)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/nodemask.h>
#include <linux/module.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#include <asm/rtas.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/eeh.h>
#include <asm/processor.h>
#include <asm/mmzone.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/iommu.h>
#include <asm/abs_addr.h>
#include <asm/vdso.h>

#include "mmu_decl.h"

unsigned long ioremap_bot = IMALLOC_BASE;
static unsigned long phbs_io_bot = PHBS_IO_BASE;

#ifdef CONFIG_PPC_ISERIES

void __iomem *ioremap(unsigned long addr, unsigned long size)
{
	return (void __iomem *)addr;
}

extern void __iomem *__ioremap(unsigned long addr, unsigned long size,
		       unsigned long flags)
{
	return (void __iomem *)addr;
}

void iounmap(volatile void __iomem *addr)
{
	return;
}

#else

/*
 * map_io_page currently only called by __ioremap
 * map_io_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
static int map_io_page(unsigned long ea, unsigned long pa, int flags)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (mem_init_done) {
		pgdp = pgd_offset_k(ea);
		pudp = pud_alloc(&init_mm, pgdp, ea);
		if (!pudp)
			return -ENOMEM;
		pmdp = pmd_alloc(&init_mm, pudp, ea);
		if (!pmdp)
			return -ENOMEM;
		ptep = pte_alloc_kernel(pmdp, ea);
		if (!ptep)
			return -ENOMEM;
		set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT,
							  __pgprot(flags)));
	} else {
		/*
		 * If the mm subsystem is not fully up, we cannot create a
		 * linux page table entry for this mapping.  Simply bolt an
		 * entry in the hardware page table.
		 *
		 */
		if (htab_bolt_mapping(ea, ea + PAGE_SIZE, pa, flags,
				      mmu_virtual_psize)) {
			printk(KERN_ERR "Failed to do bolted mapping IO "
			       "memory at %016lx !\n", pa);
			return -ENOMEM;
		}
	}
	return 0;
}


static void __iomem * __ioremap_com(unsigned long addr, unsigned long pa,
			    unsigned long ea, unsigned long size,
			    unsigned long flags)
{
	unsigned long i;

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);

	for (i = 0; i < size; i += PAGE_SIZE)
		if (map_io_page(ea+i, pa+i, flags))
			return NULL;

	return (void __iomem *) (ea + (addr & ~PAGE_MASK));
}


void __iomem *
ioremap(unsigned long addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE | _PAGE_GUARDED);
}

void __iomem * __ioremap(unsigned long addr, unsigned long size,
			 unsigned long flags)
{
	unsigned long pa, ea;
	void __iomem *ret;

	/*
	 * Choose an address to map it to.
	 * Once the imalloc system is running, we use it.
	 * Before that, we map using addresses going
	 * up from ioremap_bot.  imalloc will use
	 * the addresses from ioremap_bot through
	 * IMALLOC_END
	 * 
	 */
	pa = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - pa;

	if ((size == 0) || (pa == 0))
		return NULL;

	if (mem_init_done) {
		struct vm_struct *area;
		area = im_get_free_area(size);
		if (area == NULL)
			return NULL;
		ea = (unsigned long)(area->addr);
		ret = __ioremap_com(addr, pa, ea, size, flags);
		if (!ret)
			im_free(area->addr);
	} else {
		ea = ioremap_bot;
		ret = __ioremap_com(addr, pa, ea, size, flags);
		if (ret)
			ioremap_bot += size;
	}
	return ret;
}

#define IS_PAGE_ALIGNED(_val) ((_val) == ((_val) & PAGE_MASK))

int __ioremap_explicit(unsigned long pa, unsigned long ea,
		       unsigned long size, unsigned long flags)
{
	struct vm_struct *area;
	void __iomem *ret;
	
	/* For now, require page-aligned values for pa, ea, and size */
	if (!IS_PAGE_ALIGNED(pa) || !IS_PAGE_ALIGNED(ea) ||
	    !IS_PAGE_ALIGNED(size)) {
		printk(KERN_ERR	"unaligned value in %s\n", __FUNCTION__);
		return 1;
	}
	
	if (!mem_init_done) {
		/* Two things to consider in this case:
		 * 1) No records will be kept (imalloc, etc) that the region
		 *    has been remapped
		 * 2) It won't be easy to iounmap() the region later (because
		 *    of 1)
		 */
		;
	} else {
		area = im_get_area(ea, size,
			IM_REGION_UNUSED|IM_REGION_SUBSET|IM_REGION_EXISTS);
		if (area == NULL) {
			/* Expected when PHB-dlpar is in play */
			return 1;
		}
		if (ea != (unsigned long) area->addr) {
			printk(KERN_ERR "unexpected addr return from "
			       "im_get_area\n");
			return 1;
		}
	}
	
	ret = __ioremap_com(pa, pa, ea, size, flags);
	if (ret == NULL) {
		printk(KERN_ERR "ioremap_explicit() allocation failure !\n");
		return 1;
	}
	if (ret != (void *) ea) {
		printk(KERN_ERR "__ioremap_com() returned unexpected addr\n");
		return 1;
	}

	return 0;
}

/*  
 * Unmap an IO region and remove it from imalloc'd list.
 * Access to IO memory should be serialized by driver.
 * This code is modeled after vmalloc code - unmap_vm_area()
 *
 * XXX	what about calls before mem_init_done (ie python_countermeasures())
 */
void iounmap(volatile void __iomem *token)
{
	void *addr;

	if (!mem_init_done)
		return;
	
	addr = (void *) ((unsigned long __force) token & PAGE_MASK);

	im_free(addr);
}

static int iounmap_subset_regions(unsigned long addr, unsigned long size)
{
	struct vm_struct *area;

	/* Check whether subsets of this region exist */
	area = im_get_area(addr, size, IM_REGION_SUPERSET);
	if (area == NULL)
		return 1;

	while (area) {
		iounmap((void __iomem *) area->addr);
		area = im_get_area(addr, size,
				IM_REGION_SUPERSET);
	}

	return 0;
}

int iounmap_explicit(volatile void __iomem *start, unsigned long size)
{
	struct vm_struct *area;
	unsigned long addr;
	int rc;
	
	addr = (unsigned long __force) start & PAGE_MASK;

	/* Verify that the region either exists or is a subset of an existing
	 * region.  In the latter case, split the parent region to create 
	 * the exact region 
	 */
	area = im_get_area(addr, size, 
			    IM_REGION_EXISTS | IM_REGION_SUBSET);
	if (area == NULL) {
		/* Determine whether subset regions exist.  If so, unmap */
		rc = iounmap_subset_regions(addr, size);
		if (rc) {
			printk(KERN_ERR
			       "%s() cannot unmap nonexistent range 0x%lx\n",
 				__FUNCTION__, addr);
			return 1;
		}
	} else {
		iounmap((void __iomem *) area->addr);
	}
	/*
	 * FIXME! This can't be right:
	iounmap(area->addr);
	 * Maybe it should be "iounmap(area);"
	 */
	return 0;
}

#endif

EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);

void __iomem * reserve_phb_iospace(unsigned long size)
{
	void __iomem *virt_addr;
		
	if (phbs_io_bot >= IMALLOC_BASE) 
		panic("reserve_phb_iospace(): phb io space overflow\n");
			
	virt_addr = (void __iomem *) phbs_io_bot;
	phbs_io_bot += size;

	return virt_addr;
}
