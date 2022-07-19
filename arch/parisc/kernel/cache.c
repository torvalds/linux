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

int split_tlb __ro_after_init;
int dcache_stride __ro_after_init;
int icache_stride __ro_after_init;
EXPORT_SYMBOL(dcache_stride);

void flush_dcache_page_asm(unsigned long phys_addr, unsigned long vaddr);
EXPORT_SYMBOL(flush_dcache_page_asm);
void purge_dcache_page_asm(unsigned long phys_addr, unsigned long vaddr);
void flush_icache_page_asm(unsigned long phys_addr, unsigned long vaddr);


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

#ifdef CONFIG_SMP
void
flush_data_cache(void)
{
	on_each_cpu(flush_data_cache_local, NULL, 1);
}
void 
flush_instruction_cache(void)
{
	on_each_cpu(flush_instruction_cache_local, NULL, 1);
}
#endif

void
flush_cache_all_local(void)
{
	flush_instruction_cache_local(NULL);
	flush_data_cache_local(NULL);
}
EXPORT_SYMBOL(flush_cache_all_local);

/* Virtual address of pfn.  */
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
	seq_printf(m, "D-cache\t\t: %ld KB (%s%s, %s)\n",
		cache_info.dc_size/1024,
		(cache_info.dc_conf.cc_wt ? "WT":"WB"),
		(cache_info.dc_conf.cc_sh ? ", shared I/D":""),
		((cache_info.dc_loop == 1) ? "direct mapped" : buf));
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

void __init disable_sr_hashing(void)
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
	preempt_disable();
	flush_dcache_page_asm(physaddr, vmaddr);
	if (vma->vm_flags & VM_EXEC)
		flush_icache_page_asm(physaddr, vmaddr);
	preempt_enable();
}

static inline void
__purge_cache_page(struct vm_area_struct *vma, unsigned long vmaddr,
		   unsigned long physaddr)
{
	preempt_disable();
	purge_dcache_page_asm(physaddr, vmaddr);
	if (vma->vm_flags & VM_EXEC)
		flush_icache_page_asm(physaddr, vmaddr);
	preempt_enable();
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping_file(page);
	struct vm_area_struct *mpnt;
	unsigned long offset;
	unsigned long addr, old_addr = 0;
	pgoff_t pgoff;

	if (mapping && !mapping_mapped(mapping)) {
		set_bit(PG_dcache_dirty, &page->flags);
		return;
	}

	flush_kernel_dcache_page_addr(page_address(page));

	if (!mapping)
		return;

	pgoff = page->index;

	/* We have carefully arranged in arch_get_unmapped_area() that
	 * *any* mappings of a file are always congruently mapped (whether
	 * declared as MAP_PRIVATE or MAP_SHARED), so we only need
	 * to flush one address here for them all to become coherent */

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_foreach(mpnt, &mapping->i_mmap, pgoff, pgoff) {
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		addr = mpnt->vm_start + offset;

		/* The TLB is the engine of coherence on parisc: The
		 * CPU is entitled to speculate any page with a TLB
		 * mapping, so here we kill the mapping then flush the
		 * page along a special flush only alias mapping.
		 * This guarantees that the page is no-longer in the
		 * cache for any process and nor may it be
		 * speculatively read in (until the user or kernel
		 * specifically accesses it, of course) */

		flush_tlb_page(mpnt, addr);
		if (old_addr == 0 || (old_addr & (SHM_COLOUR - 1))
				      != (addr & (SHM_COLOUR - 1))) {
			__flush_cache_page(mpnt, addr, page_to_phys(page));
			if (parisc_requires_coherency() && old_addr)
				printk(KERN_ERR "INEQUIVALENT ALIASES 0x%lx and 0x%lx in file %pD\n", old_addr, addr, mpnt->vm_file);
			old_addr = addr;
		}
	}
	flush_dcache_mmap_unlock(mapping);
}
EXPORT_SYMBOL(flush_dcache_page);

/* Defined in arch/parisc/kernel/pacache.S */
EXPORT_SYMBOL(flush_kernel_dcache_range_asm);
EXPORT_SYMBOL(flush_data_cache_local);
EXPORT_SYMBOL(flush_kernel_icache_range_asm);

#define FLUSH_THRESHOLD 0x80000 /* 0.5MB */
static unsigned long parisc_cache_flush_threshold __ro_after_init = FLUSH_THRESHOLD;

#define FLUSH_TLB_THRESHOLD (16*1024) /* 16 KiB minimum TLB threshold */
static unsigned long parisc_tlb_flush_threshold __ro_after_init = ~0UL;

void __init parisc_setup_cache_timing(void)
{
	unsigned long rangetime, alltime;
	unsigned long size;
	unsigned long threshold;

	alltime = mfctl(16);
	flush_data_cache();
	alltime = mfctl(16) - alltime;

	size = (unsigned long)(_end - _text);
	rangetime = mfctl(16);
	flush_kernel_dcache_range((unsigned long)_text, size);
	rangetime = mfctl(16) - rangetime;

	printk(KERN_DEBUG "Whole cache flush %lu cycles, flushing %lu bytes %lu cycles\n",
		alltime, size, rangetime);

	threshold = L1_CACHE_ALIGN(size * alltime / rangetime);
	if (threshold > cache_info.dc_size)
		threshold = cache_info.dc_size;
	if (threshold)
		parisc_cache_flush_threshold = threshold;
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

void flush_kernel_dcache_page_addr(void *addr)
{
	unsigned long flags;

	flush_kernel_dcache_page_asm(addr);
	purge_tlb_start(flags);
	pdtlb_kernel(addr);
	purge_tlb_end(flags);
}
EXPORT_SYMBOL(flush_kernel_dcache_page_addr);

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
	struct page *pg)
{
       /* Copy using kernel mapping.  No coherency is needed (all in
	  kunmap) for the `to' page.  However, the `from' page needs to
	  be flushed through a mapping equivalent to the user mapping
	  before it can be accessed through the kernel mapping. */
	preempt_disable();
	flush_dcache_page_asm(__pa(vfrom), vaddr);
	copy_page_asm(vto, vfrom);
	preempt_enable();
}
EXPORT_SYMBOL(copy_user_page);

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
		mtsp(sid, 1);
		pdtlb(start);
		pitlb(start);
		purge_tlb_end(flags);
		start += PAGE_SIZE;
	}
	return 0;
}

static void cacheflush_h_tmp_function(void *dummy)
{
	flush_cache_all_local();
}

void flush_cache_all(void)
{
	on_each_cpu(cacheflush_h_tmp_function, NULL, 1);
}

static inline unsigned long mm_total_size(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	unsigned long usize = 0;

	for (vma = mm->mmap; vma; vma = vma->vm_next)
		usize += vma->vm_end - vma->vm_start;
	return usize;
}

static inline pte_t *get_ptep(pgd_t *pgd, unsigned long addr)
{
	pte_t *ptep = NULL;

	if (!pgd_none(*pgd)) {
		p4d_t *p4d = p4d_offset(pgd, addr);
		if (!p4d_none(*p4d)) {
			pud_t *pud = pud_offset(p4d, addr);
			if (!pud_none(*pud)) {
				pmd_t *pmd = pmd_offset(pud, addr);
				if (!pmd_none(*pmd))
					ptep = pte_offset_map(pmd, addr);
			}
		}
	}
	return ptep;
}

void flush_cache_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	pgd_t *pgd;

	/* Flushing the whole cache on each cpu takes forever on
	   rp3440, etc.  So, avoid it if the mm isn't too big.  */
	if ((!IS_ENABLED(CONFIG_SMP) || !arch_irqs_disabled()) &&
	    mm_total_size(mm) >= parisc_cache_flush_threshold) {
		if (mm->context)
			flush_tlb_all();
		flush_cache_all();
		return;
	}

	if (mm->context == mfsp(3)) {
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			flush_user_dcache_range_asm(vma->vm_start, vma->vm_end);
			if (vma->vm_flags & VM_EXEC)
				flush_user_icache_range_asm(vma->vm_start, vma->vm_end);
			flush_tlb_range(vma, vma->vm_start, vma->vm_end);
		}
		return;
	}

	pgd = mm->pgd;
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		unsigned long addr;

		for (addr = vma->vm_start; addr < vma->vm_end;
		     addr += PAGE_SIZE) {
			unsigned long pfn;
			pte_t *ptep = get_ptep(pgd, addr);
			if (!ptep)
				continue;
			pfn = pte_pfn(*ptep);
			if (!pfn_valid(pfn))
				continue;
			if (unlikely(mm->context)) {
				flush_tlb_page(vma, addr);
				__flush_cache_page(vma, addr, PFN_PHYS(pfn));
			} else {
				__purge_cache_page(vma, addr, PFN_PHYS(pfn));
			}
		}
	}
}

void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	pgd_t *pgd;
	unsigned long addr;

	if ((!IS_ENABLED(CONFIG_SMP) || !arch_irqs_disabled()) &&
	    end - start >= parisc_cache_flush_threshold) {
		if (vma->vm_mm->context)
			flush_tlb_range(vma, start, end);
		flush_cache_all();
		return;
	}

	if (vma->vm_mm->context == mfsp(3)) {
		flush_user_dcache_range_asm(start, end);
		if (vma->vm_flags & VM_EXEC)
			flush_user_icache_range_asm(start, end);
		flush_tlb_range(vma, start, end);
		return;
	}

	pgd = vma->vm_mm->pgd;
	for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
		unsigned long pfn;
		pte_t *ptep = get_ptep(pgd, addr);
		if (!ptep)
			continue;
		pfn = pte_pfn(*ptep);
		if (pfn_valid(pfn)) {
			if (unlikely(vma->vm_mm->context)) {
				flush_tlb_page(vma, addr);
				__flush_cache_page(vma, addr, PFN_PHYS(pfn));
			} else {
				__purge_cache_page(vma, addr, PFN_PHYS(pfn));
			}
		}
	}
}

void
flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr, unsigned long pfn)
{
	if (pfn_valid(pfn)) {
		if (likely(vma->vm_mm->context)) {
			flush_tlb_page(vma, vmaddr);
			__flush_cache_page(vma, vmaddr, PFN_PHYS(pfn));
		} else {
			__purge_cache_page(vma, vmaddr, PFN_PHYS(pfn));
		}
	}
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
