/*
 *  This file contains ioremap and related functions for 64-bit machines.
 *
 *  Derived from arch/ppc64/mm/init.c
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@samba.org)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
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
#include <linux/bootmem.h>
#include <linux/lmb.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/abs_addr.h>
#include <asm/firmware.h>

#include "mmu_decl.h"

unsigned long ioremap_bot = IOREMAP_BASE;


#ifdef CONFIG_PPC_MMU_NOHASH
static void *early_alloc_pgtable(unsigned long size)
{
	void *pt;

	if (init_bootmem_done)
		pt = __alloc_bootmem(size, size, __pa(MAX_DMA_ADDRESS));
	else
		pt = __va(lmb_alloc_base(size, size,
					 __pa(MAX_DMA_ADDRESS)));
	memset(pt, 0, size);

	return pt;
}
#endif /* CONFIG_PPC_MMU_NOHASH */

/*
 * map_kernel_page currently only called by __ioremap
 * map_kernel_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
int map_kernel_page(unsigned long ea, unsigned long pa, int flags)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (slab_is_available()) {
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
#ifdef CONFIG_PPC_MMU_NOHASH
		/* Warning ! This will blow up if bootmem is not initialized
		 * which our ppc64 code is keen to do that, we'll need to
		 * fix it and/or be more careful
		 */
		pgdp = pgd_offset_k(ea);
#ifdef PUD_TABLE_SIZE
		if (pgd_none(*pgdp)) {
			pudp = early_alloc_pgtable(PUD_TABLE_SIZE);
			BUG_ON(pudp == NULL);
			pgd_populate(&init_mm, pgdp, pudp);
		}
#endif /* PUD_TABLE_SIZE */
		pudp = pud_offset(pgdp, ea);
		if (pud_none(*pudp)) {
			pmdp = early_alloc_pgtable(PMD_TABLE_SIZE);
			BUG_ON(pmdp == NULL);
			pud_populate(&init_mm, pudp, pmdp);
		}
		pmdp = pmd_offset(pudp, ea);
		if (!pmd_present(*pmdp)) {
			ptep = early_alloc_pgtable(PAGE_SIZE);
			BUG_ON(ptep == NULL);
			pmd_populate_kernel(&init_mm, pmdp, ptep);
		}
		ptep = pte_offset_kernel(pmdp, ea);
		set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT,
							  __pgprot(flags)));
#else /* CONFIG_PPC_MMU_NOHASH */
		/*
		 * If the mm subsystem is not fully up, we cannot create a
		 * linux page table entry for this mapping.  Simply bolt an
		 * entry in the hardware page table.
		 *
		 */
		if (htab_bolt_mapping(ea, ea + PAGE_SIZE, pa, flags,
				      mmu_io_psize, mmu_kernel_ssize)) {
			printk(KERN_ERR "Failed to do bolted mapping IO "
			       "memory at %016lx !\n", pa);
			return -ENOMEM;
		}
#endif /* !CONFIG_PPC_MMU_NOHASH */
	}
	return 0;
}


/**
 * __ioremap_at - Low level function to establish the page tables
 *                for an IO mapping
 */
void __iomem * __ioremap_at(phys_addr_t pa, void *ea, unsigned long size,
			    unsigned long flags)
{
	unsigned long i;

	/* Make sure we have the base flags */
	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);

	/* Non-cacheable page cannot be coherent */
	if (flags & _PAGE_NO_CACHE)
		flags &= ~_PAGE_COHERENT;

	/* We don't support the 4K PFN hack with ioremap */
	if (flags & _PAGE_4K_PFN)
		return NULL;

	WARN_ON(pa & ~PAGE_MASK);
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	for (i = 0; i < size; i += PAGE_SIZE)
		if (map_kernel_page((unsigned long)ea+i, pa+i, flags))
			return NULL;

	return (void __iomem *)ea;
}

/**
 * __iounmap_from - Low level function to tear down the page tables
 *                  for an IO mapping. This is used for mappings that
 *                  are manipulated manually, like partial unmapping of
 *                  PCI IOs or ISA space.
 */
void __iounmap_at(void *ea, unsigned long size)
{
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	unmap_kernel_range((unsigned long)ea, size);
}

void __iomem * __ioremap_caller(phys_addr_t addr, unsigned long size,
				unsigned long flags, void *caller)
{
	phys_addr_t paligned;
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
	paligned = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - paligned;

	if ((size == 0) || (paligned == 0))
		return NULL;

	if (mem_init_done) {
		struct vm_struct *area;

		area = __get_vm_area_caller(size, VM_IOREMAP,
					    ioremap_bot, IOREMAP_END,
					    caller);
		if (area == NULL)
			return NULL;
		ret = __ioremap_at(paligned, area->addr, size, flags);
		if (!ret)
			vunmap(area->addr);
	} else {
		ret = __ioremap_at(paligned, (void *)ioremap_bot, size, flags);
		if (ret)
			ioremap_bot += size;
	}

	if (ret)
		ret += addr & ~PAGE_MASK;
	return ret;
}

void __iomem * __ioremap(phys_addr_t addr, unsigned long size,
			 unsigned long flags)
{
	return __ioremap_caller(addr, size, flags, __builtin_return_address(0));
}

void __iomem * ioremap(phys_addr_t addr, unsigned long size)
{
	unsigned long flags = _PAGE_NO_CACHE | _PAGE_GUARDED;
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}

void __iomem * ioremap_flags(phys_addr_t addr, unsigned long size,
			     unsigned long flags)
{
	void *caller = __builtin_return_address(0);

	/* writeable implies dirty for kernel addresses */
	if (flags & _PAGE_RW)
		flags |= _PAGE_DIRTY;

	/* we don't want to let _PAGE_USER and _PAGE_EXEC leak out */
	flags &= ~(_PAGE_USER | _PAGE_EXEC);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}


/*  
 * Unmap an IO region and remove it from imalloc'd list.
 * Access to IO memory should be serialized by driver.
 */
void __iounmap(volatile void __iomem *token)
{
	void *addr;

	if (!mem_init_done)
		return;
	
	addr = (void *) ((unsigned long __force)
			 PCI_FIX_ADDR(token) & PAGE_MASK);
	if ((unsigned long)addr < ioremap_bot) {
		printk(KERN_WARNING "Attempt to iounmap early bolted mapping"
		       " at 0x%p\n", addr);
		return;
	}
	vunmap(addr);
}

void iounmap(volatile void __iomem *token)
{
	if (ppc_md.iounmap)
		ppc_md.iounmap(token);
	else
		__iounmap(token);
}

EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(ioremap_flags);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(__ioremap_at);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(__iounmap);
EXPORT_SYMBOL(__iounmap_at);
