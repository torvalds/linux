#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <linux/threads.h>
#include <asm/paravirt.h>

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct mm_struct;
struct vm_area_struct;

extern pgd_t swapper_pg_dir[1024];

static inline void pgtable_cache_init(void) { }
static inline void check_pgt_cache(void) { }
void paging_init(void);

extern void set_pmd_pfn(unsigned long, unsigned long, pgprot_t);


/*
 * Define this if things work differently on an i386 and an i486:
 * it will (on an i486) warn about kernel memory accesses that are
 * done without a 'access_ok(VERIFY_WRITE,..)'
 */
#undef TEST_ACCESS_OK

#ifdef CONFIG_X86_PAE
# include <asm/pgtable-3level.h>
#else
# include <asm/pgtable-2level.h>
#endif

#if defined(CONFIG_HIGHPTE)
#define __KM_PTE			\
	(in_nmi() ? KM_NMI_PTE : 	\
	 in_irq() ? KM_IRQ_PTE :	\
	 KM_PTE0)
#define pte_offset_map(dir, address)					\
	((pte_t *)kmap_atomic(pmd_page(*(dir)), __KM_PTE) +		\
	 pte_index((address)))
#define pte_offset_map_nested(dir, address)				\
	((pte_t *)kmap_atomic(pmd_page(*(dir)), KM_PTE1) +		\
	 pte_index((address)))
#define pte_unmap(pte) kunmap_atomic((pte), __KM_PTE)
#define pte_unmap_nested(pte) kunmap_atomic((pte), KM_PTE1)
#else
#define pte_offset_map(dir, address)					\
	((pte_t *)page_address(pmd_page(*(dir))) + pte_index((address)))
#define pte_offset_map_nested(dir, address) pte_offset_map((dir), (address))
#define pte_unmap(pte) do { } while (0)
#define pte_unmap_nested(pte) do { } while (0)
#endif

/* Clear a kernel PTE and flush it from the TLB */
#define kpte_clear_flush(ptep, vaddr)		\
do {						\
	pte_clear(&init_mm, (vaddr), (ptep));	\
	__flush_tlb_one((vaddr));		\
} while (0)

/*
 * The i386 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
#define update_mmu_cache(vma, address, ptep) do { } while (0)

#endif /* !__ASSEMBLY__ */

/*
 * kern_addr_valid() is (1) for FLATMEM and (0) for
 * SPARSEMEM and DISCONTIGMEM
 */
#ifdef CONFIG_FLATMEM
#define kern_addr_valid(addr)	(1)
#else
#define kern_addr_valid(kaddr)	(0)
#endif

#endif /* _ASM_X86_PGTABLE_32_H */
