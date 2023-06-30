/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2006 Helge Deller <deller@gmx.de> (07-13-1999)
 * Copyright (C) 1999 SuSE GmbH Nuernberg
 * Copyright (C) 2000 Philipp Rumpf (prumpf@tux.org)
 *
 * Cache and TLB management
 *
 */
 
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <asm/pdc.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/shmparam.h>
#include <asm/mmu_context.h>

int split_tlb __ro_after_init;
int dcache_stride __ro_after_init;
int icache_stride __ro_after_init;
EXPORT_SYMBOL(dcache_stride);

void flush_dcache_page_asm(unsigned long phys_addr, unsigned long vaddr);
EXPORT_SYMBOL(flush_dcache_page_asm);
void purge_dcache_page_asm(unsigned long phys_addr, unsigned long vaddr);
void flush_icache_page_asm(unsigned long phys_addr, unsigned long vaddr);

/* Internal implementation in arch/parisc/kernel/pacache.S */
void flush_data_cache_local(void *);  /* flushes local data-cache only */
void flush_instruction_cache_local(void); /* flushes local code-cache only */

/* On some machines (i.e., ones with the Merced bus), there can be
 * only a single PxTLB broadcast at a time; this must be guaranteed
 * by software. We need a spinlock around all TLB flushes to ensure
 * this.
 */
DEFINE_SPINLOCK(pa_tlb_flush_lock);

#if defined(CONFIG_64BIT) && defined(CONFIG_SMP)
int pa_serialize_tlb_flushes __ro_after_init;
#endif

struct pdc_cache_info cache_info __ro_after_init;
#ifndef CONFIG_PA20
static struct pdc_btlb_info btlb_info __ro_after_init;
#endif

DEFINE_STATIC_KEY_TRUE(parisc_has_cache);
DEFINE_STATIC_KEY_TRUE(parisc_has_dcache);
DEFINE_STATIC_KEY_TRUE(parisc_has_icache);

static void cache_flush_local_cpu(void *dummy)
{
	if (static_branch_likely(&parisc_has_icache))
		flush_instruction_cache_local();
	if (static_branch_likely(&parisc_has_dcache))
		flush_data_cache_local(NULL);
}

void flush_cache_all_local(void)
{
	cache_flush_local_cpu(NULL);
}

void flush_cache_all(void)
{
	if (static_branch_likely(&parisc_has_cache))
		on_each_cpu(cache_flush_local_cpu, NULL, 1);
}

static inline void flush_data_cache(void)
{
	if (static_branch_likely(&parisc_has_dcache))
		on_each_cpu(flush_data_cache_local, NULL, 1);
}


/* Kernel virtual address of pfn.  */
#define pfn_va(pfn)	__va(PFN_PHYS(pfn))

void
__update_cache(pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);
	struct page *page;

	/* We don't have pte special.  As a result, we can be called with
	   an invalid pfn and we don't need to flush the kernel dcache page.
	   This occurs with FireGL card in C8000.  */
	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	if (page_mapping_file(page) &&
	    test_bit(PG_dcache_dirty, &page->flags)) {
		flush_kernel_dcache_page_addr(pfn_va(pfn));
		clear_bit(PG_dcache_dirty, &page->flags);
	} else if (parisc_requires_coherency())
		flush_kernel_dcache_page_addr(pfn_va(pfn));
}

void
show_cache_info(struct seq_file *m)
{
	char buf[32];

	seq_printf(m, "I-cache\t\t: %ld KB\n", 
		cache_info.ic_size/1024 );
	if (cache_info.dc_loop != 1)
		snprintf(buf, 32, "%lu-way associative", cache_info.dc_loop);
	seq_printf(m, "D-cache\t\t: %ld KB (%s%s, %s, alias=%d)\n",
		cache_info.dc_size/1024,
		(cache_info.dc_conf.cc_wt ? "WT":"WB"),
		(cache_info.dc_conf.cc_sh ? ", shared I/D":""),
		((cache_info.dc_loop == 1) ? "direct mapped" : buf),
		cache_info.dc_conf.cc_alias
	);
	seq_printf(m, "ITLB entries\t: %ld\n" "DTLB entries\t: %ld%s\n",
		cache_info.it_size,
		cache_info.dt_size,
		cache_info.dt_conf.tc_sh ? " - shared with ITLB":""
	);
		
#ifndef CONFIG_PA20
	/* BTLB - Block TLB */
	if (btlb_info.max_size==0) {
		seq_printf(m, "BTLB\t\t: not supported\n" );
	} else {
		seq_printf(m, 
		"BTLB fixed\t: max. %d pages, pagesize=%d (%dMB)\n"
		"BTLB fix-entr.\t: %d instruction, %d data (%d combined)\n"
		"BTLB var-entr.\t: %d instruction, %d data (%d combined)\n",
		btlb_info.max_size, (int)4096,
		btlb_info.max_size>>8,
		btlb_info.fixed_range_info.num_i,
		btlb_info.fixed_range_info.num_d,
		btlb_info.fixed_range_info.num_comb, 
		btlb_info.variable_range_info.num_i,
		btlb_info.variable_range_info.num_d,
		btlb_info.variable_range_info.num_comb
		);
	}
#endif
}

void __init 
parisc_cache_init(void)
{
	if (pdc_cache_info(&cache_info) < 0)
		panic("parisc_cache_init: pdc_cache_info failed");

#if 0
	printk("ic_size %lx dc_size %lx it_size %lx\n",
		cache_info.ic_size,
		cache_info.dc_size,
		cache_info.it_size);

	printk("DC  base 0x%lx stride 0x%lx count 0x%lx loop 0x%lx\n",
		cache_info.dc_base,
		cache_info.dc_stride,
		cache_info.dc_count,
		cache_info.dc_loop);

	printk("dc_conf = 0x%lx  alias %d blk %d line %d shift %d\n",
		*(unsigned long *) (&cache_info.dc_conf),
		cache_info.dc_conf.cc_alias,
		cache_info.dc_conf.cc_block,
		cache_info.dc_conf.cc_line,
		cache_info.dc_conf.cc_shift);
	printk("	wt %d sh %d cst %d hv %d\n",
		cache_info.dc_conf.cc_wt,
		cache_info.dc_conf.cc_sh,
		cache_info.dc_conf.cc_cst,
		cache_info.dc_conf.cc_hv);

	printk("IC  base 0x%lx stride 0x%lx count 0x%lx loop 0x%lx\n",
		cache_info.ic_base,
		cache_info.ic_stride,
		cache_info.ic_count,
		cache_info.ic_loop);

	printk("IT  base 0x%lx stride 0x%lx count 0x%lx loop 0x%lx off_base 0x%lx off_stride 0x%lx off_count 0x%lx\n",
		cache_info.it_sp_base,
		cache_info.it_sp_stride,
		cache_info.it_sp_count,
		cache_info.it_loop,
		cache_info.it_off_base,
		cache_info.it_off_stride,
		cache_info.it_off_count);

	printk("DT  base 0x%lx stride 0x%lx count 0x%lx loop 0x%lx off_base 0x%lx off_stride 0x%lx off_count 0x%lx\n",
		cache_info.dt_sp_base,
		cache_info.dt_sp_stride,
		cache_info.dt_sp_count,
		cache_info.dt_loop,
		cache_info.dt_off_base,
		cache_info.dt_off_stride,
		cache_info.dt_off_count);

	printk("ic_conf = 0x%lx  alias %d blk %d line %d shift %d\n",
		*(unsigned long *) (&cache_info.ic_conf),
		cache_info.ic_conf.cc_alias,
		cache_info.ic_conf.cc_block,
		cache_info.ic_conf.cc_line,
		cache_info.ic_conf.cc_shift);
	printk("	wt %d sh %d cst %d hv %d\n",
		cache_info.ic_conf.cc_wt,
		cache_info.ic_conf.cc_sh,
		cache_info.ic_conf.cc_cst,
		cache_info.ic_conf.cc_hv);

	printk("D-TLB conf: sh %d page %d cst %d aid %d sr %d\n",
		cache_info.dt_conf.tc_sh,
		cache_info.dt_conf.tc_page,
		cache_info.dt_conf.tc_cst,
		cache_info.dt_conf.tc_aid,
		cache_info.dt_conf.tc_sr);

	printk("I-TLB conf: sh %d page %d cst %d aid %d sr %d\n",
		cache_info.it_conf.tc_sh,
		cache_info.it_conf.tc_page,
		cache_info.it_conf.tc_cst,
		cache_info.it_conf.tc_aid,
		cache_info.it_conf.tc_sr);
#endif

	split_tlb = 0;
	if (cache_info.dt_conf.tc_sh == 0 || cache_info.dt_conf.tc_sh == 2) {
		if (cache_info.dt_conf.tc_sh == 2)
			printk(KERN_WARNING "Unexpected TLB configuration. "
			"Will flush I/D separately (could be optimized).\n");

		split_tlb = 1;
	}

	/* "New and Improved" version from Jim Hull 
	 *	(1 << (cc_block-1)) * (cc_line << (4 + cnf.cc_shift))
	 * The following CAFL_STRIDE is an optimized version, see
	 * http://lists.parisc-linux.org/pipermail/parisc-linux/2004-June/023625.html
	 * http://lists.parisc-linux.org/pipermail/parisc-linux/2004-June/023671.html
	 */
#define CAFL_STRIDE(cnf) (cnf.cc_line << (3 + cnf.cc_block + cnf.cc_shift))
	dcache_stride = CAFL_STRIDE(cache_info.dc_conf);
	icache_stride = CAFL_STRIDE(cache_info.ic_conf);
#undef CAFL_STRIDE

#ifndef CONFIG_PA20
	if (pdc_btlb_info(&btlb_info) < 0) {
		memset(&btlb_info, 0, sizeof btlb_info);
	}
#endif

	if ((boot_cpu_data.pdc.capabilities & PDC_MODEL_NVA_MASK) ==
						PDC_MODEL_NVA_UNSUPPORTED) {
		printk(KERN_WARNING "parisc_cache_init: Only equivalent aliasing supported!\n");
#if 0
		panic("SMP kernel required to avoid non-equivalent aliasing");
#endif
	}
}

void disable_sr_hashing(void)
{
	int srhash_type, retval;
	unsigned long space_bits;

	switch (boot_cpu_data.cpu_type) {
	case pcx: /* We shouldn't get this far.  setup.c should prevent it. */
		BUG();
		return;

	case pcxs:
	case pcxt:
	case pcxt_:
		srhash_type = SRHASH_PCXST;
		break;

	case pcxl:
		srhash_type = SRHASH_PCXL;
		break;

	case pcxl2: /* pcxl2 doesn't support space register hashing */
		return;

	default: /* Currently all PA2.0 machines use the same ins. sequence */
		srhash_type = SRHASH_PA20;
		break;
	}

	disable_sr_hashing_asm(srhash_type);

	retval = pdc_spaceid_bits(&space_bits);
	/* If this procedure isn't implemented, don't panic. */
	if (retval < 0 && retval != PDC_BAD_OPTION)
		panic("pdc_spaceid_bits call failed.\n");
	if (space_bits != 0)
		panic("SpaceID hashing is still on!\n");
}

static inline void
__flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr,
		   unsigned long physaddr)
{
	if (!static_branch_likely(&parisc_has_cache))
		return;
	preempt_disable();
	flush_dcache_page_asm(physaddr, vmaddr);
	if (vma->vm_flags & VM_EXEC)
		flush_icache_page_asm(physaddr, vmaddr);
	preempt_enable();
}

static void flush_user_cache_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	unsigned long flags, space, pgd, prot;
#ifdef CONFIG_TLB_PTLOCK
	unsigned long pgd_lock;
#endif

	vmaddr &= PAGE_MASK;

	preempt_disable();

	/* Set context for flush */
	local_irq_save(flags);
	prot = mfctl(8);
	space = mfsp(SR_USER);
	pgd = mfctl(25);
#ifdef CONFIG_TLB_PTLOCK
	pgd_lock = mfctl(28);
#endif
	switch_mm_irqs_off(NULL, vma->vm_mm, NULL);
	local_irq_restore(flags);

	flush_user_dcache_range_asm(vmaddr, vmaddr + PAGE_SIZE);
	if (vma->vm_flags & VM_EXEC)
		flush_user_icache_range_asm(vmaddr, vmaddr + PAGE_SIZE);
	flush_tlb_page(vma, vmaddr);

	/* Restore previous context */
	local_irq_save(flags);
#ifdef CONFIG_TLB_PTLOCK
	mtctl(pgd_lock, 28);
#endif
	mtctl(pgd, 25);
	mtsp(space, SR_USER);
	mtctl(prot, 8);
	local_irq_restore(flags);

	preempt_enable();
}

static inline pte_t *get_ptep(struct mm_struct *mm, unsigned long addr)
{
	pte_t *ptep = NULL;
	pgd_t *pgd = mm->pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (!pgd_none(*pgd)) {
		p4d = p4d_offset(pgd, addr);
		if (!p4d_none(*p4d)) {
			pud = pud_offset(p4d, addr);
			if (!pud_none(*pud)) {
				pmd = pmd_offset(pud, addr);
				if (!pmd_none(*pmd))
					ptep = pte_offset_map(pmd, addr);
			}
		}
	}
	return ptep;
}

static inline bool pte_needs_flush(pte_t pte)
{
	return (pte_val(pte) & (_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_NO_CACHE))
		== (_PAGE_PRESENT | _PAGE_ACCESSED);
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping_file(page);
	struct vm_area_struct *mpnt;
	unsigned long offset;
	unsigned long addr, old_addr = 0;
	unsigned long count = 0;
	unsigned long flags;
	pgoff_t pgoff;

	if (mapping && !mapping_mapped(mapping)) {
		set_bit(PG_dcache_dirty, &page->flags);
		return;
	}

	flush_kernel_dcache_page_addr(page_address(page));

	if (!mapping)
		return;

	pgoff = page->index;

	/*
	 * We have carefully arranged in arch_get_unmapped_area() that
	 * *any* mappings of a file are always congruently mapped (whether
	 * declared as MAP_PRIVATE or MAP_SHARED), so we only need
	 * to flush one address here for them all to become coherent
	 * on machines that support equivalent aliasing
	 */
	flush_dcache_mmap_lock_irqsave(mapping, flags);
	vma_interval_tree_foreach(mpnt, &mapping->i_mmap, pgoff, pgoff) {
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		addr = mpnt->vm_start + offset;
		if (parisc_requires_coherency()) {
			bool needs_flush = false;
			pte_t *ptep;

			ptep = get_ptep(mpnt->vm_mm, addr);
			if (ptep) {
				needs_flush = pte_needs_flush(*ptep);
				pte_unmap(ptep);
			}
			if (needs_flush)
				flush_user_cache_page(mpnt, addr);
		} else {
			/*
			 * The TLB is the engine of coherence on parisc:
			 * The CPU is entitled to speculate any page
			 * with a TLB mapping, so here we kill the
			 * mapping then flush the page along a special
			 * flush only alias mapping. This guarantees that
			 * the page is no-longer in the cache for any
			 * process and nor may it be speculatively read
			 * in (until the user or kernel specifically
			 * accesses it, of course)
			 */
			flush_tlb_page(mpnt, addr);
			if (old_addr == 0 || (old_addr & (SHM_COLOUR - 1))
					!= (addr & (SHM_COLOUR - 1))) {
				__flush_cache_page(mpnt, addr, page_to_phys(page));
				/*
				 * Software is allowed to have any number
				 * of private mappings to a page.
				 */
				if (!(mpnt->vm_flags & VM_SHARED))
					continue;
				if (old_addr)
					pr_err("INEQUIVALENT ALIASES 0x%lx and 0x%lx in file %pD\n",
						old_addr, addr, mpnt->vm_file);
				old_addr = addr;
			}
		}
		WARN_ON(++count == 4096);
	}
	flush_dcache_mmap_unlock_irqrestore(mapping, flags);
}
EXPORT_SYMBOL(flush_dcache_page);

/* Defined in arch/parisc/kernel/pacache.S */
EXPORT_SYMBOL(flush_kernel_dcache_range_asm);
EXPORT_SYMBOL(flush_kernel_icache_range_asm);

#define FLUSH_THRESHOLD 0x80000 /* 0.5MB */
static unsigned long parisc_cache_flush_threshold __ro_after_init = FLUSH_THRESHOLD;

#define FLUSH_TLB_THRESHOLD (16*1024) /* 16 KiB minimum TLB threshold */
static unsigned long parisc_tlb_flush_threshold __ro_after_init = ~0UL;

void __init parisc_setup_cache_timing(void)
{
	unsigned long rangetime, alltime;
	unsigned long size;
	unsigned long threshold, threshold2;

	alltime = mfctl(16);
	flush_data_cache();
	alltime = mfctl(16) - alltime;

	size = (unsigned long)(_end - _text);
	rangetime = mfctl(16);
	flush_kernel_dcache_range((unsigned long)_text, size);
	rangetime = mfctl(16) - rangetime;

	printk(KERN_DEBUG "Whole cache flush %lu cycles, flushing %lu bytes %lu cycles\n",
		alltime, size, rangetime);

	threshold = L1_CACHE_ALIGN((unsigned long)((uint64_t)size * alltime / rangetime));
	pr_info("Calculated flush threshold is %lu KiB\n",
		threshold/1024);

	/*
	 * The threshold computed above isn't very reliable. The following
	 * heuristic works reasonably well on c8000/rp3440.
	 */
	threshold2 = cache_info.dc_size * num_online_cpus();
	parisc_cache_flush_threshold = threshold2;
	printk(KERN_INFO "Cache flush threshold set to %lu KiB\n",
		parisc_cache_flush_threshold/1024);

	/* calculate TLB flush threshold */

	/* On SMP machines, skip the TLB measure of kernel text which
	 * has been mapped as huge pages. */
	if (num_online_cpus() > 1 && !parisc_requires_coherency()) {
		threshold = max(cache_info.it_size, cache_info.dt_size);
		threshold *= PAGE_SIZE;
		threshold /= num_online_cpus();
		goto set_tlb_threshold;
	}

	size = (unsigned long)_end - (unsigned long)_text;
	rangetime = mfctl(16);
	flush_tlb_kernel_range((unsigned long)_text, (unsigned long)_end);
	rangetime = mfctl(16) - rangetime;

	alltime = mfctl(16);
	flush_tlb_all();
	alltime = mfctl(16) - alltime;

	printk(KERN_INFO "Whole TLB flush %lu cycles, Range flush %lu bytes %lu cycles\n",
		alltime, size, rangetime);

	threshold = PAGE_ALIGN((num_online_cpus() * size * alltime) / rangetime);
	printk(KERN_INFO "Calculated TLB flush threshold %lu KiB\n",
		threshold/1024);

set_tlb_threshold:
	if (threshold > FLUSH_TLB_THRESHOLD)
		parisc_tlb_flush_threshold = threshold;
	else
		parisc_tlb_flush_threshold = FLUSH_TLB_THRESHOLD;

	printk(KERN_INFO "TLB flush threshold set to %lu KiB\n",
		parisc_tlb_flush_threshold/1024);
}

extern void purge_kernel_dcache_page_asm(unsigned long);
extern void clear_user_page_asm(void *, unsigned long);
extern void copy_user_page_asm(void *, void *, unsigned long);

void flush_kernel_dcache_page_addr(const void *addr)
{
	unsigned long flags;

	flush_kernel_dcache_page_asm(addr);
	purge_tlb_start(flags);
	pdtlb(SR_KERNEL, addr);
	purge_tlb_end(flags);
}
EXPORT_SYMBOL(flush_kernel_dcache_page_addr);

static void flush_cache_page_if_present(struct vm_area_struct *vma,
	unsigned long vmaddr, unsigned long pfn)
{
	bool needs_flush = false;
	pte_t *ptep;

	/*
	 * The pte check is racy and sometimes the flush will trigger
	 * a non-access TLB miss. Hopefully, the page has already been
	 * flushed.
	 */
	ptep = get_ptep(vma->vm_mm, vmaddr);
	if (ptep) {
		needs_flush = pte_needs_flush(*ptep);
		pte_unmap(ptep);
	}
	if (needs_flush)
		flush_cache_page(vma, vmaddr, pfn);
}

void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto, *kfrom;

	kfrom = kmap_local_page(from);
	kto = kmap_local_page(to);
	flush_cache_page_if_present(vma, vaddr, page_to_pfn(from));
	copy_page_asm(kto, kfrom);
	kunmap_local(kto);
	kunmap_local(kfrom);
}

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		unsigned long user_vaddr, void *dst, void *src, int len)
{
	flush_cache_page_if_present(vma, user_vaddr, page_to_pfn(page));
	memcpy(dst, src, len);
	flush_kernel_dcache_range_asm((unsigned long)dst, (unsigned long)dst + len);
}

void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
		unsigned long user_vaddr, void *dst, void *src, int len)
{
	flush_cache_page_if_present(vma, user_vaddr, page_to_pfn(page));
	memcpy(dst, src, len);
}

/* __flush_tlb_range()
 *
 * returns 1 if all TLBs were flushed.
 */
int __flush_tlb_range(unsigned long sid, unsigned long start,
		      unsigned long end)
{
	unsigned long flags;

	if ((!IS_ENABLED(CONFIG_SMP) || !arch_irqs_disabled()) &&
	    end - start >= parisc_tlb_flush_threshold) {
		flush_tlb_all();
		return 1;
	}

	/* Purge TLB entries for small ranges using the pdtlb and
	   pitlb instructions.  These instructions execute locally
	   but cause a purge request to be broadcast to other TLBs.  */
	while (start < end) {
		purge_tlb_start(flags);
		mtsp(sid, SR_TEMP1);
		pdtlb(SR_TEMP1, start);
		pitlb(SR_TEMP1, start);
		purge_tlb_end(flags);
		start += PAGE_SIZE;
	}
	return 0;
}

static void flush_cache_pages(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	unsigned long addr, pfn;
	pte_t *ptep;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		bool needs_flush = false;
		/*
		 * The vma can contain pages that aren't present. Although
		 * the pte search is expensive, we need the pte to find the
		 * page pfn and to check whether the page should be flushed.
		 */
		ptep = get_ptep(vma->vm_mm, addr);
		if (ptep) {
			needs_flush = pte_needs_flush(*ptep);
			pfn = pte_pfn(*ptep);
			pte_unmap(ptep);
		}
		if (needs_flush) {
			if (parisc_requires_coherency()) {
				flush_user_cache_page(vma, addr);
			} else {
				if (WARN_ON(!pfn_valid(pfn)))
					return;
				__flush_cache_page(vma, addr, PFN_PHYS(pfn));
			}
		}
	}
}

static inline unsigned long mm_total_size(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	unsigned long usize = 0;
	VMA_ITERATOR(vmi, mm, 0);

	for_each_vma(vmi, vma) {
		if (usize >= parisc_cache_flush_threshold)
			break;
		usize += vma->vm_end - vma->vm_start;
	}
	return usize;
}

void flush_cache_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	/*
	 * Flushing the whole cache on each cpu takes forever on
	 * rp3440, etc. So, avoid it if the mm isn't too big.
	 *
	 * Note that we must flush the entire cache on machines
	 * with aliasing caches to prevent random segmentation
	 * faults.
	 */
	if (!parisc_requires_coherency()
	    ||  mm_total_size(mm) >= parisc_cache_flush_threshold) {
		if (WARN_ON(IS_ENABLED(CONFIG_SMP) && arch_irqs_disabled()))
			return;
		flush_tlb_all();
		flush_cache_all();
		return;
	}

	/* Flush mm */
	for_each_vma(vmi, vma)
		flush_cache_pages(vma, vma->vm_start, vma->vm_end);
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	if (!parisc_requires_coherency()
	    || end - start >= parisc_cache_flush_threshold) {
		if (WARN_ON(IS_ENABLED(CONFIG_SMP) && arch_irqs_disabled()))
			return;
		flush_tlb_range(vma, start, end);
		flush_cache_all();
		return;
	}

	flush_cache_pages(vma, start, end);
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr, unsigned long pfn)
{
	if (WARN_ON(!pfn_valid(pfn)))
		return;
	if (parisc_requires_coherency())
		flush_user_cache_page(vma, vmaddr);
	else
		__flush_cache_page(vma, vmaddr, PFN_PHYS(pfn));
}

void flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
	if (!PageAnon(page))
		return;

	if (parisc_requires_coherency()) {
		if (vma->vm_flags & VM_SHARED)
			flush_data_cache();
		else
			flush_user_cache_page(vma, vmaddr);
		return;
	}

	flush_tlb_page(vma, vmaddr);
	preempt_disable();
	flush_dcache_page_asm(page_to_phys(page), vmaddr);
	preempt_enable();
}

void flush_kernel_vmap_range(void *vaddr, int size)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end = start + size;

	if ((!IS_ENABLED(CONFIG_SMP) || !arch_irqs_disabled()) &&
	    (unsigned long)size >= parisc_cache_flush_threshold) {
		flush_tlb_kernel_range(start, end);
		flush_data_cache();
		return;
	}

	flush_kernel_dcache_range_asm(start, end);
	flush_tlb_kernel_range(start, end);
}
EXPORT_SYMBOL(flush_kernel_vmap_range);

void invalidate_kernel_vmap_range(void *vaddr, int size)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end = start + size;

	/* Ensure DMA is complete */
	asm_syncdma();

	if ((!IS_ENABLED(CONFIG_SMP) || !arch_irqs_disabled()) &&
	    (unsigned long)size >= parisc_cache_flush_threshold) {
		flush_tlb_kernel_range(start, end);
		flush_data_cache();
		return;
	}

	purge_kernel_dcache_range_asm(start, end);
	flush_tlb_kernel_range(start, end);
}
EXPORT_SYMBOL(invalidate_kernel_vmap_range);
