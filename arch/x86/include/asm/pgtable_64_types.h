/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_64_DEFS_H
#define _ASM_X86_PGTABLE_64_DEFS_H

#include <asm/sparsemem.h>

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <asm/kaslr.h>

/*
 * These are used to make use of C type-checking..
 */
typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	p4dval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;

typedef struct { pteval_t pte; } pte_t;
typedef struct { pmdval_t pmd; } pmd_t;

extern unsigned int __pgtable_l5_enabled;

#ifdef CONFIG_X86_5LEVEL
#ifdef USE_EARLY_PGTABLE_L5
/*
 * cpu_feature_enabled() is not available in early boot code.
 * Use variable instead.
 */
static inline bool pgtable_l5_enabled(void)
{
	return __pgtable_l5_enabled;
}
#else
#define pgtable_l5_enabled() cpu_feature_enabled(X86_FEATURE_LA57)
#endif /* USE_EARLY_PGTABLE_L5 */

#else
#define pgtable_l5_enabled() 0
#endif /* CONFIG_X86_5LEVEL */

extern unsigned int pgdir_shift;
extern unsigned int ptrs_per_p4d;

#endif	/* !__ASSEMBLY__ */

#define SHARED_KERNEL_PMD	0

#ifdef CONFIG_X86_5LEVEL

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	pgdir_shift
#define PTRS_PER_PGD	512

/*
 * 4th level page in 5-level paging case
 */
#define P4D_SHIFT		39
#define MAX_PTRS_PER_P4D	512
#define PTRS_PER_P4D		ptrs_per_p4d
#define P4D_SIZE		(_AC(1, UL) << P4D_SHIFT)
#define P4D_MASK		(~(P4D_SIZE - 1))

#define MAX_POSSIBLE_PHYSMEM_BITS	52

#else /* CONFIG_X86_5LEVEL */

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT		39
#define PTRS_PER_PGD		512
#define MAX_PTRS_PER_P4D	1

#endif /* CONFIG_X86_5LEVEL */

/*
 * 3rd level page
 */
#define PUD_SHIFT	30
#define PTRS_PER_PUD	512

/*
 * PMD_SHIFT determines the size of the area a middle-level
 * page table can map
 */
#define PMD_SHIFT	21
#define PTRS_PER_PMD	512

/*
 * entries per page directory level
 */
#define PTRS_PER_PTE	512

#define PMD_SIZE	(_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE - 1))
#define PUD_SIZE	(_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE - 1))
#define PGDIR_SIZE	(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))

/*
 * See Documentation/arch/x86/x86_64/mm.rst for a description of the memory map.
 *
 * Be very careful vs. KASLR when changing anything here. The KASLR address
 * range must not overlap with anything except the KASAN shadow area, which
 * is correct as KASAN disables KASLR.
 */
#define MAXMEM			(1UL << MAX_PHYSMEM_BITS)

#define GUARD_HOLE_PGD_ENTRY	-256UL
#define GUARD_HOLE_SIZE		(16UL << PGDIR_SHIFT)
#define GUARD_HOLE_BASE_ADDR	(GUARD_HOLE_PGD_ENTRY << PGDIR_SHIFT)
#define GUARD_HOLE_END_ADDR	(GUARD_HOLE_BASE_ADDR + GUARD_HOLE_SIZE)

#define LDT_PGD_ENTRY		-240UL
#define LDT_BASE_ADDR		(LDT_PGD_ENTRY << PGDIR_SHIFT)
#define LDT_END_ADDR		(LDT_BASE_ADDR + PGDIR_SIZE)

#define __VMALLOC_BASE_L4	0xffffc90000000000UL
#define __VMALLOC_BASE_L5 	0xffa0000000000000UL

#define VMALLOC_SIZE_TB_L4	32UL
#define VMALLOC_SIZE_TB_L5	12800UL

#define __VMEMMAP_BASE_L4	0xffffea0000000000UL
#define __VMEMMAP_BASE_L5	0xffd4000000000000UL

#ifdef CONFIG_DYNAMIC_MEMORY_LAYOUT
# define VMALLOC_START		vmalloc_base
# define VMALLOC_SIZE_TB	(pgtable_l5_enabled() ? VMALLOC_SIZE_TB_L5 : VMALLOC_SIZE_TB_L4)
# define VMEMMAP_START		vmemmap_base
#else
# define VMALLOC_START		__VMALLOC_BASE_L4
# define VMALLOC_SIZE_TB	VMALLOC_SIZE_TB_L4
# define VMEMMAP_START		__VMEMMAP_BASE_L4
#endif /* CONFIG_DYNAMIC_MEMORY_LAYOUT */

#ifdef CONFIG_RANDOMIZE_MEMORY
# define PHYSMEM_END		physmem_end
#endif

/*
 * End of the region for which vmalloc page tables are pre-allocated.
 * For non-KMSAN builds, this is the same as VMALLOC_END.
 * For KMSAN builds, VMALLOC_START..VMEMORY_END is 4 times bigger than
 * VMALLOC_START..VMALLOC_END (see below).
 */
#define VMEMORY_END		(VMALLOC_START + (VMALLOC_SIZE_TB << 40) - 1)

#ifndef CONFIG_KMSAN
#define VMALLOC_END		VMEMORY_END
#else
/*
 * In KMSAN builds vmalloc area is four times smaller, and the remaining 3/4
 * are used to keep the metadata for virtual pages. The memory formerly
 * belonging to vmalloc area is now laid out as follows:
 *
 * 1st quarter: VMALLOC_START to VMALLOC_END - new vmalloc area
 * 2nd quarter: KMSAN_VMALLOC_SHADOW_START to
 *              VMALLOC_END+KMSAN_VMALLOC_SHADOW_OFFSET - vmalloc area shadow
 * 3rd quarter: KMSAN_VMALLOC_ORIGIN_START to
 *              VMALLOC_END+KMSAN_VMALLOC_ORIGIN_OFFSET - vmalloc area origins
 * 4th quarter: KMSAN_MODULES_SHADOW_START to KMSAN_MODULES_ORIGIN_START
 *              - shadow for modules,
 *              KMSAN_MODULES_ORIGIN_START to
 *              KMSAN_MODULES_ORIGIN_START + MODULES_LEN - origins for modules.
 */
#define VMALLOC_QUARTER_SIZE	((VMALLOC_SIZE_TB << 40) >> 2)
#define VMALLOC_END		(VMALLOC_START + VMALLOC_QUARTER_SIZE - 1)

/*
 * vmalloc metadata addresses are calculated by adding shadow/origin offsets
 * to vmalloc address.
 */
#define KMSAN_VMALLOC_SHADOW_OFFSET	VMALLOC_QUARTER_SIZE
#define KMSAN_VMALLOC_ORIGIN_OFFSET	(VMALLOC_QUARTER_SIZE << 1)

#define KMSAN_VMALLOC_SHADOW_START	(VMALLOC_START + KMSAN_VMALLOC_SHADOW_OFFSET)
#define KMSAN_VMALLOC_ORIGIN_START	(VMALLOC_START + KMSAN_VMALLOC_ORIGIN_OFFSET)

/*
 * The shadow/origin for modules are placed one by one in the last 1/4 of
 * vmalloc space.
 */
#define KMSAN_MODULES_SHADOW_START	(VMALLOC_END + KMSAN_VMALLOC_ORIGIN_OFFSET + 1)
#define KMSAN_MODULES_ORIGIN_START	(KMSAN_MODULES_SHADOW_START + MODULES_LEN)
#endif /* CONFIG_KMSAN */

#define MODULES_VADDR		(__START_KERNEL_map + KERNEL_IMAGE_SIZE)
/* The module sections ends with the start of the fixmap */
#ifndef CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP
# define MODULES_END		_AC(0xffffffffff000000, UL)
#else
# define MODULES_END		_AC(0xfffffffffe000000, UL)
#endif
#define MODULES_LEN		(MODULES_END - MODULES_VADDR)

#define ESPFIX_PGD_ENTRY	_AC(-2, UL)
#define ESPFIX_BASE_ADDR	(ESPFIX_PGD_ENTRY << P4D_SHIFT)

#define CPU_ENTRY_AREA_PGD	_AC(-4, UL)
#define CPU_ENTRY_AREA_BASE	(CPU_ENTRY_AREA_PGD << P4D_SHIFT)

#define EFI_VA_START		( -4 * (_AC(1, UL) << 30))
#define EFI_VA_END		(-68 * (_AC(1, UL) << 30))

#define EARLY_DYNAMIC_PAGE_TABLES	64

#define PGD_KERNEL_START	((PAGE_SIZE / 2) / sizeof(pgd_t))

/*
 * We borrow bit 3 to remember PG_anon_exclusive.
 */
#define _PAGE_SWP_EXCLUSIVE	_PAGE_PWT

#endif /* _ASM_X86_PGTABLE_64_DEFS_H */
