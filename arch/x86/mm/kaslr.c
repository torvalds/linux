/*
 * This file implements KASLR memory randomization for x86_64. It randomizes
 * the virtual address space of kernel memory regions (physical memory
 * mapping, vmalloc & vmemmap) for x86_64. This security feature mitigates
 * exploits relying on predictable kernel addresses.
 *
 * Entropy is generated using the KASLR early boot functions now shared in
 * the lib directory (originally written by Kees Cook). Randomization is
 * done on PGD & P4D/PUD page table levels to increase possible addresses.
 * The physical memory mapping code was adapted to support P4D/PUD level
 * virtual addresses. This implementation on the best configuration provides
 * 30,000 possible virtual addresses in average for each memory region.
 * An additional low memory page is used to ensure each CPU can start with
 * a PGD aligned virtual address (for realmode).
 *
 * The order of each memory region is not changed. The feature looks at
 * the available space for the regions based on different configuration
 * options and randomizes the base and space between each. The size of the
 * physical memory mapping is the available physical memory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/random.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/kaslr.h>

#include "mm_internal.h"

#define TB_SHIFT 40

/*
 * Virtual address start and end range for randomization. The end changes base
 * on configuration to have the highest amount of space for randomization.
 * It increases the possible random position for each randomized region.
 *
 * You need to add an if/def entry if you introduce a new memory region
 * compatible with KASLR. Your entry must be in logical order with memory
 * layout. For example, ESPFIX is before EFI because its virtual address is
 * before. You also need to add a BUILD_BUG_ON() in kernel_randomize_memory() to
 * ensure that this order is correct and won't be changed.
 */
static const unsigned long vaddr_start = __PAGE_OFFSET_BASE;

#if defined(CONFIG_X86_ESPFIX64)
static const unsigned long vaddr_end = ESPFIX_BASE_ADDR;
#elif defined(CONFIG_EFI)
static const unsigned long vaddr_end = EFI_VA_END;
#else
static const unsigned long vaddr_end = __START_KERNEL_map;
#endif

/* Default values */
unsigned long page_offset_base = __PAGE_OFFSET_BASE;
EXPORT_SYMBOL(page_offset_base);
unsigned long vmalloc_base = __VMALLOC_BASE;
EXPORT_SYMBOL(vmalloc_base);
unsigned long vmemmap_base = __VMEMMAP_BASE;
EXPORT_SYMBOL(vmemmap_base);

/*
 * Memory regions randomized by KASLR (except modules that use a separate logic
 * earlier during boot). The list is ordered based on virtual addresses. This
 * order is kept after randomization.
 */
static __initdata struct kaslr_memory_region {
	unsigned long *base;
	unsigned long size_tb;
} kaslr_regions[] = {
	{ &page_offset_base, 1 << (__PHYSICAL_MASK_SHIFT - TB_SHIFT) /* Maximum */ },
	{ &vmalloc_base, VMALLOC_SIZE_TB },
	{ &vmemmap_base, 1 },
};

/* Get size in bytes used by the memory region */
static inline unsigned long get_padding(struct kaslr_memory_region *region)
{
	return (region->size_tb << TB_SHIFT);
}

/*
 * Apply no randomization if KASLR was disabled at boot or if KASAN
 * is enabled. KASAN shadow mappings rely on regions being PGD aligned.
 */
static inline bool kaslr_memory_enabled(void)
{
	return kaslr_enabled() && !IS_ENABLED(CONFIG_KASAN);
}

/* Initialize base and padding for each memory region randomized with KASLR */
void __init kernel_randomize_memory(void)
{
	size_t i;
	unsigned long vaddr = vaddr_start;
	unsigned long rand, memory_tb;
	struct rnd_state rand_state;
	unsigned long remain_entropy;

	/*
	 * All these BUILD_BUG_ON checks ensures the memory layout is
	 * consistent with the vaddr_start/vaddr_end variables.
	 */
	BUILD_BUG_ON(vaddr_start >= vaddr_end);
	BUILD_BUG_ON(IS_ENABLED(CONFIG_X86_ESPFIX64) &&
		     vaddr_end >= EFI_VA_END);
	BUILD_BUG_ON((IS_ENABLED(CONFIG_X86_ESPFIX64) ||
		      IS_ENABLED(CONFIG_EFI)) &&
		     vaddr_end >= __START_KERNEL_map);
	BUILD_BUG_ON(vaddr_end > __START_KERNEL_map);

	if (!kaslr_memory_enabled())
		return;

	/*
	 * Update Physical memory mapping to available and
	 * add padding if needed (especially for memory hotplug support).
	 */
	BUG_ON(kaslr_regions[0].base != &page_offset_base);
	memory_tb = DIV_ROUND_UP(max_pfn << PAGE_SHIFT, 1UL << TB_SHIFT) +
		CONFIG_RANDOMIZE_MEMORY_PHYSICAL_PADDING;

	/* Adapt phyiscal memory region size based on available memory */
	if (memory_tb < kaslr_regions[0].size_tb)
		kaslr_regions[0].size_tb = memory_tb;

	/* Calculate entropy available between regions */
	remain_entropy = vaddr_end - vaddr_start;
	for (i = 0; i < ARRAY_SIZE(kaslr_regions); i++)
		remain_entropy -= get_padding(&kaslr_regions[i]);

	prandom_seed_state(&rand_state, kaslr_get_random_long("Memory"));

	for (i = 0; i < ARRAY_SIZE(kaslr_regions); i++) {
		unsigned long entropy;

		/*
		 * Select a random virtual address using the extra entropy
		 * available.
		 */
		entropy = remain_entropy / (ARRAY_SIZE(kaslr_regions) - i);
		prandom_bytes_state(&rand_state, &rand, sizeof(rand));
		if (IS_ENABLED(CONFIG_X86_5LEVEL))
			entropy = (rand % (entropy + 1)) & P4D_MASK;
		else
			entropy = (rand % (entropy + 1)) & PUD_MASK;
		vaddr += entropy;
		*kaslr_regions[i].base = vaddr;

		/*
		 * Jump the region and add a minimum padding based on
		 * randomization alignment.
		 */
		vaddr += get_padding(&kaslr_regions[i]);
		if (IS_ENABLED(CONFIG_X86_5LEVEL))
			vaddr = round_up(vaddr + 1, P4D_SIZE);
		else
			vaddr = round_up(vaddr + 1, PUD_SIZE);
		remain_entropy -= entropy;
	}
}

static void __meminit init_trampoline_pud(void)
{
	unsigned long paddr, paddr_next;
	pgd_t *pgd;
	pud_t *pud_page, *pud_page_tramp;
	int i;

	pud_page_tramp = alloc_low_page();

	paddr = 0;
	pgd = pgd_offset_k((unsigned long)__va(paddr));
	pud_page = (pud_t *) pgd_page_vaddr(*pgd);

	for (i = pud_index(paddr); i < PTRS_PER_PUD; i++, paddr = paddr_next) {
		pud_t *pud, *pud_tramp;
		unsigned long vaddr = (unsigned long)__va(paddr);

		pud_tramp = pud_page_tramp + pud_index(paddr);
		pud = pud_page + pud_index(vaddr);
		paddr_next = (paddr & PUD_MASK) + PUD_SIZE;

		*pud_tramp = *pud;
	}

	set_pgd(&trampoline_pgd_entry,
		__pgd(_KERNPG_TABLE | __pa(pud_page_tramp)));
}

static void __meminit init_trampoline_p4d(void)
{
	unsigned long paddr, paddr_next;
	pgd_t *pgd;
	p4d_t *p4d_page, *p4d_page_tramp;
	int i;

	p4d_page_tramp = alloc_low_page();

	paddr = 0;
	pgd = pgd_offset_k((unsigned long)__va(paddr));
	p4d_page = (p4d_t *) pgd_page_vaddr(*pgd);

	for (i = p4d_index(paddr); i < PTRS_PER_P4D; i++, paddr = paddr_next) {
		p4d_t *p4d, *p4d_tramp;
		unsigned long vaddr = (unsigned long)__va(paddr);

		p4d_tramp = p4d_page_tramp + p4d_index(paddr);
		p4d = p4d_page + p4d_index(vaddr);
		paddr_next = (paddr & P4D_MASK) + P4D_SIZE;

		*p4d_tramp = *p4d;
	}

	set_pgd(&trampoline_pgd_entry,
		__pgd(_KERNPG_TABLE | __pa(p4d_page_tramp)));
}

/*
 * Create PGD aligned trampoline table to allow real mode initialization
 * of additional CPUs. Consume only 1 low memory page.
 */
void __meminit init_trampoline(void)
{

	if (!kaslr_memory_enabled()) {
		init_trampoline_default();
		return;
	}

	if (IS_ENABLED(CONFIG_X86_5LEVEL))
		init_trampoline_p4d();
	else
		init_trampoline_pud();
}
