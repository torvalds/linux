#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/swap.h>
#include <linux/memblock.h>
#include <linux/swapfile.h>
#include <linux/swapops.h>
#include <linux/kmemleak.h>
#include <linux/sched/task.h>

#include <asm/set_memory.h>
#include <asm/e820/api.h>
#include <asm/init.h>
#include <asm/page.h>
#include <asm/page_types.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/proto.h>
#include <asm/dma.h>		/* for MAX_DMA_PFN */
#include <asm/microcode.h>
#include <asm/kaslr.h>
#include <asm/hypervisor.h>
#include <asm/cpufeature.h>
#include <asm/pti.h>
#include <asm/text-patching.h>
#include <asm/memtype.h>

/*
 * We need to define the tracepoints somewhere, and tlb.c
 * is only compiled when SMP=y.
 */
#include <trace/events/tlb.h>

#include "mm_internal.h"

/*
 * Tables translating between page_cache_type_t and pte encoding.
 *
 * The default values are defined statically as minimal supported mode;
 * WC and WT fall back to UC-.  pat_init() updates these values to support
 * more cache modes, WC and WT, when it is safe to do so.  See pat_init()
 * for the details.  Note, __early_ioremap() used during early boot-time
 * takes pgprot_t (pte encoding) and does not use these tables.
 *
 *   Index into __cachemode2pte_tbl[] is the cachemode.
 *
 *   Index into __pte2cachemode_tbl[] are the caching attribute bits of the pte
 *   (_PAGE_PWT, _PAGE_PCD, _PAGE_PAT) at index bit positions 0, 1, 2.
 */
static uint16_t __cachemode2pte_tbl[_PAGE_CACHE_MODE_NUM] = {
	[_PAGE_CACHE_MODE_WB      ]	= 0         | 0        ,
	[_PAGE_CACHE_MODE_WC      ]	= 0         | _PAGE_PCD,
	[_PAGE_CACHE_MODE_UC_MINUS]	= 0         | _PAGE_PCD,
	[_PAGE_CACHE_MODE_UC      ]	= _PAGE_PWT | _PAGE_PCD,
	[_PAGE_CACHE_MODE_WT      ]	= 0         | _PAGE_PCD,
	[_PAGE_CACHE_MODE_WP      ]	= 0         | _PAGE_PCD,
};

unsigned long cachemode2protval(enum page_cache_mode pcm)
{
	if (likely(pcm == 0))
		return 0;
	return __cachemode2pte_tbl[pcm];
}
EXPORT_SYMBOL(cachemode2protval);

static uint8_t __pte2cachemode_tbl[8] = {
	[__pte2cm_idx( 0        | 0         | 0        )] = _PAGE_CACHE_MODE_WB,
	[__pte2cm_idx(_PAGE_PWT | 0         | 0        )] = _PAGE_CACHE_MODE_UC_MINUS,
	[__pte2cm_idx( 0        | _PAGE_PCD | 0        )] = _PAGE_CACHE_MODE_UC_MINUS,
	[__pte2cm_idx(_PAGE_PWT | _PAGE_PCD | 0        )] = _PAGE_CACHE_MODE_UC,
	[__pte2cm_idx( 0        | 0         | _PAGE_PAT)] = _PAGE_CACHE_MODE_WB,
	[__pte2cm_idx(_PAGE_PWT | 0         | _PAGE_PAT)] = _PAGE_CACHE_MODE_UC_MINUS,
	[__pte2cm_idx(0         | _PAGE_PCD | _PAGE_PAT)] = _PAGE_CACHE_MODE_UC_MINUS,
	[__pte2cm_idx(_PAGE_PWT | _PAGE_PCD | _PAGE_PAT)] = _PAGE_CACHE_MODE_UC,
};

/*
 * Check that the write-protect PAT entry is set for write-protect.
 * To do this without making assumptions how PAT has been set up (Xen has
 * another layout than the kernel), translate the _PAGE_CACHE_MODE_WP cache
 * mode via the __cachemode2pte_tbl[] into protection bits (those protection
 * bits will select a cache mode of WP or better), and then translate the
 * protection bits back into the cache mode using __pte2cm_idx() and the
 * __pte2cachemode_tbl[] array. This will return the really used cache mode.
 */
bool x86_has_pat_wp(void)
{
	uint16_t prot = __cachemode2pte_tbl[_PAGE_CACHE_MODE_WP];

	return __pte2cachemode_tbl[__pte2cm_idx(prot)] == _PAGE_CACHE_MODE_WP;
}

enum page_cache_mode pgprot2cachemode(pgprot_t pgprot)
{
	unsigned long masked;

	masked = pgprot_val(pgprot) & _PAGE_CACHE_MASK;
	if (likely(masked == 0))
		return 0;
	return __pte2cachemode_tbl[__pte2cm_idx(masked)];
}

static unsigned long __initdata pgt_buf_start;
static unsigned long __initdata pgt_buf_end;
static unsigned long __initdata pgt_buf_top;

static unsigned long min_pfn_mapped;

static bool __initdata can_use_brk_pgt = true;

/*
 * Provide a run-time mean of disabling ZONE_DMA32 if it is enabled via
 * CONFIG_ZONE_DMA32.
 */
static bool disable_dma32 __ro_after_init;

/*
 * Pages returned are already directly mapped.
 *
 * Changing that is likely to break Xen, see commit:
 *
 *    279b706 x86,xen: introduce x86_init.mapping.pagetable_reserve
 *
 * for detailed information.
 */
__ref void *alloc_low_pages(unsigned int num)
{
	unsigned long pfn;
	int i;

	if (after_bootmem) {
		unsigned int order;

		order = get_order((unsigned long)num << PAGE_SHIFT);
		return (void *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO, order);
	}

	if ((pgt_buf_end + num) > pgt_buf_top || !can_use_brk_pgt) {
		unsigned long ret = 0;

		if (min_pfn_mapped < max_pfn_mapped) {
			ret = memblock_phys_alloc_range(
					PAGE_SIZE * num, PAGE_SIZE,
					min_pfn_mapped << PAGE_SHIFT,
					max_pfn_mapped << PAGE_SHIFT);
		}
		if (!ret && can_use_brk_pgt)
			ret = __pa(extend_brk(PAGE_SIZE * num, PAGE_SIZE));

		if (!ret)
			panic("alloc_low_pages: can not alloc memory");

		pfn = ret >> PAGE_SHIFT;
	} else {
		pfn = pgt_buf_end;
		pgt_buf_end += num;
	}

	for (i = 0; i < num; i++) {
		void *adr;

		adr = __va((pfn + i) << PAGE_SHIFT);
		clear_page(adr);
	}

	return __va(pfn << PAGE_SHIFT);
}

/*
 * By default need to be able to allocate page tables below PGD firstly for
 * the 0-ISA_END_ADDRESS range and secondly for the initial PMD_SIZE mapping.
 * With KASLR memory randomization, depending on the machine e820 memory and the
 * PUD alignment, twice that many pages may be needed when KASLR memory
 * randomization is enabled.
 */

#ifndef CONFIG_X86_5LEVEL
#define INIT_PGD_PAGE_TABLES    3
#else
#define INIT_PGD_PAGE_TABLES    4
#endif

#ifndef CONFIG_RANDOMIZE_MEMORY
#define INIT_PGD_PAGE_COUNT      (2 * INIT_PGD_PAGE_TABLES)
#else
#define INIT_PGD_PAGE_COUNT      (4 * INIT_PGD_PAGE_TABLES)
#endif

#define INIT_PGT_BUF_SIZE	(INIT_PGD_PAGE_COUNT * PAGE_SIZE)
RESERVE_BRK(early_pgt_alloc, INIT_PGT_BUF_SIZE);
void  __init early_alloc_pgt_buf(void)
{
	unsigned long tables = INIT_PGT_BUF_SIZE;
	phys_addr_t base;

	base = __pa(extend_brk(tables, PAGE_SIZE));

	pgt_buf_start = base >> PAGE_SHIFT;
	pgt_buf_end = pgt_buf_start;
	pgt_buf_top = pgt_buf_start + (tables >> PAGE_SHIFT);
}

int after_bootmem;

early_param_on_off("gbpages", "nogbpages", direct_gbpages, CONFIG_X86_DIRECT_GBPAGES);

struct map_range {
	unsigned long start;
	unsigned long end;
	unsigned page_size_mask;
};

static int page_size_mask;

/*
 * Save some of cr4 feature set we're using (e.g.  Pentium 4MB
 * enable and PPro Global page enable), so that any CPU's that boot
 * up after us can get the correct flags. Invoked on the boot CPU.
 */
static inline void cr4_set_bits_and_update_boot(unsigned long mask)
{
	mmu_cr4_features |= mask;
	if (trampoline_cr4_features)
		*trampoline_cr4_features = mmu_cr4_features;
	cr4_set_bits(mask);
}

static void __init probe_page_size_mask(void)
{
	/*
	 * For pagealloc debugging, identity mapping will use small pages.
	 * This will simplify cpa(), which otherwise needs to support splitting
	 * large pages into small in interrupt context, etc.
	 */
	if (boot_cpu_has(X86_FEATURE_PSE) && !debug_pagealloc_enabled())
		page_size_mask |= 1 << PG_LEVEL_2M;
	else
		direct_gbpages = 0;

	/* Enable PSE if available */
	if (boot_cpu_has(X86_FEATURE_PSE))
		cr4_set_bits_and_update_boot(X86_CR4_PSE);

	/* Enable PGE if available */
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (boot_cpu_has(X86_FEATURE_PGE)) {
		cr4_set_bits_and_update_boot(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}

	/* By the default is everything supported: */
	__default_kernel_pte_mask = __supported_pte_mask;
	/* Except when with PTI where the kernel is mostly non-Global: */
	if (cpu_feature_enabled(X86_FEATURE_PTI))
		__default_kernel_pte_mask &= ~_PAGE_GLOBAL;

	/* Enable 1 GB linear kernel mappings if available: */
	if (direct_gbpages && boot_cpu_has(X86_FEATURE_GBPAGES)) {
		printk(KERN_INFO "Using GB pages for direct mapping\n");
		page_size_mask |= 1 << PG_LEVEL_1G;
	} else {
		direct_gbpages = 0;
	}
}

static void setup_pcid(void)
{
	if (!IS_ENABLED(CONFIG_X86_64))
		return;

	if (!boot_cpu_has(X86_FEATURE_PCID))
		return;

	if (boot_cpu_has(X86_FEATURE_PGE)) {
		/*
		 * This can't be cr4_set_bits_and_update_boot() -- the
		 * trampoline code can't handle CR4.PCIDE and it wouldn't
		 * do any good anyway.  Despite the name,
		 * cr4_set_bits_and_update_boot() doesn't actually cause
		 * the bits in question to remain set all the way through
		 * the secondary boot asm.
		 *
		 * Instead, we brute-force it and set CR4.PCIDE manually in
		 * start_secondary().
		 */
		cr4_set_bits(X86_CR4_PCIDE);

		/*
		 * INVPCID's single-context modes (2/3) only work if we set
		 * X86_CR4_PCIDE, *and* we INVPCID support.  It's unusable
		 * on systems that have X86_CR4_PCIDE clear, or that have
		 * no INVPCID support at all.
		 */
		if (boot_cpu_has(X86_FEATURE_INVPCID))
			setup_force_cpu_cap(X86_FEATURE_INVPCID_SINGLE);
	} else {
		/*
		 * flush_tlb_all(), as currently implemented, won't work if
		 * PCID is on but PGE is not.  Since that combination
		 * doesn't exist on real hardware, there's no reason to try
		 * to fully support it, but it's polite to avoid corrupting
		 * data if we're on an improperly configured VM.
		 */
		setup_clear_cpu_cap(X86_FEATURE_PCID);
	}
}

#ifdef CONFIG_X86_32
#define NR_RANGE_MR 3
#else /* CONFIG_X86_64 */
#define NR_RANGE_MR 5
#endif

static int __meminit save_mr(struct map_range *mr, int nr_range,
			     unsigned long start_pfn, unsigned long end_pfn,
			     unsigned long page_size_mask)
{
	if (start_pfn < end_pfn) {
		if (nr_range >= NR_RANGE_MR)
			panic("run out of range for init_memory_mapping\n");
		mr[nr_range].start = start_pfn<<PAGE_SHIFT;
		mr[nr_range].end   = end_pfn<<PAGE_SHIFT;
		mr[nr_range].page_size_mask = page_size_mask;
		nr_range++;
	}

	return nr_range;
}

/*
 * adjust the page_size_mask for small range to go with
 *	big page size instead small one if nearby are ram too.
 */
static void __ref adjust_range_page_size_mask(struct map_range *mr,
							 int nr_range)
{
	int i;

	for (i = 0; i < nr_range; i++) {
		if ((page_size_mask & (1<<PG_LEVEL_2M)) &&
		    !(mr[i].page_size_mask & (1<<PG_LEVEL_2M))) {
			unsigned long start = round_down(mr[i].start, PMD_SIZE);
			unsigned long end = round_up(mr[i].end, PMD_SIZE);

#ifdef CONFIG_X86_32
			if ((end >> PAGE_SHIFT) > max_low_pfn)
				continue;
#endif

			if (memblock_is_region_memory(start, end - start))
				mr[i].page_size_mask |= 1<<PG_LEVEL_2M;
		}
		if ((page_size_mask & (1<<PG_LEVEL_1G)) &&
		    !(mr[i].page_size_mask & (1<<PG_LEVEL_1G))) {
			unsigned long start = round_down(mr[i].start, PUD_SIZE);
			unsigned long end = round_up(mr[i].end, PUD_SIZE);

			if (memblock_is_region_memory(start, end - start))
				mr[i].page_size_mask |= 1<<PG_LEVEL_1G;
		}
	}
}

static const char *page_size_string(struct map_range *mr)
{
	static const char str_1g[] = "1G";
	static const char str_2m[] = "2M";
	static const char str_4m[] = "4M";
	static const char str_4k[] = "4k";

	if (mr->page_size_mask & (1<<PG_LEVEL_1G))
		return str_1g;
	/*
	 * 32-bit without PAE has a 4M large page size.
	 * PG_LEVEL_2M is misnamed, but we can at least
	 * print out the right size in the string.
	 */
	if (IS_ENABLED(CONFIG_X86_32) &&
	    !IS_ENABLED(CONFIG_X86_PAE) &&
	    mr->page_size_mask & (1<<PG_LEVEL_2M))
		return str_4m;

	if (mr->page_size_mask & (1<<PG_LEVEL_2M))
		return str_2m;

	return str_4k;
}

static int __meminit split_mem_range(struct map_range *mr, int nr_range,
				     unsigned long start,
				     unsigned long end)
{
	unsigned long start_pfn, end_pfn, limit_pfn;
	unsigned long pfn;
	int i;

	limit_pfn = PFN_DOWN(end);

	/* head if not big page alignment ? */
	pfn = start_pfn = PFN_DOWN(start);
#ifdef CONFIG_X86_32
	/*
	 * Don't use a large page for the first 2/4MB of memory
	 * because there are often fixed size MTRRs in there
	 * and overlapping MTRRs into large pages can cause
	 * slowdowns.
	 */
	if (pfn == 0)
		end_pfn = PFN_DOWN(PMD_SIZE);
	else
		end_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
#else /* CONFIG_X86_64 */
	end_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
#endif
	if (end_pfn > limit_pfn)
		end_pfn = limit_pfn;
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);
		pfn = end_pfn;
	}

	/* big page (2M) range */
	start_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
#ifdef CONFIG_X86_32
	end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));
#else /* CONFIG_X86_64 */
	end_pfn = round_up(pfn, PFN_DOWN(PUD_SIZE));
	if (end_pfn > round_down(limit_pfn, PFN_DOWN(PMD_SIZE)))
		end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));
#endif

	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pfn = end_pfn;
	}

#ifdef CONFIG_X86_64
	/* big page (1G) range */
	start_pfn = round_up(pfn, PFN_DOWN(PUD_SIZE));
	end_pfn = round_down(limit_pfn, PFN_DOWN(PUD_SIZE));
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask &
				 ((1<<PG_LEVEL_2M)|(1<<PG_LEVEL_1G)));
		pfn = end_pfn;
	}

	/* tail is not big page (1G) alignment */
	start_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
	end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pfn = end_pfn;
	}
#endif

	/* tail is not big page (2M) alignment */
	start_pfn = pfn;
	end_pfn = limit_pfn;
	nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);

	if (!after_bootmem)
		adjust_range_page_size_mask(mr, nr_range);

	/* try to merge same page size and continuous */
	for (i = 0; nr_range > 1 && i < nr_range - 1; i++) {
		unsigned long old_start;
		if (mr[i].end != mr[i+1].start ||
		    mr[i].page_size_mask != mr[i+1].page_size_mask)
			continue;
		/* move it */
		old_start = mr[i].start;
		memmove(&mr[i], &mr[i+1],
			(nr_range - 1 - i) * sizeof(struct map_range));
		mr[i--].start = old_start;
		nr_range--;
	}

	for (i = 0; i < nr_range; i++)
		pr_debug(" [mem %#010lx-%#010lx] page %s\n",
				mr[i].start, mr[i].end - 1,
				page_size_string(&mr[i]));

	return nr_range;
}

struct range pfn_mapped[E820_MAX_ENTRIES];
int nr_pfn_mapped;

static void add_pfn_range_mapped(unsigned long start_pfn, unsigned long end_pfn)
{
	nr_pfn_mapped = add_range_with_merge(pfn_mapped, E820_MAX_ENTRIES,
					     nr_pfn_mapped, start_pfn, end_pfn);
	nr_pfn_mapped = clean_sort_range(pfn_mapped, E820_MAX_ENTRIES);

	max_pfn_mapped = max(max_pfn_mapped, end_pfn);

	if (start_pfn < (1UL<<(32-PAGE_SHIFT)))
		max_low_pfn_mapped = max(max_low_pfn_mapped,
					 min(end_pfn, 1UL<<(32-PAGE_SHIFT)));
}

bool pfn_range_is_mapped(unsigned long start_pfn, unsigned long end_pfn)
{
	int i;

	for (i = 0; i < nr_pfn_mapped; i++)
		if ((start_pfn >= pfn_mapped[i].start) &&
		    (end_pfn <= pfn_mapped[i].end))
			return true;

	return false;
}

/*
 * Setup the direct mapping of the physical memory at PAGE_OFFSET.
 * This runs before bootmem is initialized and gets pages directly from
 * the physical memory. To access them they are temporarily mapped.
 */
unsigned long __ref init_memory_mapping(unsigned long start,
					unsigned long end, pgprot_t prot)
{
	struct map_range mr[NR_RANGE_MR];
	unsigned long ret = 0;
	int nr_range, i;

	pr_debug("init_memory_mapping: [mem %#010lx-%#010lx]\n",
	       start, end - 1);

	memset(mr, 0, sizeof(mr));
	nr_range = split_mem_range(mr, 0, start, end);

	for (i = 0; i < nr_range; i++)
		ret = kernel_physical_mapping_init(mr[i].start, mr[i].end,
						   mr[i].page_size_mask,
						   prot);

	add_pfn_range_mapped(start >> PAGE_SHIFT, ret >> PAGE_SHIFT);

	return ret >> PAGE_SHIFT;
}

/*
 * We need to iterate through the E820 memory map and create direct mappings
 * for only E820_TYPE_RAM and E820_KERN_RESERVED regions. We cannot simply
 * create direct mappings for all pfns from [0 to max_low_pfn) and
 * [4GB to max_pfn) because of possible memory holes in high addresses
 * that cannot be marked as UC by fixed/variable range MTRRs.
 * Depending on the alignment of E820 ranges, this may possibly result
 * in using smaller size (i.e. 4K instead of 2M or 1G) page tables.
 *
 * init_mem_mapping() calls init_range_memory_mapping() with big range.
 * That range would have hole in the middle or ends, and only ram parts
 * will be mapped in init_range_memory_mapping().
 */
static unsigned long __init init_range_memory_mapping(
					   unsigned long r_start,
					   unsigned long r_end)
{
	unsigned long start_pfn, end_pfn;
	unsigned long mapped_ram_size = 0;
	int i;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL) {
		u64 start = clamp_val(PFN_PHYS(start_pfn), r_start, r_end);
		u64 end = clamp_val(PFN_PHYS(end_pfn), r_start, r_end);
		if (start >= end)
			continue;

		/*
		 * if it is overlapping with brk pgt, we need to
		 * alloc pgt buf from memblock instead.
		 */
		can_use_brk_pgt = max(start, (u64)pgt_buf_end<<PAGE_SHIFT) >=
				    min(end, (u64)pgt_buf_top<<PAGE_SHIFT);
		init_memory_mapping(start, end, PAGE_KERNEL);
		mapped_ram_size += end - start;
		can_use_brk_pgt = true;
	}

	return mapped_ram_size;
}

static unsigned long __init get_new_step_size(unsigned long step_size)
{
	/*
	 * Initial mapped size is PMD_SIZE (2M).
	 * We can not set step_size to be PUD_SIZE (1G) yet.
	 * In worse case, when we cross the 1G boundary, and
	 * PG_LEVEL_2M is not set, we will need 1+1+512 pages (2M + 8k)
	 * to map 1G range with PTE. Hence we use one less than the
	 * difference of page table level shifts.
	 *
	 * Don't need to worry about overflow in the top-down case, on 32bit,
	 * when step_size is 0, round_down() returns 0 for start, and that
	 * turns it into 0x100000000ULL.
	 * In the bottom-up case, round_up(x, 0) returns 0 though too, which
	 * needs to be taken into consideration by the code below.
	 */
	return step_size << (PMD_SHIFT - PAGE_SHIFT - 1);
}

/**
 * memory_map_top_down - Map [map_start, map_end) top down
 * @map_start: start address of the target memory range
 * @map_end: end address of the target memory range
 *
 * This function will setup direct mapping for memory range
 * [map_start, map_end) in top-down. That said, the page tables
 * will be allocated at the end of the memory, and we map the
 * memory in top-down.
 */
static void __init memory_map_top_down(unsigned long map_start,
				       unsigned long map_end)
{
	unsigned long real_end, last_start;
	unsigned long step_size;
	unsigned long addr;
	unsigned long mapped_ram_size = 0;

	/*
	 * Systems that have many reserved areas near top of the memory,
	 * e.g. QEMU with less than 1G RAM and EFI enabled, or Xen, will
	 * require lots of 4K mappings which may exhaust pgt_buf.
	 * Start with top-most PMD_SIZE range aligned at PMD_SIZE to ensure
	 * there is enough mapped memory that can be allocated from
	 * memblock.
	 */
	addr = memblock_phys_alloc_range(PMD_SIZE, PMD_SIZE, map_start,
					 map_end);
	memblock_phys_free(addr, PMD_SIZE);
	real_end = addr + PMD_SIZE;

	/* step_size need to be small so pgt_buf from BRK could cover it */
	step_size = PMD_SIZE;
	max_pfn_mapped = 0; /* will get exact value next */
	min_pfn_mapped = real_end >> PAGE_SHIFT;
	last_start = real_end;

	/*
	 * We start from the top (end of memory) and go to the bottom.
	 * The memblock_find_in_range() gets us a block of RAM from the
	 * end of RAM in [min_pfn_mapped, max_pfn_mapped) used as new pages
	 * for page table.
	 */
	while (last_start > map_start) {
		unsigned long start;

		if (last_start > step_size) {
			start = round_down(last_start - 1, step_size);
			if (start < map_start)
				start = map_start;
		} else
			start = map_start;
		mapped_ram_size += init_range_memory_mapping(start,
							last_start);
		last_start = start;
		min_pfn_mapped = last_start >> PAGE_SHIFT;
		if (mapped_ram_size >= step_size)
			step_size = get_new_step_size(step_size);
	}

	if (real_end < map_end)
		init_range_memory_mapping(real_end, map_end);
}

/**
 * memory_map_bottom_up - Map [map_start, map_end) bottom up
 * @map_start: start address of the target memory range
 * @map_end: end address of the target memory range
 *
 * This function will setup direct mapping for memory range
 * [map_start, map_end) in bottom-up. Since we have limited the
 * bottom-up allocation above the kernel, the page tables will
 * be allocated just above the kernel and we map the memory
 * in [map_start, map_end) in bottom-up.
 */
static void __init memory_map_bottom_up(unsigned long map_start,
					unsigned long map_end)
{
	unsigned long next, start;
	unsigned long mapped_ram_size = 0;
	/* step_size need to be small so pgt_buf from BRK could cover it */
	unsigned long step_size = PMD_SIZE;

	start = map_start;
	min_pfn_mapped = start >> PAGE_SHIFT;

	/*
	 * We start from the bottom (@map_start) and go to the top (@map_end).
	 * The memblock_find_in_range() gets us a block of RAM from the
	 * end of RAM in [min_pfn_mapped, max_pfn_mapped) used as new pages
	 * for page table.
	 */
	while (start < map_end) {
		if (step_size && map_end - start > step_size) {
			next = round_up(start + 1, step_size);
			if (next > map_end)
				next = map_end;
		} else {
			next = map_end;
		}

		mapped_ram_size += init_range_memory_mapping(start, next);
		start = next;

		if (mapped_ram_size >= step_size)
			step_size = get_new_step_size(step_size);
	}
}

/*
 * The real mode trampoline, which is required for bootstrapping CPUs
 * occupies only a small area under the low 1MB.  See reserve_real_mode()
 * for details.
 *
 * If KASLR is disabled the first PGD entry of the direct mapping is copied
 * to map the real mode trampoline.
 *
 * If KASLR is enabled, copy only the PUD which covers the low 1MB
 * area. This limits the randomization granularity to 1GB for both 4-level
 * and 5-level paging.
 */
static void __init init_trampoline(void)
{
#ifdef CONFIG_X86_64
	/*
	 * The code below will alias kernel page-tables in the user-range of the
	 * address space, including the Global bit. So global TLB entries will
	 * be created when using the trampoline page-table.
	 */
	if (!kaslr_memory_enabled())
		trampoline_pgd_entry = init_top_pgt[pgd_index(__PAGE_OFFSET)];
	else
		init_trampoline_kaslr();
#endif
}

void __init init_mem_mapping(void)
{
	unsigned long end;

	pti_check_boottime_disable();
	probe_page_size_mask();
	setup_pcid();

#ifdef CONFIG_X86_64
	end = max_pfn << PAGE_SHIFT;
#else
	end = max_low_pfn << PAGE_SHIFT;
#endif

	/* the ISA range is always mapped regardless of memory holes */
	init_memory_mapping(0, ISA_END_ADDRESS, PAGE_KERNEL);

	/* Init the trampoline, possibly with KASLR memory offset */
	init_trampoline();

	/*
	 * If the allocation is in bottom-up direction, we setup direct mapping
	 * in bottom-up, otherwise we setup direct mapping in top-down.
	 */
	if (memblock_bottom_up()) {
		unsigned long kernel_end = __pa_symbol(_end);

		/*
		 * we need two separate calls here. This is because we want to
		 * allocate page tables above the kernel. So we first map
		 * [kernel_end, end) to make memory above the kernel be mapped
		 * as soon as possible. And then use page tables allocated above
		 * the kernel to map [ISA_END_ADDRESS, kernel_end).
		 */
		memory_map_bottom_up(kernel_end, end);
		memory_map_bottom_up(ISA_END_ADDRESS, kernel_end);
	} else {
		memory_map_top_down(ISA_END_ADDRESS, end);
	}

#ifdef CONFIG_X86_64
	if (max_pfn > max_low_pfn) {
		/* can we preserve max_low_pfn ?*/
		max_low_pfn = max_pfn;
	}
#else
	early_ioremap_page_table_range_init();
#endif

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();

	x86_init.hyper.init_mem_mapping();

	early_memtest(0, max_pfn_mapped << PAGE_SHIFT);
}

/*
 * Initialize an mm_struct to be used during poking and a pointer to be used
 * during patching.
 */
void __init poking_init(void)
{
	spinlock_t *ptl;
	pte_t *ptep;

	poking_mm = copy_init_mm();
	BUG_ON(!poking_mm);

	/*
	 * Randomize the poking address, but make sure that the following page
	 * will be mapped at the same PMD. We need 2 pages, so find space for 3,
	 * and adjust the address if the PMD ends after the first one.
	 */
	poking_addr = TASK_UNMAPPED_BASE;
	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE))
		poking_addr += (kaslr_get_random_long("Poking") & PAGE_MASK) %
			(TASK_SIZE - TASK_UNMAPPED_BASE - 3 * PAGE_SIZE);

	if (((poking_addr + PAGE_SIZE) & ~PMD_MASK) == 0)
		poking_addr += PAGE_SIZE;

	/*
	 * We need to trigger the allocation of the page-tables that will be
	 * needed for poking now. Later, poking may be performed in an atomic
	 * section, which might cause allocation to fail.
	 */
	ptep = get_locked_pte(poking_mm, poking_addr, &ptl);
	BUG_ON(!ptep);
	pte_unmap_unlock(ptep, ptl);
}

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 * On x86, access has to be given to the first megabyte of RAM because that
 * area traditionally contains BIOS code and data regions used by X, dosemu,
 * and similar apps. Since they map the entire memory range, the whole range
 * must be allowed (for mapping), but any areas that would otherwise be
 * disallowed are flagged as being "zero filled" instead of rejected.
 * Access has to be given to non-kernel-ram areas as well, these contain the
 * PCI mmio resources as well as potential bios/acpi data regions.
 */
int devmem_is_allowed(unsigned long pagenr)
{
	if (region_intersects(PFN_PHYS(pagenr), PAGE_SIZE,
				IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE)
			!= REGION_DISJOINT) {
		/*
		 * For disallowed memory regions in the low 1MB range,
		 * request that the page be shown as all zeros.
		 */
		if (pagenr < 256)
			return 2;

		return 0;
	}

	/*
	 * This must follow RAM test, since System RAM is considered a
	 * restricted resource under CONFIG_STRICT_DEVMEM.
	 */
	if (iomem_is_exclusive(pagenr << PAGE_SHIFT)) {
		/* Low 1MB bypasses iomem restrictions. */
		if (pagenr < 256)
			return 1;

		return 0;
	}

	return 1;
}

void free_init_pages(const char *what, unsigned long begin, unsigned long end)
{
	unsigned long begin_aligned, end_aligned;

	/* Make sure boundaries are page aligned */
	begin_aligned = PAGE_ALIGN(begin);
	end_aligned   = end & PAGE_MASK;

	if (WARN_ON(begin_aligned != begin || end_aligned != end)) {
		begin = begin_aligned;
		end   = end_aligned;
	}

	if (begin >= end)
		return;

	/*
	 * If debugging page accesses then do not free this memory but
	 * mark them not present - any buggy init-section access will
	 * create a kernel page fault:
	 */
	if (debug_pagealloc_enabled()) {
		pr_info("debug: unmapping init [mem %#010lx-%#010lx]\n",
			begin, end - 1);
		/*
		 * Inform kmemleak about the hole in the memory since the
		 * corresponding pages will be unmapped.
		 */
		kmemleak_free_part((void *)begin, end - begin);
		set_memory_np(begin, (end - begin) >> PAGE_SHIFT);
	} else {
		/*
		 * We just marked the kernel text read only above, now that
		 * we are going to free part of that, we need to make that
		 * writeable and non-executable first.
		 */
		set_memory_nx(begin, (end - begin) >> PAGE_SHIFT);
		set_memory_rw(begin, (end - begin) >> PAGE_SHIFT);

		free_reserved_area((void *)begin, (void *)end,
				   POISON_FREE_INITMEM, what);
	}
}

/*
 * begin/end can be in the direct map or the "high kernel mapping"
 * used for the kernel image only.  free_init_pages() will do the
 * right thing for either kind of address.
 */
void free_kernel_image_pages(const char *what, void *begin, void *end)
{
	unsigned long begin_ul = (unsigned long)begin;
	unsigned long end_ul = (unsigned long)end;
	unsigned long len_pages = (end_ul - begin_ul) >> PAGE_SHIFT;

	free_init_pages(what, begin_ul, end_ul);

	/*
	 * PTI maps some of the kernel into userspace.  For performance,
	 * this includes some kernel areas that do not contain secrets.
	 * Those areas might be adjacent to the parts of the kernel image
	 * being freed, which may contain secrets.  Remove the "high kernel
	 * image mapping" for these freed areas, ensuring they are not even
	 * potentially vulnerable to Meltdown regardless of the specific
	 * optimizations PTI is currently using.
	 *
	 * The "noalias" prevents unmapping the direct map alias which is
	 * needed to access the freed pages.
	 *
	 * This is only valid for 64bit kernels. 32bit has only one mapping
	 * which can't be treated in this way for obvious reasons.
	 */
	if (IS_ENABLED(CONFIG_X86_64) && cpu_feature_enabled(X86_FEATURE_PTI))
		set_memory_np_noalias(begin_ul, len_pages);
}

void __ref free_initmem(void)
{
	e820__reallocate_tables();

	mem_encrypt_free_decrypted_mem();

	free_kernel_image_pages("unused kernel image (initmem)",
				&__init_begin, &__init_end);
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	/*
	 * end could be not aligned, and We can not align that,
	 * decompressor could be confused by aligned initrd_end
	 * We already reserve the end partial page before in
	 *   - i386_start_kernel()
	 *   - x86_64_start_kernel()
	 *   - relocate_initrd()
	 * So here We can do PAGE_ALIGN() safely to get partial page to be freed
	 */
	free_init_pages("initrd", start, PAGE_ALIGN(end));
}
#endif

/*
 * Calculate the precise size of the DMA zone (first 16 MB of RAM),
 * and pass it to the MM layer - to help it set zone watermarks more
 * accurately.
 *
 * Done on 64-bit systems only for the time being, although 32-bit systems
 * might benefit from this as well.
 */
void __init memblock_find_dma_reserve(void)
{
#ifdef CONFIG_X86_64
	u64 nr_pages = 0, nr_free_pages = 0;
	unsigned long start_pfn, end_pfn;
	phys_addr_t start_addr, end_addr;
	int i;
	u64 u;

	/*
	 * Iterate over all memory ranges (free and reserved ones alike),
	 * to calculate the total number of pages in the first 16 MB of RAM:
	 */
	nr_pages = 0;
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL) {
		start_pfn = min(start_pfn, MAX_DMA_PFN);
		end_pfn   = min(end_pfn,   MAX_DMA_PFN);

		nr_pages += end_pfn - start_pfn;
	}

	/*
	 * Iterate over free memory ranges to calculate the number of free
	 * pages in the DMA zone, while not counting potential partial
	 * pages at the beginning or the end of the range:
	 */
	nr_free_pages = 0;
	for_each_free_mem_range(u, NUMA_NO_NODE, MEMBLOCK_NONE, &start_addr, &end_addr, NULL) {
		start_pfn = min_t(unsigned long, PFN_UP(start_addr), MAX_DMA_PFN);
		end_pfn   = min_t(unsigned long, PFN_DOWN(end_addr), MAX_DMA_PFN);

		if (start_pfn < end_pfn)
			nr_free_pages += end_pfn - start_pfn;
	}

	set_dma_reserve(nr_pages - nr_free_pages);
#endif
}

void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA]		= min(MAX_DMA_PFN, max_low_pfn);
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32]	= disable_dma32 ? 0 : min(MAX_DMA32_PFN, max_low_pfn);
#endif
	max_zone_pfns[ZONE_NORMAL]	= max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM]	= max_pfn;
#endif

	free_area_init(max_zone_pfns);
}

static int __init early_disable_dma32(char *buf)
{
	if (!buf)
		return -EINVAL;

	if (!strcmp(buf, "on"))
		disable_dma32 = true;

	return 0;
}
early_param("disable_dma32", early_disable_dma32);

__visible DEFINE_PER_CPU_ALIGNED(struct tlb_state, cpu_tlbstate) = {
	.loaded_mm = &init_mm,
	.next_asid = 1,
	.cr4 = ~0UL,	/* fail hard if we screw up cr4 shadow initialization */
};

void update_cache_mode_entry(unsigned entry, enum page_cache_mode cache)
{
	/* entry 0 MUST be WB (hardwired to speed up translations) */
	BUG_ON(!entry && cache != _PAGE_CACHE_MODE_WB);

	__cachemode2pte_tbl[cache] = __cm_idx2pte(entry);
	__pte2cachemode_tbl[entry] = cache;
}

#ifdef CONFIG_SWAP
unsigned long arch_max_swapfile_size(void)
{
	unsigned long pages;

	pages = generic_max_swapfile_size();

	if (boot_cpu_has_bug(X86_BUG_L1TF) && l1tf_mitigation != L1TF_MITIGATION_OFF) {
		/* Limit the swap file size to MAX_PA/2 for L1TF workaround */
		unsigned long long l1tf_limit = l1tf_pfn_limit();
		/*
		 * We encode swap offsets also with 3 bits below those for pfn
		 * which makes the usable limit higher.
		 */
#if CONFIG_PGTABLE_LEVELS > 2
		l1tf_limit <<= PAGE_SHIFT - SWP_OFFSET_FIRST_BIT;
#endif
		pages = min_t(unsigned long long, l1tf_limit, pages);
	}
	return pages;
}
#endif
