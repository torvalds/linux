/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *  PPC44x/36-bit changes by Matt Porter (mporter@mvista.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/initrd.h>
#include <linux/pagemap.h>

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/btext.h>
#include <asm/tlb.h>
#include <asm/bootinfo.h>
#include <asm/prom.h>

#include "mem_pieces.h"
#include "mmu_decl.h"

#ifndef CPU_FTR_COHERENT_ICACHE
#define CPU_FTR_COHERENT_ICACHE	0	/* XXX for now */
#define CPU_FTR_NOEXECUTE	0
#endif

/*
 * This is called by /dev/mem to know if a given address has to
 * be mapped non-cacheable or not
 */
int page_is_ram(unsigned long pfn)
{
	unsigned long paddr = (pfn << PAGE_SHIFT);

#ifndef CONFIG_PPC64	/* XXX for now */
	return paddr < __pa(high_memory);
#else
	int i;
	for (i=0; i < lmb.memory.cnt; i++) {
		unsigned long base;

		base = lmb.memory.region[i].base;

		if ((paddr >= base) &&
			(paddr < (base + lmb.memory.region[i].size))) {
			return 1;
		}
	}

	return 0;
#endif
}
EXPORT_SYMBOL(page_is_ram);

pgprot_t phys_mem_access_prot(struct file *file, unsigned long addr,
			      unsigned long size, pgprot_t vma_prot)
{
	if (ppc_md.phys_mem_access_prot)
		return ppc_md.phys_mem_access_prot(file, addr, size, vma_prot);

	if (!page_is_ram(addr >> PAGE_SHIFT))
		vma_prot = __pgprot(pgprot_val(vma_prot)
				    | _PAGE_GUARDED | _PAGE_NO_CACHE);
	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);

void show_mem(void)
{
	unsigned long total = 0, reserved = 0;
	unsigned long shared = 0, cached = 0;
	unsigned long highmem = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageHighMem(page))
				highmem++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
	}
	printk("%ld pages of RAM\n", total);
#ifdef CONFIG_HIGHMEM
	printk("%ld pages of HIGHMEM\n", highmem);
#endif
	printk("%ld reserved pages\n", reserved);
	printk("%ld pages shared\n", shared);
	printk("%ld pages swap cached\n", cached);
}

/*
 * This is called when a page has been modified by the kernel.
 * It just marks the page as not i-cache clean.  We do the i-cache
 * flush later when the page is given to a user process, if necessary.
 */
void flush_dcache_page(struct page *page)
{
	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &page->flags))
		clear_bit(PG_arch_1, &page->flags);
}
EXPORT_SYMBOL(flush_dcache_page);

void flush_dcache_icache_page(struct page *page)
{
#ifdef CONFIG_BOOKE
	void *start = kmap_atomic(page, KM_PPC_SYNC_ICACHE);
	__flush_dcache_icache(start);
	kunmap_atomic(start, KM_PPC_SYNC_ICACHE);
#elif defined(CONFIG_8xx)
	/* On 8xx there is no need to kmap since highmem is not supported */
	__flush_dcache_icache(page_address(page)); 
#else
	__flush_dcache_icache_phys(page_to_pfn(page) << PAGE_SHIFT);
#endif

}
void clear_user_page(void *page, unsigned long vaddr, struct page *pg)
{
	clear_page(page);

	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/*
	 * We shouldnt have to do this, but some versions of glibc
	 * require it (ld.so assumes zero filled pages are icache clean)
	 * - Anton
	 */

	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &pg->flags))
		clear_bit(PG_arch_1, &pg->flags);
}
EXPORT_SYMBOL(clear_user_page);

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *pg)
{
	copy_page(vto, vfrom);

	/*
	 * We should be able to use the following optimisation, however
	 * there are two problems.
	 * Firstly a bug in some versions of binutils meant PLT sections
	 * were not marked executable.
	 * Secondly the first word in the GOT section is blrl, used
	 * to establish the GOT address. Until recently the GOT was
	 * not marked executable.
	 * - Anton
	 */
#if 0
	if (!vma->vm_file && ((vma->vm_flags & VM_EXEC) == 0))
		return;
#endif

	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;

	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &pg->flags))
		clear_bit(PG_arch_1, &pg->flags);
}

void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long addr, int len)
{
	unsigned long maddr;

	maddr = (unsigned long) kmap(page) + (addr & ~PAGE_MASK);
	flush_icache_range(maddr, maddr + len);
	kunmap(page);
}
EXPORT_SYMBOL(flush_icache_user_range);

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 * 
 * This must always be called with the mm->page_table_lock held
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t pte)
{
	/* handle i-cache coherency */
	unsigned long pfn = pte_pfn(pte);
#ifdef CONFIG_PPC32
	pmd_t *pmd;
#else
	unsigned long vsid;
	void *pgdir;
	pte_t *ptep;
	int local = 0;
	cpumask_t tmp;
	unsigned long flags;
#endif

	/* handle i-cache coherency */
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE) &&
	    !cpu_has_feature(CPU_FTR_NOEXECUTE) &&
	    pfn_valid(pfn)) {
		struct page *page = pfn_to_page(pfn);
		if (!PageReserved(page)
		    && !test_bit(PG_arch_1, &page->flags)) {
			if (vma->vm_mm == current->active_mm) {
#ifdef CONFIG_8xx
			/* On 8xx, cache control instructions (particularly 
		 	 * "dcbst" from flush_dcache_icache) fault as write 
			 * operation if there is an unpopulated TLB entry 
			 * for the address in question. To workaround that, 
			 * we invalidate the TLB here, thus avoiding dcbst 
			 * misbehaviour.
			 */
				_tlbie(address);
#endif
				__flush_dcache_icache((void *) address);
			} else
				flush_dcache_icache_page(page);
			set_bit(PG_arch_1, &page->flags);
		}
	}

#ifdef CONFIG_PPC_STD_MMU
	/* We only want HPTEs for linux PTEs that have _PAGE_ACCESSED set */
	if (!pte_young(pte) || address >= TASK_SIZE)
		return;
#ifdef CONFIG_PPC32
	if (Hash == 0)
		return;
	pmd = pmd_offset(pgd_offset(vma->vm_mm, address), address);
	if (!pmd_none(*pmd))
		add_hash_page(vma->vm_mm->context, address, pmd_val(*pmd));
#else
	pgdir = vma->vm_mm->pgd;
	if (pgdir == NULL)
		return;

	ptep = find_linux_pte(pgdir, ea);
	if (!ptep)
		return;

	vsid = get_vsid(vma->vm_mm->context.id, ea);

	local_irq_save(flags);
	tmp = cpumask_of_cpu(smp_processor_id());
	if (cpus_equal(vma->vm_mm->cpu_vm_mask, tmp))
		local = 1;

	__hash_page(ea, pte_val(pte) & (_PAGE_USER|_PAGE_RW), vsid, ptep,
		    0x300, local);
	local_irq_restore(flags);
#endif
#endif
}
