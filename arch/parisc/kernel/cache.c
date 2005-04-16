/* $Id: cache.c,v 1.4 2000/01/25 00:11:38 prumpf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Helge Deller (07-13-1999)
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

#include <asm/pdc.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>

int split_tlb;
int dcache_stride;
int icache_stride;
EXPORT_SYMBOL(dcache_stride);


#if defined(CONFIG_SMP)
/* On some machines (e.g. ones with the Merced bus), there can be
 * only a single PxTLB broadcast at a time; this must be guaranteed
 * by software.  We put a spinlock around all TLB flushes  to
 * ensure this.
 */
DEFINE_SPINLOCK(pa_tlb_lock);
EXPORT_SYMBOL(pa_tlb_lock);
#endif

struct pdc_cache_info cache_info;
#ifndef CONFIG_PA20
static struct pdc_btlb_info btlb_info;
#endif

#ifdef CONFIG_SMP
void
flush_data_cache(void)
{
	on_each_cpu((void (*)(void *))flush_data_cache_local, NULL, 1, 1);
}
void 
flush_instruction_cache(void)
{
	on_each_cpu((void (*)(void *))flush_instruction_cache_local, NULL, 1, 1);
}
#endif

void
flush_cache_all_local(void)
{
	flush_instruction_cache_local();
	flush_data_cache_local();
}
EXPORT_SYMBOL(flush_cache_all_local);

/* flushes EVERYTHING (tlb & cache) */

void
flush_all_caches(void)
{
	flush_cache_all();
	flush_tlb_all();
}
EXPORT_SYMBOL(flush_all_caches);

void
update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	struct page *page = pte_page(pte);

	if (pfn_valid(page_to_pfn(page)) && page_mapping(page) &&
	    test_bit(PG_dcache_dirty, &page->flags)) {

		flush_kernel_dcache_page(page_address(page));
		clear_bit(PG_dcache_dirty, &page->flags);
	}
}

void
show_cache_info(struct seq_file *m)
{
	seq_printf(m, "I-cache\t\t: %ld KB\n", 
		cache_info.ic_size/1024 );
	seq_printf(m, "D-cache\t\t: %ld KB (%s%s, %d-way associative)\n", 
		cache_info.dc_size/1024,
		(cache_info.dc_conf.cc_wt ? "WT":"WB"),
		(cache_info.dc_conf.cc_sh ? ", shared I/D":""),
		(cache_info.dc_conf.cc_assoc)
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
	printk("	wt %d sh %d cst %d assoc %d\n",
		cache_info.dc_conf.cc_wt,
		cache_info.dc_conf.cc_sh,
		cache_info.dc_conf.cc_cst,
		cache_info.dc_conf.cc_assoc);

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
	printk("	wt %d sh %d cst %d assoc %d\n",
		cache_info.ic_conf.cc_wt,
		cache_info.ic_conf.cc_sh,
		cache_info.ic_conf.cc_cst,
		cache_info.ic_conf.cc_assoc);

	printk("D-TLB conf: sh %d page %d cst %d aid %d pad1 %d \n",
		cache_info.dt_conf.tc_sh,
		cache_info.dt_conf.tc_page,
		cache_info.dt_conf.tc_cst,
		cache_info.dt_conf.tc_aid,
		cache_info.dt_conf.tc_pad1);

	printk("I-TLB conf: sh %d page %d cst %d aid %d pad1 %d \n",
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
	int srhash_type;

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
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	struct vm_area_struct *mpnt;
	struct prio_tree_iter iter;
	unsigned long offset;
	unsigned long addr;
	pgoff_t pgoff;
	pte_t *pte;
	unsigned long pfn = page_to_pfn(page);


	if (mapping && !mapping_mapped(mapping)) {
		set_bit(PG_dcache_dirty, &page->flags);
		return;
	}

	flush_kernel_dcache_page(page_address(page));

	if (!mapping)
		return;

	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	/* We have carefully arranged in arch_get_unmapped_area() that
	 * *any* mappings of a file are always congruently mapped (whether
	 * declared as MAP_PRIVATE or MAP_SHARED), so we only need
	 * to flush one address here for them all to become coherent */

	flush_dcache_mmap_lock(mapping);
	vma_prio_tree_foreach(mpnt, &iter, &mapping->i_mmap, pgoff, pgoff) {
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		addr = mpnt->vm_start + offset;

		/* Flush instructions produce non access tlb misses.
		 * On PA, we nullify these instructions rather than
		 * taking a page fault if the pte doesn't exist.
		 * This is just for speed.  If the page translation
		 * isn't there, there's no point exciting the
		 * nadtlb handler into a nullification frenzy */


  		if(!(pte = translation_exists(mpnt, addr)))
			continue;

		/* make sure we really have this page: the private
		 * mappings may cover this area but have COW'd this
		 * particular page */
		if(pte_pfn(*pte) != pfn)
  			continue;

		__flush_cache_page(mpnt, addr);

		break;
	}
	flush_dcache_mmap_unlock(mapping);
}
EXPORT_SYMBOL(flush_dcache_page);

/* Defined in arch/parisc/kernel/pacache.S */
EXPORT_SYMBOL(flush_kernel_dcache_range_asm);
EXPORT_SYMBOL(flush_kernel_dcache_page);
EXPORT_SYMBOL(flush_data_cache_local);
EXPORT_SYMBOL(flush_kernel_icache_range_asm);

void clear_user_page_asm(void *page, unsigned long vaddr)
{
	/* This function is implemented in assembly in pacache.S */
	extern void __clear_user_page_asm(void *page, unsigned long vaddr);

	purge_tlb_start();
	__clear_user_page_asm(page, vaddr);
	purge_tlb_end();
}

#define FLUSH_THRESHOLD 0x80000 /* 0.5MB */
int parisc_cache_flush_threshold = FLUSH_THRESHOLD;

void parisc_setup_cache_timing(void)
{
	unsigned long rangetime, alltime;
	extern char _text;	/* start of kernel code, defined by linker */
	extern char _end;	/* end of BSS, defined by linker */
	unsigned long size;

	alltime = mfctl(16);
	flush_data_cache();
	alltime = mfctl(16) - alltime;

	size = (unsigned long)(&_end - _text);
	rangetime = mfctl(16);
	flush_kernel_dcache_range((unsigned long)&_text, size);
	rangetime = mfctl(16) - rangetime;

	printk(KERN_DEBUG "Whole cache flush %lu cycles, flushing %lu bytes %lu cycles\n",
		alltime, size, rangetime);

	/* Racy, but if we see an intermediate value, it's ok too... */
	parisc_cache_flush_threshold = size * alltime / rangetime;

	parisc_cache_flush_threshold = (parisc_cache_flush_threshold + L1_CACHE_BYTES - 1) &~ (L1_CACHE_BYTES - 1); 
	if (!parisc_cache_flush_threshold)
		parisc_cache_flush_threshold = FLUSH_THRESHOLD;

	printk("Setting cache flush threshold to %x (%d CPUs online)\n", parisc_cache_flush_threshold, num_online_cpus());
}
