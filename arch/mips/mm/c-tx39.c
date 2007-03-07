/*
 * r2300.c: R2000 and R3000 specific mmu/cache code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * with a lot of changes to make this thing work for R3000s
 * Tx39XX R4k style caches added. HK
 * Copyright (C) 1998, 1999, 2000 Harald Koerfgen
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/cacheops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/isadep.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

/* For R3000 cores with R4000 style caches */
static unsigned long icache_size, dcache_size;		/* Size in bytes */

#include <asm/r4kcache.h>

extern int r3k_have_wired_reg;	/* in r3k-tlb.c */

/* This sequence is required to ensure icache is disabled immediately */
#define TX39_STOP_STREAMING() \
__asm__ __volatile__( \
	".set    push\n\t" \
	".set    noreorder\n\t" \
	"b       1f\n\t" \
	"nop\n\t" \
	"1:\n\t" \
	".set pop" \
	)

/* TX39H-style cache flush routines. */
static void tx39h_flush_icache_all(void)
{
	unsigned long flags, config;

	/* disable icache (set ICE#) */
	local_irq_save(flags);
	config = read_c0_conf();
	write_c0_conf(config & ~TX39_CONF_ICE);
	TX39_STOP_STREAMING();
	blast_icache16();
	write_c0_conf(config);
	local_irq_restore(flags);
}

static void tx39h_dma_cache_wback_inv(unsigned long addr, unsigned long size)
{
	/* Catch bad driver code */
	BUG_ON(size == 0);

	iob();
	blast_inv_dcache_range(addr, addr + size);
}


/* TX39H2,TX39H3 */
static inline void tx39_blast_dcache_page(unsigned long addr)
{
	if (current_cpu_data.cputype != CPU_TX3912)
		blast_dcache16_page(addr);
}

static inline void tx39_blast_dcache_page_indexed(unsigned long addr)
{
	blast_dcache16_page_indexed(addr);
}

static inline void tx39_blast_dcache(void)
{
	blast_dcache16();
}

static inline void tx39_blast_icache_page(unsigned long addr)
{
	unsigned long flags, config;
	/* disable icache (set ICE#) */
	local_irq_save(flags);
	config = read_c0_conf();
	write_c0_conf(config & ~TX39_CONF_ICE);
	TX39_STOP_STREAMING();
	blast_icache16_page(addr);
	write_c0_conf(config);
	local_irq_restore(flags);
}

static inline void tx39_blast_icache_page_indexed(unsigned long addr)
{
	unsigned long flags, config;
	/* disable icache (set ICE#) */
	local_irq_save(flags);
	config = read_c0_conf();
	write_c0_conf(config & ~TX39_CONF_ICE);
	TX39_STOP_STREAMING();
	blast_icache16_page_indexed(addr);
	write_c0_conf(config);
	local_irq_restore(flags);
}

static inline void tx39_blast_icache(void)
{
	unsigned long flags, config;
	/* disable icache (set ICE#) */
	local_irq_save(flags);
	config = read_c0_conf();
	write_c0_conf(config & ~TX39_CONF_ICE);
	TX39_STOP_STREAMING();
	blast_icache16();
	write_c0_conf(config);
	local_irq_restore(flags);
}

static inline void tx39_flush_cache_all(void)
{
	if (!cpu_has_dc_aliases)
		return;

	tx39_blast_dcache();
}

static inline void tx39___flush_cache_all(void)
{
	tx39_blast_dcache();
	tx39_blast_icache();
}

static void tx39_flush_cache_mm(struct mm_struct *mm)
{
	if (!cpu_has_dc_aliases)
		return;

	if (cpu_context(smp_processor_id(), mm) != 0)
		tx39_blast_dcache();
}

static void tx39_flush_cache_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end)
{
	if (!cpu_has_dc_aliases)
		return;
	if (!(cpu_context(smp_processor_id(), vma->vm_mm)))
		return;

	tx39_blast_dcache();
}

static void tx39_flush_cache_page(struct vm_area_struct *vma, unsigned long page, unsigned long pfn)
{
	int exec = vma->vm_flags & VM_EXEC;
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (cpu_context(smp_processor_id(), mm) == 0)
		return;

	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pudp = pud_offset(pgdp, page);
	pmdp = pmd_offset(pudp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_PRESENT))
		return;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if ((mm == current->active_mm) && (pte_val(*ptep) & _PAGE_VALID)) {
		if (cpu_has_dc_aliases || exec)
			tx39_blast_dcache_page(page);
		if (exec)
			tx39_blast_icache_page(page);

		return;
	}

	/*
	 * Do indexed flush, too much work to get the (possible) TLB refills
	 * to work correctly.
	 */
	if (cpu_has_dc_aliases || exec)
		tx39_blast_dcache_page_indexed(page);
	if (exec)
		tx39_blast_icache_page_indexed(page);
}

static void local_tx39_flush_data_cache_page(void * addr)
{
	tx39_blast_dcache_page((unsigned long)addr);
}

static void tx39_flush_data_cache_page(unsigned long addr)
{
	tx39_blast_dcache_page(addr);
}

static void tx39_flush_icache_range(unsigned long start, unsigned long end)
{
	if (end - start > dcache_size)
		tx39_blast_dcache();
	else
		protected_blast_dcache_range(start, end);

	if (end - start > icache_size)
		tx39_blast_icache();
	else {
		unsigned long flags, config;
		/* disable icache (set ICE#) */
		local_irq_save(flags);
		config = read_c0_conf();
		write_c0_conf(config & ~TX39_CONF_ICE);
		TX39_STOP_STREAMING();
		protected_blast_icache_range(start, end);
		write_c0_conf(config);
		local_irq_restore(flags);
	}
}

static void tx39_dma_cache_wback_inv(unsigned long addr, unsigned long size)
{
	unsigned long end;

	if (((size | addr) & (PAGE_SIZE - 1)) == 0) {
		end = addr + size;
		do {
			tx39_blast_dcache_page(addr);
			addr += PAGE_SIZE;
		} while(addr != end);
	} else if (size > dcache_size) {
		tx39_blast_dcache();
	} else {
		blast_dcache_range(addr, addr + size);
	}
}

static void tx39_dma_cache_inv(unsigned long addr, unsigned long size)
{
	unsigned long end;

	if (((size | addr) & (PAGE_SIZE - 1)) == 0) {
		end = addr + size;
		do {
			tx39_blast_dcache_page(addr);
			addr += PAGE_SIZE;
		} while(addr != end);
	} else if (size > dcache_size) {
		tx39_blast_dcache();
	} else {
		blast_inv_dcache_range(addr, addr + size);
	}
}

static void tx39_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long ic_lsize = current_cpu_data.icache.linesz;
	unsigned long dc_lsize = current_cpu_data.dcache.linesz;
	unsigned long config;
	unsigned long flags;

	protected_writeback_dcache_line(addr & ~(dc_lsize - 1));

	/* disable icache (set ICE#) */
	local_irq_save(flags);
	config = read_c0_conf();
	write_c0_conf(config & ~TX39_CONF_ICE);
	TX39_STOP_STREAMING();
	protected_flush_icache_line(addr & ~(ic_lsize - 1));
	write_c0_conf(config);
	local_irq_restore(flags);
}

static __init void tx39_probe_cache(void)
{
	unsigned long config;

	config = read_c0_conf();

	icache_size = 1 << (10 + ((config & TX39_CONF_ICS_MASK) >>
				  TX39_CONF_ICS_SHIFT));
	dcache_size = 1 << (10 + ((config & TX39_CONF_DCS_MASK) >>
				  TX39_CONF_DCS_SHIFT));

	current_cpu_data.icache.linesz = 16;
	switch (current_cpu_data.cputype) {
	case CPU_TX3912:
		current_cpu_data.icache.ways = 1;
		current_cpu_data.dcache.ways = 1;
		current_cpu_data.dcache.linesz = 4;
		break;

	case CPU_TX3927:
		current_cpu_data.icache.ways = 2;
		current_cpu_data.dcache.ways = 2;
		current_cpu_data.dcache.linesz = 16;
		break;

	case CPU_TX3922:
	default:
		current_cpu_data.icache.ways = 1;
		current_cpu_data.dcache.ways = 1;
		current_cpu_data.dcache.linesz = 16;
		break;
	}
}

void __init tx39_cache_init(void)
{
	extern void build_clear_page(void);
	extern void build_copy_page(void);
	unsigned long config;

	config = read_c0_conf();
	config &= ~TX39_CONF_WBON;
	write_c0_conf(config);

	tx39_probe_cache();

	switch (current_cpu_data.cputype) {
	case CPU_TX3912:
		/* TX39/H core (writethru direct-map cache) */
		flush_cache_all	= tx39h_flush_icache_all;
		__flush_cache_all	= tx39h_flush_icache_all;
		flush_cache_mm		= (void *) tx39h_flush_icache_all;
		flush_cache_range	= (void *) tx39h_flush_icache_all;
		flush_cache_page	= (void *) tx39h_flush_icache_all;
		flush_icache_range	= (void *) tx39h_flush_icache_all;

		flush_cache_sigtramp	= (void *) tx39h_flush_icache_all;
		local_flush_data_cache_page	= (void *) tx39h_flush_icache_all;
		flush_data_cache_page	= (void *) tx39h_flush_icache_all;

		_dma_cache_wback_inv	= tx39h_dma_cache_wback_inv;

		shm_align_mask		= PAGE_SIZE - 1;

		break;

	case CPU_TX3922:
	case CPU_TX3927:
	default:
		/* TX39/H2,H3 core (writeback 2way-set-associative cache) */
		r3k_have_wired_reg = 1;
		write_c0_wired(0);	/* set 8 on reset... */
		/* board-dependent init code may set WBON */

		flush_cache_all = tx39_flush_cache_all;
		__flush_cache_all = tx39___flush_cache_all;
		flush_cache_mm = tx39_flush_cache_mm;
		flush_cache_range = tx39_flush_cache_range;
		flush_cache_page = tx39_flush_cache_page;
		flush_icache_range = tx39_flush_icache_range;

		flush_cache_sigtramp = tx39_flush_cache_sigtramp;
		local_flush_data_cache_page = local_tx39_flush_data_cache_page;
		flush_data_cache_page = tx39_flush_data_cache_page;

		_dma_cache_wback_inv = tx39_dma_cache_wback_inv;
		_dma_cache_wback = tx39_dma_cache_wback_inv;
		_dma_cache_inv = tx39_dma_cache_inv;

		shm_align_mask = max_t(unsigned long,
		                       (dcache_size / current_cpu_data.dcache.ways) - 1,
		                       PAGE_SIZE - 1);

		break;
	}

	current_cpu_data.icache.waysize = icache_size / current_cpu_data.icache.ways;
	current_cpu_data.dcache.waysize = dcache_size / current_cpu_data.dcache.ways;

	current_cpu_data.icache.sets =
		current_cpu_data.icache.waysize / current_cpu_data.icache.linesz;
	current_cpu_data.dcache.sets =
		current_cpu_data.dcache.waysize / current_cpu_data.dcache.linesz;

	if (current_cpu_data.dcache.waysize > PAGE_SIZE)
		current_cpu_data.dcache.flags |= MIPS_CACHE_ALIASES;

	current_cpu_data.icache.waybit = 0;
	current_cpu_data.dcache.waybit = 0;

	printk("Primary instruction cache %ldkB, linesize %d bytes\n",
		icache_size >> 10, current_cpu_data.icache.linesz);
	printk("Primary data cache %ldkB, linesize %d bytes\n",
		dcache_size >> 10, current_cpu_data.dcache.linesz);

	build_clear_page();
	build_copy_page();
	tx39h_flush_icache_all();
}
