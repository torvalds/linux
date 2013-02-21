/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/bitops.h>

#include <asm/bcache.h>
#include <asm/bootinfo.h>
#include <asm/cache.h>
#include <asm/cacheops.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/r4kcache.h>
#include <asm/sections.h>
#include <asm/mmu_context.h>
#include <asm/war.h>
#include <asm/cacheflush.h> /* for run_uncached() */
#include <asm/traps.h>

/*
 * Special Variant of smp_call_function for use by cache functions:
 *
 *  o No return value
 *  o collapses to normal function call on UP kernels
 *  o collapses to normal function call on systems with a single shared
 *    primary cache.
 *  o doesn't disable interrupts on the local CPU
 */
static inline void r4k_on_each_cpu(void (*func) (void *info), void *info)
{
	preempt_disable();

#if !defined(CONFIG_MIPS_MT_SMP) && !defined(CONFIG_MIPS_MT_SMTC)
	smp_call_function(func, info, 1);
#endif
	func(info);
	preempt_enable();
}

#if defined(CONFIG_MIPS_CMP)
#define cpu_has_safe_index_cacheops 0
#else
#define cpu_has_safe_index_cacheops 1
#endif

/*
 * Must die.
 */
static unsigned long icache_size __read_mostly;
static unsigned long dcache_size __read_mostly;
static unsigned long scache_size __read_mostly;

/*
 * Dummy cache handling routines for machines without boardcaches
 */
static void cache_noop(void) {}

static struct bcache_ops no_sc_ops = {
	.bc_enable = (void *)cache_noop,
	.bc_disable = (void *)cache_noop,
	.bc_wback_inv = (void *)cache_noop,
	.bc_inv = (void *)cache_noop
};

struct bcache_ops *bcops = &no_sc_ops;

#define cpu_is_r4600_v1_x()	((read_c0_prid() & 0xfffffff0) == 0x00002010)
#define cpu_is_r4600_v2_x()	((read_c0_prid() & 0xfffffff0) == 0x00002020)

#define R4600_HIT_CACHEOP_WAR_IMPL					\
do {									\
	if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())		\
		*(volatile unsigned long *)CKSEG1;			\
	if (R4600_V1_HIT_CACHEOP_WAR)					\
		__asm__ __volatile__("nop;nop;nop;nop");		\
} while (0)

static void (*r4k_blast_dcache_page)(unsigned long addr);

static inline void r4k_blast_dcache_page_dc32(unsigned long addr)
{
	R4600_HIT_CACHEOP_WAR_IMPL;
	blast_dcache32_page(addr);
}

static inline void r4k_blast_dcache_page_dc64(unsigned long addr)
{
	R4600_HIT_CACHEOP_WAR_IMPL;
	blast_dcache64_page(addr);
}

static void __cpuinit r4k_blast_dcache_page_setup(void)
{
	unsigned long  dc_lsize = cpu_dcache_line_size();

	if (dc_lsize == 0)
		r4k_blast_dcache_page = (void *)cache_noop;
	else if (dc_lsize == 16)
		r4k_blast_dcache_page = blast_dcache16_page;
	else if (dc_lsize == 32)
		r4k_blast_dcache_page = r4k_blast_dcache_page_dc32;
	else if (dc_lsize == 64)
		r4k_blast_dcache_page = r4k_blast_dcache_page_dc64;
}

static void (* r4k_blast_dcache_page_indexed)(unsigned long addr);

static void __cpuinit r4k_blast_dcache_page_indexed_setup(void)
{
	unsigned long dc_lsize = cpu_dcache_line_size();

	if (dc_lsize == 0)
		r4k_blast_dcache_page_indexed = (void *)cache_noop;
	else if (dc_lsize == 16)
		r4k_blast_dcache_page_indexed = blast_dcache16_page_indexed;
	else if (dc_lsize == 32)
		r4k_blast_dcache_page_indexed = blast_dcache32_page_indexed;
	else if (dc_lsize == 64)
		r4k_blast_dcache_page_indexed = blast_dcache64_page_indexed;
}

static void (* r4k_blast_dcache)(void);

static void __cpuinit r4k_blast_dcache_setup(void)
{
	unsigned long dc_lsize = cpu_dcache_line_size();

	if (dc_lsize == 0)
		r4k_blast_dcache = (void *)cache_noop;
	else if (dc_lsize == 16)
		r4k_blast_dcache = blast_dcache16;
	else if (dc_lsize == 32)
		r4k_blast_dcache = blast_dcache32;
	else if (dc_lsize == 64)
		r4k_blast_dcache = blast_dcache64;
}

/* force code alignment (used for TX49XX_ICACHE_INDEX_INV_WAR) */
#define JUMP_TO_ALIGN(order) \
	__asm__ __volatile__( \
		"b\t1f\n\t" \
		".align\t" #order "\n\t" \
		"1:\n\t" \
		)
#define CACHE32_UNROLL32_ALIGN	JUMP_TO_ALIGN(10) /* 32 * 32 = 1024 */
#define CACHE32_UNROLL32_ALIGN2 JUMP_TO_ALIGN(11)

static inline void blast_r4600_v1_icache32(void)
{
	unsigned long flags;

	local_irq_save(flags);
	blast_icache32();
	local_irq_restore(flags);
}

static inline void tx49_blast_icache32(void)
{
	unsigned long start = INDEX_BASE;
	unsigned long end = start + current_cpu_data.icache.waysize;
	unsigned long ws_inc = 1UL << current_cpu_data.icache.waybit;
	unsigned long ws_end = current_cpu_data.icache.ways <<
			       current_cpu_data.icache.waybit;
	unsigned long ws, addr;

	CACHE32_UNROLL32_ALIGN2;
	/* I'm in even chunk.  blast odd chunks */
	for (ws = 0; ws < ws_end; ws += ws_inc)
		for (addr = start + 0x400; addr < end; addr += 0x400 * 2)
			cache32_unroll32(addr|ws, Index_Invalidate_I);
	CACHE32_UNROLL32_ALIGN;
	/* I'm in odd chunk.  blast even chunks */
	for (ws = 0; ws < ws_end; ws += ws_inc)
		for (addr = start; addr < end; addr += 0x400 * 2)
			cache32_unroll32(addr|ws, Index_Invalidate_I);
}

static inline void blast_icache32_r4600_v1_page_indexed(unsigned long page)
{
	unsigned long flags;

	local_irq_save(flags);
	blast_icache32_page_indexed(page);
	local_irq_restore(flags);
}

static inline void tx49_blast_icache32_page_indexed(unsigned long page)
{
	unsigned long indexmask = current_cpu_data.icache.waysize - 1;
	unsigned long start = INDEX_BASE + (page & indexmask);
	unsigned long end = start + PAGE_SIZE;
	unsigned long ws_inc = 1UL << current_cpu_data.icache.waybit;
	unsigned long ws_end = current_cpu_data.icache.ways <<
			       current_cpu_data.icache.waybit;
	unsigned long ws, addr;

	CACHE32_UNROLL32_ALIGN2;
	/* I'm in even chunk.  blast odd chunks */
	for (ws = 0; ws < ws_end; ws += ws_inc)
		for (addr = start + 0x400; addr < end; addr += 0x400 * 2)
			cache32_unroll32(addr|ws, Index_Invalidate_I);
	CACHE32_UNROLL32_ALIGN;
	/* I'm in odd chunk.  blast even chunks */
	for (ws = 0; ws < ws_end; ws += ws_inc)
		for (addr = start; addr < end; addr += 0x400 * 2)
			cache32_unroll32(addr|ws, Index_Invalidate_I);
}

static void (* r4k_blast_icache_page)(unsigned long addr);

static void __cpuinit r4k_blast_icache_page_setup(void)
{
	unsigned long ic_lsize = cpu_icache_line_size();

	if (ic_lsize == 0)
		r4k_blast_icache_page = (void *)cache_noop;
	else if (ic_lsize == 16)
		r4k_blast_icache_page = blast_icache16_page;
	else if (ic_lsize == 32)
		r4k_blast_icache_page = blast_icache32_page;
	else if (ic_lsize == 64)
		r4k_blast_icache_page = blast_icache64_page;
}


static void (* r4k_blast_icache_page_indexed)(unsigned long addr);

static void __cpuinit r4k_blast_icache_page_indexed_setup(void)
{
	unsigned long ic_lsize = cpu_icache_line_size();

	if (ic_lsize == 0)
		r4k_blast_icache_page_indexed = (void *)cache_noop;
	else if (ic_lsize == 16)
		r4k_blast_icache_page_indexed = blast_icache16_page_indexed;
	else if (ic_lsize == 32) {
		if (R4600_V1_INDEX_ICACHEOP_WAR && cpu_is_r4600_v1_x())
			r4k_blast_icache_page_indexed =
				blast_icache32_r4600_v1_page_indexed;
		else if (TX49XX_ICACHE_INDEX_INV_WAR)
			r4k_blast_icache_page_indexed =
				tx49_blast_icache32_page_indexed;
		else
			r4k_blast_icache_page_indexed =
				blast_icache32_page_indexed;
	} else if (ic_lsize == 64)
		r4k_blast_icache_page_indexed = blast_icache64_page_indexed;
}

static void (* r4k_blast_icache)(void);

static void __cpuinit r4k_blast_icache_setup(void)
{
	unsigned long ic_lsize = cpu_icache_line_size();

	if (ic_lsize == 0)
		r4k_blast_icache = (void *)cache_noop;
	else if (ic_lsize == 16)
		r4k_blast_icache = blast_icache16;
	else if (ic_lsize == 32) {
		if (R4600_V1_INDEX_ICACHEOP_WAR && cpu_is_r4600_v1_x())
			r4k_blast_icache = blast_r4600_v1_icache32;
		else if (TX49XX_ICACHE_INDEX_INV_WAR)
			r4k_blast_icache = tx49_blast_icache32;
		else
			r4k_blast_icache = blast_icache32;
	} else if (ic_lsize == 64)
		r4k_blast_icache = blast_icache64;
}

static void (* r4k_blast_scache_page)(unsigned long addr);

static void __cpuinit r4k_blast_scache_page_setup(void)
{
	unsigned long sc_lsize = cpu_scache_line_size();

	if (scache_size == 0)
		r4k_blast_scache_page = (void *)cache_noop;
	else if (sc_lsize == 16)
		r4k_blast_scache_page = blast_scache16_page;
	else if (sc_lsize == 32)
		r4k_blast_scache_page = blast_scache32_page;
	else if (sc_lsize == 64)
		r4k_blast_scache_page = blast_scache64_page;
	else if (sc_lsize == 128)
		r4k_blast_scache_page = blast_scache128_page;
}

static void (* r4k_blast_scache_page_indexed)(unsigned long addr);

static void __cpuinit r4k_blast_scache_page_indexed_setup(void)
{
	unsigned long sc_lsize = cpu_scache_line_size();

	if (scache_size == 0)
		r4k_blast_scache_page_indexed = (void *)cache_noop;
	else if (sc_lsize == 16)
		r4k_blast_scache_page_indexed = blast_scache16_page_indexed;
	else if (sc_lsize == 32)
		r4k_blast_scache_page_indexed = blast_scache32_page_indexed;
	else if (sc_lsize == 64)
		r4k_blast_scache_page_indexed = blast_scache64_page_indexed;
	else if (sc_lsize == 128)
		r4k_blast_scache_page_indexed = blast_scache128_page_indexed;
}

static void (* r4k_blast_scache)(void);

static void __cpuinit r4k_blast_scache_setup(void)
{
	unsigned long sc_lsize = cpu_scache_line_size();

	if (scache_size == 0)
		r4k_blast_scache = (void *)cache_noop;
	else if (sc_lsize == 16)
		r4k_blast_scache = blast_scache16;
	else if (sc_lsize == 32)
		r4k_blast_scache = blast_scache32;
	else if (sc_lsize == 64)
		r4k_blast_scache = blast_scache64;
	else if (sc_lsize == 128)
		r4k_blast_scache = blast_scache128;
}

static inline void local_r4k___flush_cache_all(void * args)
{
#if defined(CONFIG_CPU_LOONGSON2)
	r4k_blast_scache();
	return;
#endif
	r4k_blast_dcache();
	r4k_blast_icache();

	switch (current_cpu_type()) {
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400SC:
	case CPU_R4400MC:
	case CPU_R10000:
	case CPU_R12000:
	case CPU_R14000:
		r4k_blast_scache();
	}
}

static void r4k___flush_cache_all(void)
{
	r4k_on_each_cpu(local_r4k___flush_cache_all, NULL);
}

static inline int has_valid_asid(const struct mm_struct *mm)
{
#if defined(CONFIG_MIPS_MT_SMP) || defined(CONFIG_MIPS_MT_SMTC)
	int i;

	for_each_online_cpu(i)
		if (cpu_context(i, mm))
			return 1;

	return 0;
#else
	return cpu_context(smp_processor_id(), mm);
#endif
}

static void r4k__flush_cache_vmap(void)
{
	r4k_blast_dcache();
}

static void r4k__flush_cache_vunmap(void)
{
	r4k_blast_dcache();
}

static inline void local_r4k_flush_cache_range(void * args)
{
	struct vm_area_struct *vma = args;
	int exec = vma->vm_flags & VM_EXEC;

	if (!(has_valid_asid(vma->vm_mm)))
		return;

	r4k_blast_dcache();
	if (exec)
		r4k_blast_icache();
}

static void r4k_flush_cache_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end)
{
	int exec = vma->vm_flags & VM_EXEC;

	if (cpu_has_dc_aliases || (exec && !cpu_has_ic_fills_f_dc))
		r4k_on_each_cpu(local_r4k_flush_cache_range, vma);
}

static inline void local_r4k_flush_cache_mm(void * args)
{
	struct mm_struct *mm = args;

	if (!has_valid_asid(mm))
		return;

	/*
	 * Kludge alert.  For obscure reasons R4000SC and R4400SC go nuts if we
	 * only flush the primary caches but R10000 and R12000 behave sane ...
	 * R4000SC and R4400SC indexed S-cache ops also invalidate primary
	 * caches, so we can bail out early.
	 */
	if (current_cpu_type() == CPU_R4000SC ||
	    current_cpu_type() == CPU_R4000MC ||
	    current_cpu_type() == CPU_R4400SC ||
	    current_cpu_type() == CPU_R4400MC) {
		r4k_blast_scache();
		return;
	}

	r4k_blast_dcache();
}

static void r4k_flush_cache_mm(struct mm_struct *mm)
{
	if (!cpu_has_dc_aliases)
		return;

	r4k_on_each_cpu(local_r4k_flush_cache_mm, mm);
}

struct flush_cache_page_args {
	struct vm_area_struct *vma;
	unsigned long addr;
	unsigned long pfn;
};

static inline void local_r4k_flush_cache_page(void *args)
{
	struct flush_cache_page_args *fcp_args = args;
	struct vm_area_struct *vma = fcp_args->vma;
	unsigned long addr = fcp_args->addr;
	struct page *page = pfn_to_page(fcp_args->pfn);
	int exec = vma->vm_flags & VM_EXEC;
	struct mm_struct *mm = vma->vm_mm;
	int map_coherent = 0;
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	void *vaddr;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (!has_valid_asid(mm))
		return;

	addr &= PAGE_MASK;
	pgdp = pgd_offset(mm, addr);
	pudp = pud_offset(pgdp, addr);
	pmdp = pmd_offset(pudp, addr);
	ptep = pte_offset(pmdp, addr);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_present(*ptep)))
		return;

	if ((mm == current->active_mm) && (pte_val(*ptep) & _PAGE_VALID))
		vaddr = NULL;
	else {
		/*
		 * Use kmap_coherent or kmap_atomic to do flushes for
		 * another ASID than the current one.
		 */
		map_coherent = (cpu_has_dc_aliases &&
				page_mapped(page) && !Page_dcache_dirty(page));
		if (map_coherent)
			vaddr = kmap_coherent(page, addr);
		else
			vaddr = kmap_atomic(page);
		addr = (unsigned long)vaddr;
	}

	if (cpu_has_dc_aliases || (exec && !cpu_has_ic_fills_f_dc)) {
		r4k_blast_dcache_page(addr);
		if (exec && !cpu_icache_snoops_remote_store)
			r4k_blast_scache_page(addr);
	}
	if (exec) {
		if (vaddr && cpu_has_vtag_icache && mm == current->active_mm) {
			int cpu = smp_processor_id();

			if (cpu_context(cpu, mm) != 0)
				drop_mmu_context(mm, cpu);
		} else
			r4k_blast_icache_page(addr);
	}

	if (vaddr) {
		if (map_coherent)
			kunmap_coherent();
		else
			kunmap_atomic(vaddr);
	}
}

static void r4k_flush_cache_page(struct vm_area_struct *vma,
	unsigned long addr, unsigned long pfn)
{
	struct flush_cache_page_args args;

	args.vma = vma;
	args.addr = addr;
	args.pfn = pfn;

	r4k_on_each_cpu(local_r4k_flush_cache_page, &args);
}

static inline void local_r4k_flush_data_cache_page(void * addr)
{
	r4k_blast_dcache_page((unsigned long) addr);
}

static void r4k_flush_data_cache_page(unsigned long addr)
{
	if (in_atomic())
		local_r4k_flush_data_cache_page((void *)addr);
	else
		r4k_on_each_cpu(local_r4k_flush_data_cache_page, (void *) addr);
}

struct flush_icache_range_args {
	unsigned long start;
	unsigned long end;
};

static inline void local_r4k_flush_icache_range(unsigned long start, unsigned long end)
{
	if (!cpu_has_ic_fills_f_dc) {
		if (end - start >= dcache_size) {
			r4k_blast_dcache();
		} else {
			R4600_HIT_CACHEOP_WAR_IMPL;
			protected_blast_dcache_range(start, end);
		}
	}

	if (end - start > icache_size)
		r4k_blast_icache();
	else
		protected_blast_icache_range(start, end);
}

static inline void local_r4k_flush_icache_range_ipi(void *args)
{
	struct flush_icache_range_args *fir_args = args;
	unsigned long start = fir_args->start;
	unsigned long end = fir_args->end;

	local_r4k_flush_icache_range(start, end);
}

static void r4k_flush_icache_range(unsigned long start, unsigned long end)
{
	struct flush_icache_range_args args;

	args.start = start;
	args.end = end;

	r4k_on_each_cpu(local_r4k_flush_icache_range_ipi, &args);
	instruction_hazard();
}

#ifdef CONFIG_DMA_NONCOHERENT

static void r4k_dma_cache_wback_inv(unsigned long addr, unsigned long size)
{
	/* Catch bad driver code */
	BUG_ON(size == 0);

	if (cpu_has_inclusive_pcaches) {
		if (size >= scache_size)
			r4k_blast_scache();
		else
			blast_scache_range(addr, addr + size);
		__sync();
		return;
	}

	/*
	 * Either no secondary cache or the available caches don't have the
	 * subset property so we have to flush the primary caches
	 * explicitly
	 */
	if (cpu_has_safe_index_cacheops && size >= dcache_size) {
		r4k_blast_dcache();
	} else {
		R4600_HIT_CACHEOP_WAR_IMPL;
		blast_dcache_range(addr, addr + size);
	}

	bc_wback_inv(addr, size);
	__sync();
}

static void r4k_dma_cache_inv(unsigned long addr, unsigned long size)
{
	/* Catch bad driver code */
	BUG_ON(size == 0);

	if (cpu_has_inclusive_pcaches) {
		if (size >= scache_size)
			r4k_blast_scache();
		else {
			/*
			 * There is no clearly documented alignment requirement
			 * for the cache instruction on MIPS processors and
			 * some processors, among them the RM5200 and RM7000
			 * QED processors will throw an address error for cache
			 * hit ops with insufficient alignment.	 Solved by
			 * aligning the address to cache line size.
			 */
			blast_inv_scache_range(addr, addr + size);
		}
		__sync();
		return;
	}

	if (cpu_has_safe_index_cacheops && size >= dcache_size) {
		r4k_blast_dcache();
	} else {
		R4600_HIT_CACHEOP_WAR_IMPL;
		blast_inv_dcache_range(addr, addr + size);
	}

	bc_inv(addr, size);
	__sync();
}
#endif /* CONFIG_DMA_NONCOHERENT */

/*
 * While we're protected against bad userland addresses we don't care
 * very much about what happens in that case.  Usually a segmentation
 * fault will dump the process later on anyway ...
 */
static void local_r4k_flush_cache_sigtramp(void * arg)
{
	unsigned long ic_lsize = cpu_icache_line_size();
	unsigned long dc_lsize = cpu_dcache_line_size();
	unsigned long sc_lsize = cpu_scache_line_size();
	unsigned long addr = (unsigned long) arg;

	R4600_HIT_CACHEOP_WAR_IMPL;
	if (dc_lsize)
		protected_writeback_dcache_line(addr & ~(dc_lsize - 1));
	if (!cpu_icache_snoops_remote_store && scache_size)
		protected_writeback_scache_line(addr & ~(sc_lsize - 1));
	if (ic_lsize)
		protected_flush_icache_line(addr & ~(ic_lsize - 1));
	if (MIPS4K_ICACHE_REFILL_WAR) {
		__asm__ __volatile__ (
			".set push\n\t"
			".set noat\n\t"
			".set mips3\n\t"
#ifdef CONFIG_32BIT
			"la	$at,1f\n\t"
#endif
#ifdef CONFIG_64BIT
			"dla	$at,1f\n\t"
#endif
			"cache	%0,($at)\n\t"
			"nop; nop; nop\n"
			"1:\n\t"
			".set pop"
			:
			: "i" (Hit_Invalidate_I));
	}
	if (MIPS_CACHE_SYNC_WAR)
		__asm__ __volatile__ ("sync");
}

static void r4k_flush_cache_sigtramp(unsigned long addr)
{
	r4k_on_each_cpu(local_r4k_flush_cache_sigtramp, (void *) addr);
}

static void r4k_flush_icache_all(void)
{
	if (cpu_has_vtag_icache)
		r4k_blast_icache();
}

struct flush_kernel_vmap_range_args {
	unsigned long	vaddr;
	int		size;
};

static inline void local_r4k_flush_kernel_vmap_range(void *args)
{
	struct flush_kernel_vmap_range_args *vmra = args;
	unsigned long vaddr = vmra->vaddr;
	int size = vmra->size;

	/*
	 * Aliases only affect the primary caches so don't bother with
	 * S-caches or T-caches.
	 */
	if (cpu_has_safe_index_cacheops && size >= dcache_size)
		r4k_blast_dcache();
	else {
		R4600_HIT_CACHEOP_WAR_IMPL;
		blast_dcache_range(vaddr, vaddr + size);
	}
}

static void r4k_flush_kernel_vmap_range(unsigned long vaddr, int size)
{
	struct flush_kernel_vmap_range_args args;

	args.vaddr = (unsigned long) vaddr;
	args.size = size;

	r4k_on_each_cpu(local_r4k_flush_kernel_vmap_range, &args);
}

static inline void rm7k_erratum31(void)
{
	const unsigned long ic_lsize = 32;
	unsigned long addr;

	/* RM7000 erratum #31. The icache is screwed at startup. */
	write_c0_taglo(0);
	write_c0_taghi(0);

	for (addr = INDEX_BASE; addr <= INDEX_BASE + 4096; addr += ic_lsize) {
		__asm__ __volatile__ (
			".set push\n\t"
			".set noreorder\n\t"
			".set mips3\n\t"
			"cache\t%1, 0(%0)\n\t"
			"cache\t%1, 0x1000(%0)\n\t"
			"cache\t%1, 0x2000(%0)\n\t"
			"cache\t%1, 0x3000(%0)\n\t"
			"cache\t%2, 0(%0)\n\t"
			"cache\t%2, 0x1000(%0)\n\t"
			"cache\t%2, 0x2000(%0)\n\t"
			"cache\t%2, 0x3000(%0)\n\t"
			"cache\t%1, 0(%0)\n\t"
			"cache\t%1, 0x1000(%0)\n\t"
			"cache\t%1, 0x2000(%0)\n\t"
			"cache\t%1, 0x3000(%0)\n\t"
			".set pop\n"
			:
			: "r" (addr), "i" (Index_Store_Tag_I), "i" (Fill));
	}
}

static inline void alias_74k_erratum(struct cpuinfo_mips *c)
{
	/*
	 * Early versions of the 74K do not update the cache tags on a
	 * vtag miss/ptag hit which can occur in the case of KSEG0/KUSEG
	 * aliases. In this case it is better to treat the cache as always
	 * having aliases.
	 */
	if ((c->processor_id & 0xff) <= PRID_REV_ENCODE_332(2, 4, 0))
		c->dcache.flags |= MIPS_CACHE_VTAG;
	if ((c->processor_id & 0xff) == PRID_REV_ENCODE_332(2, 4, 0))
		write_c0_config6(read_c0_config6() | MIPS_CONF6_SYND);
	if (((c->processor_id & 0xff00) == PRID_IMP_1074K) &&
	    ((c->processor_id & 0xff) <= PRID_REV_ENCODE_332(1, 1, 0))) {
		c->dcache.flags |= MIPS_CACHE_VTAG;
		write_c0_config6(read_c0_config6() | MIPS_CONF6_SYND);
	}
}

static char *way_string[] __cpuinitdata = { NULL, "direct mapped", "2-way",
	"3-way", "4-way", "5-way", "6-way", "7-way", "8-way"
};

static void __cpuinit probe_pcache(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int config = read_c0_config();
	unsigned int prid = read_c0_prid();
	unsigned long config1;
	unsigned int lsize;

	switch (c->cputype) {
	case CPU_R4600:			/* QED style two way caches? */
	case CPU_R4700:
	case CPU_R5000:
	case CPU_NEVADA:
		icache_size = 1 << (12 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 2;
		c->icache.waybit = __ffs(icache_size/2);

		dcache_size = 1 << (12 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 2;
		c->dcache.waybit= __ffs(dcache_size/2);

		c->options |= MIPS_CPU_CACHE_CDEX_P;
		break;

	case CPU_R5432:
	case CPU_R5500:
		icache_size = 1 << (12 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 2;
		c->icache.waybit= 0;

		dcache_size = 1 << (12 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 2;
		c->dcache.waybit = 0;

		c->options |= MIPS_CPU_CACHE_CDEX_P | MIPS_CPU_PREFETCH;
		break;

	case CPU_TX49XX:
		icache_size = 1 << (12 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 4;
		c->icache.waybit= 0;

		dcache_size = 1 << (12 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 4;
		c->dcache.waybit = 0;

		c->options |= MIPS_CPU_CACHE_CDEX_P;
		c->options |= MIPS_CPU_PREFETCH;
		break;

	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
	case CPU_R4300:
		icache_size = 1 << (12 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 1;
		c->icache.waybit = 0;	/* doesn't matter */

		dcache_size = 1 << (12 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 1;
		c->dcache.waybit = 0;	/* does not matter */

		c->options |= MIPS_CPU_CACHE_CDEX_P;
		break;

	case CPU_R10000:
	case CPU_R12000:
	case CPU_R14000:
		icache_size = 1 << (12 + ((config & R10K_CONF_IC) >> 29));
		c->icache.linesz = 64;
		c->icache.ways = 2;
		c->icache.waybit = 0;

		dcache_size = 1 << (12 + ((config & R10K_CONF_DC) >> 26));
		c->dcache.linesz = 32;
		c->dcache.ways = 2;
		c->dcache.waybit = 0;

		c->options |= MIPS_CPU_PREFETCH;
		break;

	case CPU_VR4133:
		write_c0_config(config & ~VR41_CONF_P4K);
	case CPU_VR4131:
		/* Workaround for cache instruction bug of VR4131 */
		if (c->processor_id == 0x0c80U || c->processor_id == 0x0c81U ||
		    c->processor_id == 0x0c82U) {
			config |= 0x00400000U;
			if (c->processor_id == 0x0c80U)
				config |= VR41_CONF_BP;
			write_c0_config(config);
		} else
			c->options |= MIPS_CPU_CACHE_CDEX_P;

		icache_size = 1 << (10 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 2;
		c->icache.waybit = __ffs(icache_size/2);

		dcache_size = 1 << (10 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 2;
		c->dcache.waybit = __ffs(dcache_size/2);
		break;

	case CPU_VR41XX:
	case CPU_VR4111:
	case CPU_VR4121:
	case CPU_VR4122:
	case CPU_VR4181:
	case CPU_VR4181A:
		icache_size = 1 << (10 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 1;
		c->icache.waybit = 0;	/* doesn't matter */

		dcache_size = 1 << (10 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 1;
		c->dcache.waybit = 0;	/* does not matter */

		c->options |= MIPS_CPU_CACHE_CDEX_P;
		break;

	case CPU_RM7000:
		rm7k_erratum31();

		icache_size = 1 << (12 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		c->icache.ways = 4;
		c->icache.waybit = __ffs(icache_size / c->icache.ways);

		dcache_size = 1 << (12 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		c->dcache.ways = 4;
		c->dcache.waybit = __ffs(dcache_size / c->dcache.ways);

		c->options |= MIPS_CPU_CACHE_CDEX_P;
		c->options |= MIPS_CPU_PREFETCH;
		break;

	case CPU_LOONGSON2:
		icache_size = 1 << (12 + ((config & CONF_IC) >> 9));
		c->icache.linesz = 16 << ((config & CONF_IB) >> 5);
		if (prid & 0x3)
			c->icache.ways = 4;
		else
			c->icache.ways = 2;
		c->icache.waybit = 0;

		dcache_size = 1 << (12 + ((config & CONF_DC) >> 6));
		c->dcache.linesz = 16 << ((config & CONF_DB) >> 4);
		if (prid & 0x3)
			c->dcache.ways = 4;
		else
			c->dcache.ways = 2;
		c->dcache.waybit = 0;
		break;

	default:
		if (!(config & MIPS_CONF_M))
			panic("Don't know how to probe P-caches on this cpu.");

		/*
		 * So we seem to be a MIPS32 or MIPS64 CPU
		 * So let's probe the I-cache ...
		 */
		config1 = read_c0_config1();

		if ((lsize = ((config1 >> 19) & 7)))
			c->icache.linesz = 2 << lsize;
		else
			c->icache.linesz = lsize;
		c->icache.sets = 32 << (((config1 >> 22) + 1) & 7);
		c->icache.ways = 1 + ((config1 >> 16) & 7);

		icache_size = c->icache.sets *
			      c->icache.ways *
			      c->icache.linesz;
		c->icache.waybit = __ffs(icache_size/c->icache.ways);

		if (config & 0x8)		/* VI bit */
			c->icache.flags |= MIPS_CACHE_VTAG;

		/*
		 * Now probe the MIPS32 / MIPS64 data cache.
		 */
		c->dcache.flags = 0;

		if ((lsize = ((config1 >> 10) & 7)))
			c->dcache.linesz = 2 << lsize;
		else
			c->dcache.linesz= lsize;
		c->dcache.sets = 32 << (((config1 >> 13) + 1) & 7);
		c->dcache.ways = 1 + ((config1 >> 7) & 7);

		dcache_size = c->dcache.sets *
			      c->dcache.ways *
			      c->dcache.linesz;
		c->dcache.waybit = __ffs(dcache_size/c->dcache.ways);

		c->options |= MIPS_CPU_PREFETCH;
		break;
	}

	/*
	 * Processor configuration sanity check for the R4000SC erratum
	 * #5.	With page sizes larger than 32kB there is no possibility
	 * to get a VCE exception anymore so we don't care about this
	 * misconfiguration.  The case is rather theoretical anyway;
	 * presumably no vendor is shipping his hardware in the "bad"
	 * configuration.
	 */
	if ((prid & 0xff00) == PRID_IMP_R4000 && (prid & 0xff) < 0x40 &&
	    !(config & CONF_SC) && c->icache.linesz != 16 &&
	    PAGE_SIZE <= 0x8000)
		panic("Improper R4000SC processor configuration detected");

	/* compute a couple of other cache variables */
	c->icache.waysize = icache_size / c->icache.ways;
	c->dcache.waysize = dcache_size / c->dcache.ways;

	c->icache.sets = c->icache.linesz ?
		icache_size / (c->icache.linesz * c->icache.ways) : 0;
	c->dcache.sets = c->dcache.linesz ?
		dcache_size / (c->dcache.linesz * c->dcache.ways) : 0;

	/*
	 * R10000 and R12000 P-caches are odd in a positive way.  They're 32kB
	 * 2-way virtually indexed so normally would suffer from aliases.  So
	 * normally they'd suffer from aliases but magic in the hardware deals
	 * with that for us so we don't need to take care ourselves.
	 */
	switch (c->cputype) {
	case CPU_20KC:
	case CPU_25KF:
	case CPU_SB1:
	case CPU_SB1A:
	case CPU_XLR:
		c->dcache.flags |= MIPS_CACHE_PINDEX;
		break;

	case CPU_R10000:
	case CPU_R12000:
	case CPU_R14000:
		break;

	case CPU_M14KC:
	case CPU_M14KEC:
	case CPU_24K:
	case CPU_34K:
	case CPU_74K:
	case CPU_1004K:
		if (c->cputype == CPU_74K)
			alias_74k_erratum(c);
		if ((read_c0_config7() & (1 << 16))) {
			/* effectively physically indexed dcache,
			   thus no virtual aliases. */
			c->dcache.flags |= MIPS_CACHE_PINDEX;
			break;
		}
	default:
		if (c->dcache.waysize > PAGE_SIZE)
			c->dcache.flags |= MIPS_CACHE_ALIASES;
	}

	switch (c->cputype) {
	case CPU_20KC:
		/*
		 * Some older 20Kc chips doesn't have the 'VI' bit in
		 * the config register.
		 */
		c->icache.flags |= MIPS_CACHE_VTAG;
		break;

	case CPU_ALCHEMY:
		c->icache.flags |= MIPS_CACHE_IC_F_DC;
		break;
	}

#ifdef	CONFIG_CPU_LOONGSON2
	/*
	 * LOONGSON2 has 4 way icache, but when using indexed cache op,
	 * one op will act on all 4 ways
	 */
	c->icache.ways = 1;
#endif

	printk("Primary instruction cache %ldkB, %s, %s, linesize %d bytes.\n",
	       icache_size >> 10,
	       c->icache.flags & MIPS_CACHE_VTAG ? "VIVT" : "VIPT",
	       way_string[c->icache.ways], c->icache.linesz);

	printk("Primary data cache %ldkB, %s, %s, %s, linesize %d bytes\n",
	       dcache_size >> 10, way_string[c->dcache.ways],
	       (c->dcache.flags & MIPS_CACHE_PINDEX) ? "PIPT" : "VIPT",
	       (c->dcache.flags & MIPS_CACHE_ALIASES) ?
			"cache aliases" : "no aliases",
	       c->dcache.linesz);
}

/*
 * If you even _breathe_ on this function, look at the gcc output and make sure
 * it does not pop things on and off the stack for the cache sizing loop that
 * executes in KSEG1 space or else you will crash and burn badly.  You have
 * been warned.
 */
static int __cpuinit probe_scache(void)
{
	unsigned long flags, addr, begin, end, pow2;
	unsigned int config = read_c0_config();
	struct cpuinfo_mips *c = &current_cpu_data;

	if (config & CONF_SC)
		return 0;

	begin = (unsigned long) &_stext;
	begin &= ~((4 * 1024 * 1024) - 1);
	end = begin + (4 * 1024 * 1024);

	/*
	 * This is such a bitch, you'd think they would make it easy to do
	 * this.  Away you daemons of stupidity!
	 */
	local_irq_save(flags);

	/* Fill each size-multiple cache line with a valid tag. */
	pow2 = (64 * 1024);
	for (addr = begin; addr < end; addr = (begin + pow2)) {
		unsigned long *p = (unsigned long *) addr;
		__asm__ __volatile__("nop" : : "r" (*p)); /* whee... */
		pow2 <<= 1;
	}

	/* Load first line with zero (therefore invalid) tag. */
	write_c0_taglo(0);
	write_c0_taghi(0);
	__asm__ __volatile__("nop; nop; nop; nop;"); /* avoid the hazard */
	cache_op(Index_Store_Tag_I, begin);
	cache_op(Index_Store_Tag_D, begin);
	cache_op(Index_Store_Tag_SD, begin);

	/* Now search for the wrap around point. */
	pow2 = (128 * 1024);
	for (addr = begin + (128 * 1024); addr < end; addr = begin + pow2) {
		cache_op(Index_Load_Tag_SD, addr);
		__asm__ __volatile__("nop; nop; nop; nop;"); /* hazard... */
		if (!read_c0_taglo())
			break;
		pow2 <<= 1;
	}
	local_irq_restore(flags);
	addr -= begin;

	scache_size = addr;
	c->scache.linesz = 16 << ((config & R4K_CONF_SB) >> 22);
	c->scache.ways = 1;
	c->dcache.waybit = 0;		/* does not matter */

	return 1;
}

#if defined(CONFIG_CPU_LOONGSON2)
static void __init loongson2_sc_init(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	scache_size = 512*1024;
	c->scache.linesz = 32;
	c->scache.ways = 4;
	c->scache.waybit = 0;
	c->scache.waysize = scache_size / (c->scache.ways);
	c->scache.sets = scache_size / (c->scache.linesz * c->scache.ways);
	pr_info("Unified secondary cache %ldkB %s, linesize %d bytes.\n",
	       scache_size >> 10, way_string[c->scache.ways], c->scache.linesz);

	c->options |= MIPS_CPU_INCLUSIVE_CACHES;
}
#endif

extern int r5k_sc_init(void);
extern int rm7k_sc_init(void);
extern int mips_sc_init(void);

static void __cpuinit setup_scache(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int config = read_c0_config();
	int sc_present = 0;

	/*
	 * Do the probing thing on R4000SC and R4400SC processors.  Other
	 * processors don't have a S-cache that would be relevant to the
	 * Linux memory management.
	 */
	switch (c->cputype) {
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		sc_present = run_uncached(probe_scache);
		if (sc_present)
			c->options |= MIPS_CPU_CACHE_CDEX_S;
		break;

	case CPU_R10000:
	case CPU_R12000:
	case CPU_R14000:
		scache_size = 0x80000 << ((config & R10K_CONF_SS) >> 16);
		c->scache.linesz = 64 << ((config >> 13) & 1);
		c->scache.ways = 2;
		c->scache.waybit= 0;
		sc_present = 1;
		break;

	case CPU_R5000:
	case CPU_NEVADA:
#ifdef CONFIG_R5000_CPU_SCACHE
		r5k_sc_init();
#endif
		return;

	case CPU_RM7000:
#ifdef CONFIG_RM7000_CPU_SCACHE
		rm7k_sc_init();
#endif
		return;

#if defined(CONFIG_CPU_LOONGSON2)
	case CPU_LOONGSON2:
		loongson2_sc_init();
		return;
#endif
	case CPU_XLP:
		/* don't need to worry about L2, fully coherent */
		return;

	default:
		if (c->isa_level == MIPS_CPU_ISA_M32R1 ||
		    c->isa_level == MIPS_CPU_ISA_M32R2 ||
		    c->isa_level == MIPS_CPU_ISA_M64R1 ||
		    c->isa_level == MIPS_CPU_ISA_M64R2) {
#ifdef CONFIG_MIPS_CPU_SCACHE
			if (mips_sc_init ()) {
				scache_size = c->scache.ways * c->scache.sets * c->scache.linesz;
				printk("MIPS secondary cache %ldkB, %s, linesize %d bytes.\n",
				       scache_size >> 10,
				       way_string[c->scache.ways], c->scache.linesz);
			}
#else
			if (!(c->scache.flags & MIPS_CACHE_NOT_PRESENT))
				panic("Dunno how to handle MIPS32 / MIPS64 second level cache");
#endif
			return;
		}
		sc_present = 0;
	}

	if (!sc_present)
		return;

	/* compute a couple of other cache variables */
	c->scache.waysize = scache_size / c->scache.ways;

	c->scache.sets = scache_size / (c->scache.linesz * c->scache.ways);

	printk("Unified secondary cache %ldkB %s, linesize %d bytes.\n",
	       scache_size >> 10, way_string[c->scache.ways], c->scache.linesz);

	c->options |= MIPS_CPU_INCLUSIVE_CACHES;
}

void au1x00_fixup_config_od(void)
{
	/*
	 * c0_config.od (bit 19) was write only (and read as 0)
	 * on the early revisions of Alchemy SOCs.  It disables the bus
	 * transaction overlapping and needs to be set to fix various errata.
	 */
	switch (read_c0_prid()) {
	case 0x00030100: /* Au1000 DA */
	case 0x00030201: /* Au1000 HA */
	case 0x00030202: /* Au1000 HB */
	case 0x01030200: /* Au1500 AB */
	/*
	 * Au1100 errata actually keeps silence about this bit, so we set it
	 * just in case for those revisions that require it to be set according
	 * to the (now gone) cpu table.
	 */
	case 0x02030200: /* Au1100 AB */
	case 0x02030201: /* Au1100 BA */
	case 0x02030202: /* Au1100 BC */
		set_c0_config(1 << 19);
		break;
	}
}

/* CP0 hazard avoidance. */
#define NXP_BARRIER()							\
	 __asm__ __volatile__(						\
	".set noreorder\n\t"						\
	"nop; nop; nop; nop; nop; nop;\n\t"				\
	".set reorder\n\t")

static void nxp_pr4450_fixup_config(void)
{
	unsigned long config0;

	config0 = read_c0_config();

	/* clear all three cache coherency fields */
	config0 &= ~(0x7 | (7 << 25) | (7 << 28));
	config0 |= (((_page_cachable_default >> _CACHE_SHIFT) <<  0) |
		    ((_page_cachable_default >> _CACHE_SHIFT) << 25) |
		    ((_page_cachable_default >> _CACHE_SHIFT) << 28));
	write_c0_config(config0);
	NXP_BARRIER();
}

static int __cpuinitdata cca = -1;

static int __init cca_setup(char *str)
{
	get_option(&str, &cca);

	return 0;
}

early_param("cca", cca_setup);

static void __cpuinit coherency_setup(void)
{
	if (cca < 0 || cca > 7)
		cca = read_c0_config() & CONF_CM_CMASK;
	_page_cachable_default = cca << _CACHE_SHIFT;

	pr_debug("Using cache attribute %d\n", cca);
	change_c0_config(CONF_CM_CMASK, cca);

	/*
	 * c0_status.cu=0 specifies that updates by the sc instruction use
	 * the coherency mode specified by the TLB; 1 means cachable
	 * coherent update on write will be used.  Not all processors have
	 * this bit and; some wire it to zero, others like Toshiba had the
	 * silly idea of putting something else there ...
	 */
	switch (current_cpu_type()) {
	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		clear_c0_config(CONF_CU);
		break;
	/*
	 * We need to catch the early Alchemy SOCs with
	 * the write-only co_config.od bit and set it back to one on:
	 * Au1000 rev DA, HA, HB;  Au1100 AB, BA, BC, Au1500 AB
	 */
	case CPU_ALCHEMY:
		au1x00_fixup_config_od();
		break;

	case PRID_IMP_PR4450:
		nxp_pr4450_fixup_config();
		break;
	}
}

#if defined(CONFIG_DMA_NONCOHERENT)

static int __cpuinitdata coherentio;

static int __init setcoherentio(char *str)
{
	coherentio = 1;

	return 0;
}

early_param("coherentio", setcoherentio);
#endif

static void __cpuinit r4k_cache_error_setup(void)
{
	extern char __weak except_vec2_generic;
	extern char __weak except_vec2_sb1;
	struct cpuinfo_mips *c = &current_cpu_data;

	switch (c->cputype) {
	case CPU_SB1:
	case CPU_SB1A:
		set_uncached_handler(0x100, &except_vec2_sb1, 0x80);
		break;

	default:
		set_uncached_handler(0x100, &except_vec2_generic, 0x80);
		break;
	}
}

void __cpuinit r4k_cache_init(void)
{
	extern void build_clear_page(void);
	extern void build_copy_page(void);
	struct cpuinfo_mips *c = &current_cpu_data;

	probe_pcache();
	setup_scache();

	r4k_blast_dcache_page_setup();
	r4k_blast_dcache_page_indexed_setup();
	r4k_blast_dcache_setup();
	r4k_blast_icache_page_setup();
	r4k_blast_icache_page_indexed_setup();
	r4k_blast_icache_setup();
	r4k_blast_scache_page_setup();
	r4k_blast_scache_page_indexed_setup();
	r4k_blast_scache_setup();

	/*
	 * Some MIPS32 and MIPS64 processors have physically indexed caches.
	 * This code supports virtually indexed processors and will be
	 * unnecessarily inefficient on physically indexed processors.
	 */
	if (c->dcache.linesz)
		shm_align_mask = max_t( unsigned long,
					c->dcache.sets * c->dcache.linesz - 1,
					PAGE_SIZE - 1);
	else
		shm_align_mask = PAGE_SIZE-1;

	__flush_cache_vmap	= r4k__flush_cache_vmap;
	__flush_cache_vunmap	= r4k__flush_cache_vunmap;

	flush_cache_all		= cache_noop;
	__flush_cache_all	= r4k___flush_cache_all;
	flush_cache_mm		= r4k_flush_cache_mm;
	flush_cache_page	= r4k_flush_cache_page;
	flush_cache_range	= r4k_flush_cache_range;

	__flush_kernel_vmap_range = r4k_flush_kernel_vmap_range;

	flush_cache_sigtramp	= r4k_flush_cache_sigtramp;
	flush_icache_all	= r4k_flush_icache_all;
	local_flush_data_cache_page	= local_r4k_flush_data_cache_page;
	flush_data_cache_page	= r4k_flush_data_cache_page;
	flush_icache_range	= r4k_flush_icache_range;
	local_flush_icache_range	= local_r4k_flush_icache_range;

#if defined(CONFIG_DMA_NONCOHERENT)
	if (coherentio) {
		_dma_cache_wback_inv	= (void *)cache_noop;
		_dma_cache_wback	= (void *)cache_noop;
		_dma_cache_inv		= (void *)cache_noop;
	} else {
		_dma_cache_wback_inv	= r4k_dma_cache_wback_inv;
		_dma_cache_wback	= r4k_dma_cache_wback_inv;
		_dma_cache_inv		= r4k_dma_cache_inv;
	}
#endif

	build_clear_page();
	build_copy_page();
#if !defined(CONFIG_MIPS_CMP)
	local_r4k___flush_cache_all(NULL);
#endif
	coherency_setup();
	board_cache_error_setup = r4k_cache_error_setup;
}
