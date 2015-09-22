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
#include <asm/pdc.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/shmparam.h>

int split_tlb __read_mostly;
int dcache_stride __read_mostly;
int icache_stride __read_mostly;
EXPORT_SYMBOL(dcache_stride);

void flush_dcache_page_asm(unsigned long phys_addr, unsigned long vaddr);
EXPORT_SYMBOL(flush_dcache_page_asm);
void flush_icache_page_asm(unsigned long phys_addr, unsigned long vaddr);


/* On some machines (e.g. ones with the Merced bus), there can be
 * only a single PxTLB broadcast at a time; this must be guaranteed
 * by software.  We put a spinlock around all TLB flushes  to
 * ensure this.
 */
DEFINE_SPINLOCK(pa_tlb_lock);

struct pdc_cache_info cache_info __read_mostly;
#ifndef CONFIG_PA20
static struct pdc_btlb_info btlb_info __read_mostly;
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
update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t *ptep)
{
	unsigned long pfn = pte_pfn(*ptep);
	struct page *page;

	/* We don't have pte special.  As a result, we can be called with
	   an invalid pfn and we don't need to flush the kernel dcache page.
	   This occurs with FireGL card in C8000.  */
	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	if (page_mapping(page) && test_bit(PG_dcache_dirty, &page->flags)) {
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

	printk("D-TLB conf: sh %d page %d cst %d aid %d pad1 %d\n",
		cache_info.dt_conf.tc_sh,
		cache_info.dt_conf.tc_page,
		cache_info.dt_conf.tc_cst,
		cache_info.dt_conf.tc_aid,
		cache_info.dt_conf.tc_pad1);

	printk("I-TLB conf: sh %d page %d cst %d aid %d pad1 %d\n",
		cache_info.it_conf.tc_sh,
		cache_info.it_conf.tc_page,
		cache_info.it_conf.tc_cst,
		cache_info.it_conf.tc_aid,
		cache_info.it_conf.tc_pad1);
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
	preempt_disable();
	flush_dcache_page_asm(physaddr, vmaddr);
	if (vma->vm_flags & VM_EXEC)
		flush_icache_page_asm(physaddr, vmaddr);
	preempt_enable();
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	struct vm_area_struct *mpnt;
	unsigned long offset;
	unsigned long addr, old_addr = 0;
	pgoff_t pgoff;

	if (mapping && !mapping_mapped(mapping)) {
		set_bit(PG_dcache_dirty, &page->flags);
		return;
	}

	flush_kernel_dcache_page(page);

	if (!mapping)
		return;

	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

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
			if (old_addr)
				printk(KERN_ERR "INEQUIVALENT ALIASES 0x%lx and 0x%lx in file %s\n", old_addr, addr, mpnt->vm_file ? (char *)mpnt->vm_file->f_path.dentry->d_name.name : "(null)");
			old_addr = addr;
		}
	}
	flush_dcache_mmap_unlock(mapping);
}
EXPORT_SYMBOL(flush_dcache_page);

/* Defined in arch/parisc/kernel/pacache.S */
EXPORT_SYMBOL(flush_kernel_dcache_range_asm);
EXPORT_SYMBOL(flush_kernel_dcache_page_asm);
EXPORT_SYMBOL(flush_data_cache_local);
EXPORT_SYMBOL(flush_kernel_icache_range_asm);

#define FLUSH_THRESHOLD 0x80000 /* 0.5MB */
static unsigned long parisc_cache_flush_threshold __read_mostly = FLUSH_THRESHOLD;

#define FLUSH_TLB_THRESHOLD (2*1024*1024) /* 2MB initial TLB threshold */
static unsigned long parisc_tlb_flush_threshold __read_mostly = FLUSH_TLB_THRESHOLD;

void __init parisc_setup_cache_timing(void)
{
	unsigned long rangetime, alltime;
	unsigned long size, start;

	alltime = mfctl(16);
	flush_data_cache();
	alltime = mfctl(16) - alltime;

	size = (unsigned long)(_end - _text);
	rangetime = mfctl(16);
	flush_kernel_dcache_range((unsigned long)_text, size);
	rangetime = mfctl(16) - rangetime;

	printk(KERN_DEBUG "Whole cache flush %lu cycles, flushing %lu bytes %lu cycles\n",
		alltime, size, rangetime);

	/* Racy, but if we see an intermediate value, it's ok too... */
	parisc_cache_flush_threshold = size * alltime / rangetime;

	parisc_cache_flush_threshold = L1_CACHE_ALIGN(parisc_cache_flush_threshold);
	if (!parisc_cache_flush_threshold)
		parisc_cache_flush_threshold = FLUSH_THRESHOLD;

	if (parisc_cache_flush_threshold > cache_info.dc_size)
		parisc_cache_flush_threshold = cache_info.dc_size;

	printk(KERN_INFO "Setting cache flush threshold to %lu kB\n",
		parisc_cache_flush_threshold/1024);

	/* calculate TLB flush threshold */

	alltime = mfctl(16);
	flush_tlb_all();
	alltime = mfctl(16) - alltime;

	size = PAGE_SIZE;
	start = (unsigned long) _text;
	rangetime = mfctl(16);
	while (start < (unsigned long) _end) {
		flush_tlb_kernel_range(start, start + PAGE_SIZE);
		start += PAGE_SIZE;
		size += PAGE_SIZE;
	}
	rangetime = mfctl(16) - rangetime;

	printk(KERN_DEBUG "Whole TLB flush %lu cycles, flushing %lu bytes %lu cycles\n",
		alltime, size, rangetime);

	parisc_tlb_flush_threshold = size * alltime / rangetime;
	parisc_tlb_flush_threshold *= num_online_cpus();
	parisc_tlb_flush_threshold = PAGE_ALIGN(parisc_tlb_flush_threshold);
	if (!parisc_tlb_flush_threshold)
		parisc_tlb_flush_threshold = FLUSH_TLB_THRESHOLD;

	printk(KERN_INFO "Setting TLB flush threshold to %lu kB\n",
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
	preempt_enable();
	copy_page_asm(vto, vfrom);
}
EXPORT_SYMBOL(copy_user_page);

/* __flush_tlb_range()
 *
 * returns 1 if all TLBs were flushed.
 */
int __flush_tlb_range(unsigned long sid, unsigned long start,
		      unsigned long end)
{
	unsigned long flags, size;

	size = (end - start);
	if (size >= parisc_tlb_flush_threshold) {
		flush_tlb_all();
		return 1;
	}

	/* Purge TLB entries for small ranges using the pdtlb and
	   pitlb instructions.  These instructions execute locally
	   but cause a purge request to be broadcast to other TLBs.  */
	if (likely(!split_tlb)) {
		while (start < end) {
			purge_tlb_start(flags);
			mtsp(sid, 1);
			pdtlb(start);
			purge_tlb_end(flags);
			start += PAGE_SIZE;
		}
		return 0;
	}

	/* split TLB case */
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
		pud_t *pud = pud_offset(pgd, addr);
		if (!pud_none(*pud)) {
			pmd_t *pmd = pmd_offset(pud, addr);
			if (!pmd_none(*pmd))
				ptep = pte_offset_map(pmd, addr);
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
	if (mm_total_size(mm) >= parisc_cache_flush_threshold) {
		flush_cache_all();
		return;
	}

	if (mm->context == mfsp(3)) {
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			flush_user_dcache_range_asm(vma->vm_start, vma->vm_end);
			if ((vma->vm_flags & VM_EXEC) == 0)
				continue;
			flush_user_icache_range_asm(vma->vm_start, vma->vm_end);
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
			__flush_cache_page(vma, addr, PFN_PHYS(pfn));
		}
	}
}

void
flush_user_dcache_range(unsigned long start, unsigned long end)
{
	if ((end - start) < parisc_cache_flush_threshold)
		flush_user_dcache_range_asm(start,end);
	else
		flush_data_cache();
}

void
flush_user_icache_range(unsigned long start, unsigned long end)
{
	if ((end - start) < parisc_cache_flush_threshold)
		flush_user_icache_range_asm(start,end);
	else
		flush_instruction_cache();
}

void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	unsigned long addr;
	pgd_t *pgd;

	BUG_ON(!vma->vm_mm->context);

	if ((end - start) >= parisc_cache_flush_threshold) {
		flush_cache_all();
		return;
	}

	if (vma->vm_mm->context == mfsp(3)) {
		flush_user_dcache_range_asm(start, end);
		if (vma->vm_flags & VM_EXEC)
			flush_user_icache_range_asm(start, end);
		return;
	}

	pgd = vma->vm_mm->pgd;
	for (addr = start & PAGE_MASK; addr < end; addr += PAGE_SIZE) {
		unsigned long pfn;
		pte_t *ptep = get_ptep(pgd, addr);
		if (!ptep)
			continue;
		pfn = pte_pfn(*ptep);
		if (pfn_valid(pfn))
			__flush_cache_page(vma, addr, PFN_PHYS(pfn));
	}
}

void
flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr, unsigned long pfn)
{
	BUG_ON(!vma->vm_mm->context);

	if (pfn_valid(pfn)) {
		flush_tlb_page(vma, vmaddr);
		__flush_cache_page(vma, vmaddr, PFN_PHYS(pfn));
	}
}
