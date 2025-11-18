// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/sh/mm/cache.c
 *
 * Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 * Copyright (C) 2002 - 2010  Paul Mundt
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

void (*local_flush_cache_all)(void *args) = cache_noop;
void (*local_flush_cache_mm)(void *args) = cache_noop;
void (*local_flush_cache_dup_mm)(void *args) = cache_noop;
void (*local_flush_cache_page)(void *args) = cache_noop;
void (*local_flush_cache_range)(void *args) = cache_noop;
void (*local_flush_dcache_folio)(void *args) = cache_noop;
void (*local_flush_icache_range)(void *args) = cache_noop;
void (*local_flush_icache_folio)(void *args) = cache_noop;
void (*local_flush_cache_sigtramp)(void *args) = cache_noop;

void (*__flush_wback_region)(void *start, int size);
EXPORT_SYMBOL(__flush_wback_region);
void (*__flush_purge_region)(void *start, int size);
EXPORT_SYMBOL(__flush_purge_region);
void (*__flush_invalidate_region)(void *start, int size);
EXPORT_SYMBOL(__flush_invalidate_region);

static inline void noop__flush_region(void *start, int size)
{
}

static inline void cacheop_on_each_cpu(void (*func) (void *info), void *info,
                                   int wait)
{
	preempt_disable();

	/* Needing IPI for cross-core flush is SHX3-specific. */
#ifdef CONFIG_CPU_SHX3
	/*
	 * It's possible that this gets called early on when IRQs are
	 * still disabled due to ioremapping by the boot CPU, so don't
	 * even attempt IPIs unless there are other CPUs online.
	 */
	if (num_online_cpus() > 1)
		smp_call_function(func, info, wait);
#endif

	func(info);

	preempt_enable();
}

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long vaddr, void *dst, const void *src,
		       unsigned long len)
{
	struct folio *folio = page_folio(page);

	if (boot_cpu_data.dcache.n_aliases && folio_mapped(folio) &&
	    test_bit(PG_dcache_clean, &folio->flags.f)) {
		void *vto = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(vto, src, len);
		kunmap_coherent(vto);
	} else {
		memcpy(dst, src, len);
		if (boot_cpu_data.dcache.n_aliases)
			clear_bit(PG_dcache_clean, &folio->flags.f);
	}

	if (vma->vm_flags & VM_EXEC)
		flush_cache_page(vma, vaddr, page_to_pfn(page));
}

void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
			 unsigned long vaddr, void *dst, const void *src,
			 unsigned long len)
{
	struct folio *folio = page_folio(page);

	if (boot_cpu_data.dcache.n_aliases && folio_mapped(folio) &&
	    test_bit(PG_dcache_clean, &folio->flags.f)) {
		void *vfrom = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(dst, vfrom, len);
		kunmap_coherent(vfrom);
	} else {
		memcpy(dst, src, len);
		if (boot_cpu_data.dcache.n_aliases)
			clear_bit(PG_dcache_clean, &folio->flags.f);
	}
}

void copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma)
{
	struct folio *src = page_folio(from);
	void *vfrom, *vto;

	vto = kmap_atomic(to);

	if (boot_cpu_data.dcache.n_aliases && folio_mapped(src) &&
	    test_bit(PG_dcache_clean, &src->flags.f)) {
		vfrom = kmap_coherent(from, vaddr);
		copy_page(vto, vfrom);
		kunmap_coherent(vfrom);
	} else {
		vfrom = kmap_atomic(from);
		copy_page(vto, vfrom);
		kunmap_atomic(vfrom);
	}

	if (pages_do_alias((unsigned long)vto, vaddr & PAGE_MASK) ||
	    (vma->vm_flags & VM_EXEC))
		__flush_purge_region(vto, PAGE_SIZE);

	kunmap_atomic(vto);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}
EXPORT_SYMBOL(copy_user_highpage);

void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *kaddr = kmap_atomic(page);

	clear_page(kaddr);

	if (pages_do_alias((unsigned long)kaddr, vaddr & PAGE_MASK))
		__flush_purge_region(kaddr, PAGE_SIZE);

	kunmap_atomic(kaddr);
}
EXPORT_SYMBOL(clear_user_highpage);

void __update_cache(struct vm_area_struct *vma,
		    unsigned long address, pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);

	if (!boot_cpu_data.dcache.n_aliases)
		return;

	if (pfn_valid(pfn)) {
		struct folio *folio = page_folio(pfn_to_page(pfn));
		int dirty = !test_and_set_bit(PG_dcache_clean, &folio->flags.f);
		if (dirty)
			__flush_purge_region(folio_address(folio),
						folio_size(folio));
	}
}

void __flush_anon_page(struct page *page, unsigned long vmaddr)
{
	struct folio *folio = page_folio(page);
	unsigned long addr = (unsigned long) page_address(page);

	if (pages_do_alias(addr, vmaddr)) {
		if (boot_cpu_data.dcache.n_aliases && folio_mapped(folio) &&
		    test_bit(PG_dcache_clean, &folio->flags.f)) {
			void *kaddr;

			kaddr = kmap_coherent(page, vmaddr);
			/* XXX.. For now kunmap_coherent() does a purge */
			/* __flush_purge_region((void *)kaddr, PAGE_SIZE); */
			kunmap_coherent(kaddr);
		} else
			__flush_purge_region(folio_address(folio),
						folio_size(folio));
	}
}

void flush_cache_all(void)
{
	cacheop_on_each_cpu(local_flush_cache_all, NULL, 1);
}
EXPORT_SYMBOL(flush_cache_all);

void flush_cache_mm(struct mm_struct *mm)
{
	if (boot_cpu_data.dcache.n_aliases == 0)
		return;

	cacheop_on_each_cpu(local_flush_cache_mm, mm, 1);
}

void flush_cache_dup_mm(struct mm_struct *mm)
{
	if (boot_cpu_data.dcache.n_aliases == 0)
		return;

	cacheop_on_each_cpu(local_flush_cache_dup_mm, mm, 1);
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long addr,
		      unsigned long pfn)
{
	struct flusher_data data;

	data.vma = vma;
	data.addr1 = addr;
	data.addr2 = pfn;

	cacheop_on_each_cpu(local_flush_cache_page, (void *)&data, 1);
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	struct flusher_data data;

	data.vma = vma;
	data.addr1 = start;
	data.addr2 = end;

	cacheop_on_each_cpu(local_flush_cache_range, (void *)&data, 1);
}
EXPORT_SYMBOL(flush_cache_range);

void flush_dcache_folio(struct folio *folio)
{
	cacheop_on_each_cpu(local_flush_dcache_folio, folio, 1);
}
EXPORT_SYMBOL(flush_dcache_folio);

void flush_icache_range(unsigned long start, unsigned long end)
{
	struct flusher_data data;

	data.vma = NULL;
	data.addr1 = start;
	data.addr2 = end;

	cacheop_on_each_cpu(local_flush_icache_range, (void *)&data, 1);
}
EXPORT_SYMBOL(flush_icache_range);

void flush_icache_pages(struct vm_area_struct *vma, struct page *page,
		unsigned int nr)
{
	/* Nothing uses the VMA, so just pass the folio along */
	cacheop_on_each_cpu(local_flush_icache_folio, page_folio(page), 1);
}

void flush_cache_sigtramp(unsigned long address)
{
	cacheop_on_each_cpu(local_flush_cache_sigtramp, (void *)address, 1);
}

static void compute_alias(struct cache_info *c)
{
#ifdef CONFIG_MMU
	c->alias_mask = ((c->sets - 1) << c->entry_shift) & ~(PAGE_SIZE - 1);
#else
	c->alias_mask = 0;
#endif
	c->n_aliases = c->alias_mask ? (c->alias_mask >> PAGE_SHIFT) + 1 : 0;
}

static void __init emit_cache_params(void)
{
	printk(KERN_NOTICE "I-cache : n_ways=%d n_sets=%d way_incr=%d\n",
		boot_cpu_data.icache.ways,
		boot_cpu_data.icache.sets,
		boot_cpu_data.icache.way_incr);
	printk(KERN_NOTICE "I-cache : entry_mask=0x%08x alias_mask=0x%08x n_aliases=%d\n",
		boot_cpu_data.icache.entry_mask,
		boot_cpu_data.icache.alias_mask,
		boot_cpu_data.icache.n_aliases);
	printk(KERN_NOTICE "D-cache : n_ways=%d n_sets=%d way_incr=%d\n",
		boot_cpu_data.dcache.ways,
		boot_cpu_data.dcache.sets,
		boot_cpu_data.dcache.way_incr);
	printk(KERN_NOTICE "D-cache : entry_mask=0x%08x alias_mask=0x%08x n_aliases=%d\n",
		boot_cpu_data.dcache.entry_mask,
		boot_cpu_data.dcache.alias_mask,
		boot_cpu_data.dcache.n_aliases);

	/*
	 * Emit Secondary Cache parameters if the CPU has a probed L2.
	 */
	if (boot_cpu_data.flags & CPU_HAS_L2_CACHE) {
		printk(KERN_NOTICE "S-cache : n_ways=%d n_sets=%d way_incr=%d\n",
			boot_cpu_data.scache.ways,
			boot_cpu_data.scache.sets,
			boot_cpu_data.scache.way_incr);
		printk(KERN_NOTICE "S-cache : entry_mask=0x%08x alias_mask=0x%08x n_aliases=%d\n",
			boot_cpu_data.scache.entry_mask,
			boot_cpu_data.scache.alias_mask,
			boot_cpu_data.scache.n_aliases);
	}
}

void __init cpu_cache_init(void)
{
	unsigned int cache_disabled = 0;

#ifdef SH_CCR
	cache_disabled = !(__raw_readl(SH_CCR) & CCR_CACHE_ENABLE);
#endif

	compute_alias(&boot_cpu_data.icache);
	compute_alias(&boot_cpu_data.dcache);
	compute_alias(&boot_cpu_data.scache);

	__flush_wback_region		= noop__flush_region;
	__flush_purge_region		= noop__flush_region;
	__flush_invalidate_region	= noop__flush_region;

	/*
	 * No flushing is necessary in the disabled cache case so we can
	 * just keep the noop functions in local_flush_..() and __flush_..()
	 */
	if (unlikely(cache_disabled))
		goto skip;

	if (boot_cpu_data.type == CPU_J2) {
		j2_cache_init();
	} else if (boot_cpu_data.family == CPU_FAMILY_SH2) {
		sh2_cache_init();
	}

	if (boot_cpu_data.family == CPU_FAMILY_SH2A) {
		sh2a_cache_init();
	}

	if (boot_cpu_data.family == CPU_FAMILY_SH3) {
		sh3_cache_init();

		if ((boot_cpu_data.type == CPU_SH7705) &&
		    (boot_cpu_data.dcache.sets == 512)) {
			sh7705_cache_init();
		}
	}

	if ((boot_cpu_data.family == CPU_FAMILY_SH4) ||
	    (boot_cpu_data.family == CPU_FAMILY_SH4A) ||
	    (boot_cpu_data.family == CPU_FAMILY_SH4AL_DSP)) {
		sh4_cache_init();

		if ((boot_cpu_data.type == CPU_SH7786) ||
		    (boot_cpu_data.type == CPU_SHX3)) {
			shx3_cache_init();
		}
	}

skip:
	emit_cache_params();
}
