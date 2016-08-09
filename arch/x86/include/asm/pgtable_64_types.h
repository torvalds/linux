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
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;

typedef struct { pteval_t pte; } pte_t;

#endif	/* !__ASSEMBLY__ */

#define SHARED_KERNEL_PMD	0

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	39
#define PTRS_PER_PGD	512

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

/* See Documentation/x86/x86_64/mm.txt for a description of the memory map. */
#define MAXMEM		_AC(__AC(1, UL) << MAX_PHYSMEM_BITS, UL)
#define VMALLOC_SIZE_TB	_AC(32, UL)
#define __VMALLOC_BASE	_AC(0xffffc90000000000, UL)
#define VMEMMAP_START	_AC(0xffffea0000000000, UL)
#ifdef CONFIG_RANDOMIZE_MEMORY
#define VMALLOC_START	vmalloc_base
#else
#define VMALLOC_START	__VMALLOC_BASE
#endif /* CONFIG_RANDOMIZE_MEMORY */
#define VMALLOC_END	(VMALLOC_START + _AC((VMALLOC_SIZE_TB << 40) - 1, UL))
#define MODULES_VADDR    (__START_KERNEL_map + KERNEL_IMAGE_SIZE)
#define MODULES_END      _AC(0xffffffffff000000, UL)
#define MODULES_LEN   (MODULES_END - MODULES_VADDR)
#define ESPFIX_PGD_ENTRY _AC(-2, UL)
#define ESPFIX_BASE_ADDR (ESPFIX_PGD_ENTRY << PGDIR_SHIFT)
#define EFI_VA_START	 ( -4 * (_AC(1, UL) << 30))
#define EFI_VA_END	 (-68 * (_AC(1, UL) << 30))

#define EARLY_DYNAMIC_PAGE_TABLES	64

#endif /* _ASM_X86_PGTABLE_64_DEFS_H */
