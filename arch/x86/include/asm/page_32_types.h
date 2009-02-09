#ifndef _ASM_X86_PAGE_32_DEFS_H
#define _ASM_X86_PAGE_32_DEFS_H

#include <linux/const.h>

/*
 * This handles the memory map.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB.
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 */
#define __PAGE_OFFSET		_AC(CONFIG_PAGE_OFFSET, UL)

#ifdef CONFIG_4KSTACKS
#define THREAD_ORDER	0
#else
#define THREAD_ORDER	1
#endif
#define THREAD_SIZE 	(PAGE_SIZE << THREAD_ORDER)

#define STACKFAULT_STACK 0
#define DOUBLEFAULT_STACK 1
#define NMI_STACK 0
#define DEBUG_STACK 0
#define MCE_STACK 0
#define N_EXCEPTION_STACKS 1

#ifdef CONFIG_X86_PAE
/* 44=32+12, the limit we can fit into an unsigned long pfn */
#define __PHYSICAL_MASK_SHIFT	44
#define __VIRTUAL_MASK_SHIFT	32
#define PAGETABLE_LEVELS	3

#else  /* !CONFIG_X86_PAE */
#define __PHYSICAL_MASK_SHIFT	32
#define __VIRTUAL_MASK_SHIFT	32
#define PAGETABLE_LEVELS	2
#endif	/* CONFIG_X86_PAE */

#ifndef __ASSEMBLY__

#include <linux/types.h>

#ifdef CONFIG_X86_PAE
typedef u64	pteval_t;
typedef u64	pmdval_t;
typedef u64	pudval_t;
typedef u64	pgdval_t;
typedef u64	pgprotval_t;

typedef union {
	struct {
		unsigned long pte_low, pte_high;
	};
	pteval_t pte;
} pte_t;
#else  /* !CONFIG_X86_PAE */
typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;

typedef union {
	pteval_t pte;
	pteval_t pte_low;
} pte_t;
#endif	/* CONFIG_X86_PAE */

extern int nx_enabled;

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;
extern int sysctl_legacy_va_layout;

extern void find_low_pfn_range(void);
extern unsigned long init_memory_mapping(unsigned long start,
					 unsigned long end);
extern void initmem_init(unsigned long, unsigned long);
extern void free_initmem(void);
extern void setup_bootmem_allocator(void);

#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_X86_PAGE_32_DEFS_H */
