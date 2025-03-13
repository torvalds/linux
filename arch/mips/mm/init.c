/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>
#include <linux/hardirq.h>
#include <linux/gfp.h>
#include <linux/kcore.h>
#include <linux/initrd.h>
#include <linux/execmem.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/cpu.h>
#include <asm/dma.h>
#include <asm/maar.h>
#include <asm/mmu_context.h>
#include <asm/mmzone.h>
#include <asm/sections.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>

/*
 * We have up to 8 empty zeroed pages so we can map one of the right colour
 * when needed.	 This is necessary only on R4000 / R4400 SC and MC versions
 * where we have to avoid VCED / VECI exceptions for good performance at
 * any price.  Since page is never written to after the initialization we
 * don't have to care about aliases on other CPUs.
 */
unsigned long empty_zero_page, zero_page_mask;
EXPORT_SYMBOL_GPL(empty_zero_page);
EXPORT_SYMBOL(zero_page_mask);

/*
 * Not static inline because used by IP27 special magic initialization code
 */
static void __init setup_zero_pages(void)
{
	unsigned int order;

	if (cpu_has_vce)
		order = 3;
	else
		order = 0;

	empty_zero_page = (unsigned long)memblock_alloc_or_panic(PAGE_SIZE << order, PAGE_SIZE);

	zero_page_mask = ((PAGE_SIZE << order) - 1) & PAGE_MASK;
}

static void *__kmap_pgprot(struct page *page, unsigned long addr, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned int old_mmid;
	unsigned long vaddr, flags, entrylo;
	unsigned long old_ctx;
	pte_t pte;
	int tlbidx;

	BUG_ON(folio_test_dcache_dirty(page_folio(page)));

	preempt_disable();
	pagefault_disable();
	idx = (addr >> PAGE_SHIFT) & (FIX_N_COLOURS - 1);
	idx += in_interrupt() ? FIX_N_COLOURS : 0;
	vaddr = __fix_to_virt(FIX_CMAP_END - idx);
	pte = mk_pte(page, prot);
#if defined(CONFIG_XPA)
	entrylo = pte_to_entrylo(pte.pte_high);
#elif defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)
	entrylo = pte.pte_high;
#else
	entrylo = pte_to_entrylo(pte_val(pte));
#endif

	local_irq_save(flags);
	old_ctx = read_c0_entryhi();
	write_c0_entryhi(vaddr & (PAGE_MASK << 1));
	write_c0_entrylo0(entrylo);
	write_c0_entrylo1(entrylo);
	if (cpu_has_mmid) {
		old_mmid = read_c0_memorymapid();
		write_c0_memorymapid(MMID_KERNEL_WIRED);
	}
#ifdef CONFIG_XPA
	if (cpu_has_xpa) {
		entrylo = (pte.pte_low & _PFNX_MASK);
		writex_c0_entrylo0(entrylo);
		writex_c0_entrylo1(entrylo);
	}
#endif
	tlbidx = num_wired_entries();
	write_c0_wired(tlbidx + 1);
	write_c0_index(tlbidx);
	mtc0_tlbw_hazard();
	tlb_write_indexed();
	tlbw_use_hazard();
	write_c0_entryhi(old_ctx);
	if (cpu_has_mmid)
		write_c0_memorymapid(old_mmid);
	local_irq_restore(flags);

	return (void*) vaddr;
}

void *kmap_coherent(struct page *page, unsigned long addr)
{
	return __kmap_pgprot(page, addr, PAGE_KERNEL);
}

void *kmap_noncoherent(struct page *page, unsigned long addr)
{
	return __kmap_pgprot(page, addr, PAGE_KERNEL_NC);
}

void kunmap_coherent(void)
{
	unsigned int wired;
	unsigned long flags, old_ctx;

	local_irq_save(flags);
	old_ctx = read_c0_entryhi();
	wired = num_wired_entries() - 1;
	write_c0_wired(wired);
	write_c0_index(wired);
	write_c0_entryhi(UNIQUE_ENTRYHI(wired));
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);
	mtc0_tlbw_hazard();
	tlb_write_indexed();
	tlbw_use_hazard();
	write_c0_entryhi(old_ctx);
	local_irq_restore(flags);
	pagefault_enable();
	preempt_enable();
}

void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	struct folio *src = page_folio(from);
	void *vfrom, *vto;

	vto = kmap_atomic(to);
	if (cpu_has_dc_aliases &&
	    folio_mapped(src) && !folio_test_dcache_dirty(src)) {
		vfrom = kmap_coherent(from, vaddr);
		copy_page(vto, vfrom);
		kunmap_coherent();
	} else {
		vfrom = kmap_atomic(from);
		copy_page(vto, vfrom);
		kunmap_atomic(vfrom);
	}
	if ((!cpu_has_ic_fills_f_dc) ||
	    pages_do_alias((unsigned long)vto, vaddr & PAGE_MASK))
		flush_data_cache_page((unsigned long)vto);
	kunmap_atomic(vto);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}

void copy_to_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len)
{
	struct folio *folio = page_folio(page);

	if (cpu_has_dc_aliases &&
	    folio_mapped(folio) && !folio_test_dcache_dirty(folio)) {
		void *vto = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(vto, src, len);
		kunmap_coherent();
	} else {
		memcpy(dst, src, len);
		if (cpu_has_dc_aliases)
			folio_set_dcache_dirty(folio);
	}
	if (vma->vm_flags & VM_EXEC)
		flush_cache_page(vma, vaddr, page_to_pfn(page));
}

void copy_from_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len)
{
	struct folio *folio = page_folio(page);

	if (cpu_has_dc_aliases &&
	    folio_mapped(folio) && !folio_test_dcache_dirty(folio)) {
		void *vfrom = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(dst, vfrom, len);
		kunmap_coherent();
	} else {
		memcpy(dst, src, len);
		if (cpu_has_dc_aliases)
			folio_set_dcache_dirty(folio);
	}
}
EXPORT_SYMBOL_GPL(copy_from_user_page);

void __init fixrange_init(unsigned long start, unsigned long end,
	pgd_t *pgd_base)
{
#ifdef CONFIG_HIGHMEM
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int i, j, k;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pud_index(vaddr);
	k = pmd_index(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr < end); pgd++, i++) {
		pud = (pud_t *)pgd;
		for ( ; (j < PTRS_PER_PUD) && (vaddr < end); pud++, j++) {
			pmd = (pmd_t *)pud;
			for (; (k < PTRS_PER_PMD) && (vaddr < end); pmd++, k++) {
				if (pmd_none(*pmd)) {
					pte = (pte_t *) memblock_alloc_low(PAGE_SIZE,
									   PAGE_SIZE);
					if (!pte)
						panic("%s: Failed to allocate %lu bytes align=%lx\n",
						      __func__, PAGE_SIZE,
						      PAGE_SIZE);

					set_pmd(pmd, __pmd((unsigned long)pte));
					BUG_ON(pte != pte_offset_kernel(pmd, 0));
				}
				vaddr += PMD_SIZE;
			}
			k = 0;
		}
		j = 0;
	}
#endif
}

struct maar_walk_info {
	struct maar_config cfg[16];
	unsigned int num_cfg;
};

static int maar_res_walk(unsigned long start_pfn, unsigned long nr_pages,
			 void *data)
{
	struct maar_walk_info *wi = data;
	struct maar_config *cfg = &wi->cfg[wi->num_cfg];
	unsigned int maar_align;

	/* MAAR registers hold physical addresses right shifted by 4 bits */
	maar_align = BIT(MIPS_MAAR_ADDR_SHIFT + 4);

	/* Fill in the MAAR config entry */
	cfg->lower = ALIGN(PFN_PHYS(start_pfn), maar_align);
	cfg->upper = ALIGN_DOWN(PFN_PHYS(start_pfn + nr_pages), maar_align) - 1;
	cfg->attrs = MIPS_MAAR_S;

	/* Ensure we don't overflow the cfg array */
	if (!WARN_ON(wi->num_cfg >= ARRAY_SIZE(wi->cfg)))
		wi->num_cfg++;

	return 0;
}


unsigned __weak platform_maar_init(unsigned num_pairs)
{
	unsigned int num_configured;
	struct maar_walk_info wi;

	wi.num_cfg = 0;
	walk_system_ram_range(0, max_pfn, &wi, maar_res_walk);

	num_configured = maar_config(wi.cfg, wi.num_cfg, num_pairs);
	if (num_configured < wi.num_cfg)
		pr_warn("Not enough MAAR pairs (%u) for all memory regions (%u)\n",
			num_pairs, wi.num_cfg);

	return num_configured;
}

void maar_init(void)
{
	unsigned num_maars, used, i;
	phys_addr_t lower, upper, attr;
	static struct {
		struct maar_config cfgs[3];
		unsigned used;
	} recorded = { { { 0 } }, 0 };

	if (!cpu_has_maar)
		return;

	/* Detect the number of MAARs */
	write_c0_maari(~0);
	back_to_back_c0_hazard();
	num_maars = read_c0_maari() + 1;

	/* MAARs should be in pairs */
	WARN_ON(num_maars % 2);

	/* Set MAARs using values we recorded already */
	if (recorded.used) {
		used = maar_config(recorded.cfgs, recorded.used, num_maars / 2);
		BUG_ON(used != recorded.used);
	} else {
		/* Configure the required MAARs */
		used = platform_maar_init(num_maars / 2);
	}

	/* Disable any further MAARs */
	for (i = (used * 2); i < num_maars; i++) {
		write_c0_maari(i);
		back_to_back_c0_hazard();
		write_c0_maar(0);
		back_to_back_c0_hazard();
	}

	if (recorded.used)
		return;

	pr_info("MAAR configuration:\n");
	for (i = 0; i < num_maars; i += 2) {
		write_c0_maari(i);
		back_to_back_c0_hazard();
		upper = read_c0_maar();
#ifdef CONFIG_XPA
		upper |= (phys_addr_t)readx_c0_maar() << MIPS_MAARX_ADDR_SHIFT;
#endif

		write_c0_maari(i + 1);
		back_to_back_c0_hazard();
		lower = read_c0_maar();
#ifdef CONFIG_XPA
		lower |= (phys_addr_t)readx_c0_maar() << MIPS_MAARX_ADDR_SHIFT;
#endif

		attr = lower & upper;
		lower = (lower & MIPS_MAAR_ADDR) << 4;
		upper = ((upper & MIPS_MAAR_ADDR) << 4) | 0xffff;

		pr_info("  [%d]: ", i / 2);
		if ((attr & MIPS_MAAR_V) != MIPS_MAAR_V) {
			pr_cont("disabled\n");
			continue;
		}

		pr_cont("%pa-%pa", &lower, &upper);

		if (attr & MIPS_MAAR_S)
			pr_cont(" speculate");

		pr_cont("\n");

		/* Record the setup for use on secondary CPUs */
		if (used <= ARRAY_SIZE(recorded.cfgs)) {
			recorded.cfgs[recorded.used].lower = lower;
			recorded.cfgs[recorded.used].upper = upper;
			recorded.cfgs[recorded.used].attrs = attr;
			recorded.used++;
		}
	}
}

#ifndef CONFIG_NUMA
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	pagetable_init();

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM] = highend_pfn;

	if (cpu_has_dc_aliases && max_low_pfn != highend_pfn) {
		printk(KERN_WARNING "This processor doesn't support highmem."
		       " %ldk highmem ignored\n",
		       (highend_pfn - max_low_pfn) << (PAGE_SHIFT - 10));
		max_zone_pfns[ZONE_HIGHMEM] = max_low_pfn;
	}
#endif

	free_area_init(max_zone_pfns);
}

#ifdef CONFIG_64BIT
static struct kcore_list kcore_kseg0;
#endif

void __init arch_mm_preinit(void)
{
	/*
	 * When PFN_PTE_SHIFT is greater than PAGE_SHIFT we won't have enough PTE
	 * bits to hold a full 32b physical address on MIPS32 systems.
	 */
	BUILD_BUG_ON(IS_ENABLED(CONFIG_32BIT) && (PFN_PTE_SHIFT > PAGE_SHIFT));

	maar_init();
	setup_zero_pages();	/* Setup zeroed pages.  */

#ifdef CONFIG_64BIT
	if ((unsigned long) &_text > (unsigned long) CKSEG0)
		/* The -4 is a hack so that user tools don't have to handle
		   the overflow.  */
		kclist_add(&kcore_kseg0, (void *) CKSEG0,
				0x80000000 - 4, KCORE_TEXT);
#endif
}
#else  /* CONFIG_NUMA */
void __init arch_mm_preinit(void)
{
	setup_zero_pages();	/* This comes from node 0 */
}
#endif /* !CONFIG_NUMA */

void free_init_pages(const char *what, unsigned long begin, unsigned long end)
{
	unsigned long pfn;

	for (pfn = PFN_UP(begin); pfn < PFN_DOWN(end); pfn++) {
		struct page *page = pfn_to_page(pfn);
		void *addr = phys_to_virt(PFN_PHYS(pfn));

		memset(addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_reserved_page(page);
	}
	printk(KERN_INFO "Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

void (*free_init_pages_eva)(void *begin, void *end) = NULL;

void __weak __init prom_free_prom_memory(void)
{
	/* nothing to do */
}

void __ref free_initmem(void)
{
	prom_free_prom_memory();
	/*
	 * Let the platform define a specific function to free the
	 * init section since EVA may have used any possible mapping
	 * between virtual and physical addresses.
	 */
	if (free_init_pages_eva)
		free_init_pages_eva((void *)&__init_begin, (void *)&__init_end);
	else
		free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	return node_distance(cpu_to_node(from), cpu_to_node(to));
}

static int __init pcpu_cpu_to_node(int cpu)
{
	return cpu_to_node(cpu);
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc;

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
				    PERCPU_DYNAMIC_RESERVE, PAGE_SIZE,
				    pcpu_cpu_distance,
				    pcpu_cpu_to_node);
	if (rc < 0)
		panic("Failed to initialize percpu areas.");

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

#ifndef CONFIG_MIPS_PGD_C0_CONTEXT
unsigned long pgd_current[NR_CPUS];
#endif

/*
 * Align swapper_pg_dir in to 64K, allows its address to be loaded
 * with a single LUI instruction in the TLB handlers.  If we used
 * __aligned(64K), its size would get rounded up to the alignment
 * size, and waste space.  So we place it in its own section and align
 * it in the linker script.
 */
pgd_t swapper_pg_dir[PTRS_PER_PGD] __section(".bss..swapper_pg_dir");
#ifndef __PAGETABLE_PUD_FOLDED
pud_t invalid_pud_table[PTRS_PER_PUD] __page_aligned_bss;
#endif
#ifndef __PAGETABLE_PMD_FOLDED
pmd_t invalid_pmd_table[PTRS_PER_PMD] __page_aligned_bss;
EXPORT_SYMBOL_GPL(invalid_pmd_table);
#endif
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned_bss;
EXPORT_SYMBOL(invalid_pte_table);

#ifdef CONFIG_EXECMEM
#ifdef MODULES_VADDR
static struct execmem_info execmem_info __ro_after_init;

struct execmem_info __init *execmem_arch_setup(void)
{
	execmem_info = (struct execmem_info){
		.ranges = {
			[EXECMEM_DEFAULT] = {
				.start	= MODULES_VADDR,
				.end	= MODULES_END,
				.pgprot	= PAGE_KERNEL,
				.alignment = 1,
			},
		},
	};

	return &execmem_info;
}
#endif
#endif /* CONFIG_EXECMEM */
