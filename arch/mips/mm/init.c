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
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>
#include <linux/hardirq.h>
#include <linux/gfp.h>
#include <linux/kcore.h>

#include <asm/asm-offsets.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/cpu.h>
#include <asm/dma.h>
#include <asm/kmap_types.h>
#include <asm/maar.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>
#include <asm/maar.h>

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
void setup_zero_pages(void)
{
	unsigned int order, i;
	struct page *page;

	if (cpu_has_vce)
		order = 3;
	else
		order = 0;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page((void *)empty_zero_page);
	split_page(page, order);
	for (i = 0; i < (1 << order); i++, page++)
		mark_page_reserved(page);

	zero_page_mask = ((PAGE_SIZE << order) - 1) & PAGE_MASK;
}

static void *__kmap_pgprot(struct page *page, unsigned long addr, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr, flags, entrylo;
	unsigned long old_ctx;
	pte_t pte;
	int tlbidx;

	BUG_ON(Page_dcache_dirty(page));

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
	void *vfrom, *vto;

	vto = kmap_atomic(to);
	if (cpu_has_dc_aliases &&
	    page_mapcount(from) && !Page_dcache_dirty(from)) {
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
	if (cpu_has_dc_aliases &&
	    page_mapcount(page) && !Page_dcache_dirty(page)) {
		void *vto = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(vto, src, len);
		kunmap_coherent();
	} else {
		memcpy(dst, src, len);
		if (cpu_has_dc_aliases)
			SetPageDcacheDirty(page);
	}
	if (vma->vm_flags & VM_EXEC)
		flush_cache_page(vma, vaddr, page_to_pfn(page));
}

void copy_from_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len)
{
	if (cpu_has_dc_aliases &&
	    page_mapcount(page) && !Page_dcache_dirty(page)) {
		void *vfrom = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(dst, vfrom, len);
		kunmap_coherent();
	} else {
		memcpy(dst, src, len);
		if (cpu_has_dc_aliases)
			SetPageDcacheDirty(page);
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
	i = __pgd_offset(vaddr);
	j = __pud_offset(vaddr);
	k = __pmd_offset(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr < end); pgd++, i++) {
		pud = (pud_t *)pgd;
		for ( ; (j < PTRS_PER_PUD) && (vaddr < end); pud++, j++) {
			pmd = (pmd_t *)pud;
			for (; (k < PTRS_PER_PMD) && (vaddr < end); pmd++, k++) {
				if (pmd_none(*pmd)) {
					pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
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

unsigned __weak platform_maar_init(unsigned num_pairs)
{
	struct maar_config cfg[BOOT_MEM_MAP_MAX];
	unsigned i, num_configured, num_cfg = 0;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
		case BOOT_MEM_INIT_RAM:
			break;
		default:
			continue;
		}

		/* Round lower up */
		cfg[num_cfg].lower = boot_mem_map.map[i].addr;
		cfg[num_cfg].lower = (cfg[num_cfg].lower + 0xffff) & ~0xffff;

		/* Round upper down */
		cfg[num_cfg].upper = boot_mem_map.map[i].addr +
					boot_mem_map.map[i].size;
		cfg[num_cfg].upper = (cfg[num_cfg].upper & ~0xffff) - 1;

		cfg[num_cfg].attrs = MIPS_MAAR_S;
		num_cfg++;
	}

	num_configured = maar_config(cfg, num_cfg, num_pairs);
	if (num_configured < num_cfg)
		pr_warn("Not enough MAAR pairs (%u) for all bootmem regions (%u)\n",
			num_pairs, num_cfg);

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

		write_c0_maari(i + 1);
		back_to_back_c0_hazard();
		lower = read_c0_maar();

		attr = lower & upper;
		lower = (lower & MIPS_MAAR_ADDR) << 4;
		upper = ((upper & MIPS_MAAR_ADDR) << 4) | 0xffff;

		pr_info("  [%d]: ", i / 2);
		if (!(attr & MIPS_MAAR_V)) {
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

#ifndef CONFIG_NEED_MULTIPLE_NODES
int page_is_ram(unsigned long pagenr)
{
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long addr, end;

		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
		case BOOT_MEM_INIT_RAM:
			break;
		default:
			/* not usable memory */
			continue;
		}

		addr = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr +
			       boot_mem_map.map[i].size);

		if (pagenr >= addr && pagenr < end)
			return 1;
	}

	return 0;
}

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	unsigned long lastpfn __maybe_unused;

	pagetable_init();

#ifdef CONFIG_HIGHMEM
	kmap_init();
#endif
#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	lastpfn = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM] = highend_pfn;
	lastpfn = highend_pfn;

	if (cpu_has_dc_aliases && max_low_pfn != highend_pfn) {
		printk(KERN_WARNING "This processor doesn't support highmem."
		       " %ldk highmem ignored\n",
		       (highend_pfn - max_low_pfn) << (PAGE_SHIFT - 10));
		max_zone_pfns[ZONE_HIGHMEM] = max_low_pfn;
		lastpfn = max_low_pfn;
	}
#endif

	free_area_init_nodes(max_zone_pfns);
}

#ifdef CONFIG_64BIT
static struct kcore_list kcore_kseg0;
#endif

static inline void mem_init_free_highmem(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;

	if (cpu_has_dc_aliases)
		return;

	for (tmp = highstart_pfn; tmp < highend_pfn; tmp++) {
		struct page *page = pfn_to_page(tmp);

		if (!page_is_ram(tmp))
			SetPageReserved(page);
		else
			free_highmem_page(page);
	}
#endif
}

void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
#ifdef CONFIG_DISCONTIGMEM
#error "CONFIG_HIGHMEM and CONFIG_DISCONTIGMEM dont work together yet"
#endif
	max_mapnr = highend_pfn ? highend_pfn : max_low_pfn;
#else
	max_mapnr = max_low_pfn;
#endif
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

	maar_init();
	free_all_bootmem();
	setup_zero_pages();	/* Setup zeroed pages.  */
	mem_init_free_highmem();
	mem_init_print_info(NULL);

#ifdef CONFIG_64BIT
	if ((unsigned long) &_text > (unsigned long) CKSEG0)
		/* The -4 is a hack so that user tools don't have to handle
		   the overflow.  */
		kclist_add(&kcore_kseg0, (void *) CKSEG0,
				0x80000000 - 4, KCORE_TEXT);
#endif
}
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

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

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif

void (*free_init_pages_eva)(void *begin, void *end) = NULL;

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

#ifndef CONFIG_MIPS_PGD_C0_CONTEXT
unsigned long pgd_current[NR_CPUS];
#endif

/*
 * gcc 3.3 and older have trouble determining that PTRS_PER_PGD and PGD_ORDER
 * are constants.  So we use the variants from asm-offset.h until that gcc
 * will officially be retired.
 *
 * Align swapper_pg_dir in to 64K, allows its address to be loaded
 * with a single LUI instruction in the TLB handlers.  If we used
 * __aligned(64K), its size would get rounded up to the alignment
 * size, and waste space.  So we place it in its own section and align
 * it in the linker script.
 */
pgd_t swapper_pg_dir[_PTRS_PER_PGD] __section(.bss..swapper_pg_dir);
#ifndef __PAGETABLE_PMD_FOLDED
pmd_t invalid_pmd_table[PTRS_PER_PMD] __page_aligned_bss;
#endif
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned_bss;
