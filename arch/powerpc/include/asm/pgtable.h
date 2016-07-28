#ifndef _ASM_POWERPC_PGTABLE_H
#define _ASM_POWERPC_PGTABLE_H

#ifndef __ASSEMBLY__
#include <linux/mmdebug.h>
#include <linux/mmzone.h>
#include <asm/processor.h>		/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>

struct mm_struct;

#endif /* !__ASSEMBLY__ */

#ifdef CONFIG_PPC_BOOK3S
#include <asm/book3s/pgtable.h>
#else
#include <asm/nohash/pgtable.h>
#endif /* !CONFIG_PPC_BOOK3S */

#ifndef __ASSEMBLY__

#include <asm/tlbflush.h>

/* Keep these as a macros to avoid include dependency mess */
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

extern pgd_t swapper_pg_dir[];

void limit_zone_pfn(enum zone_type zone, unsigned long max_pfn);
int dma_pfn_limit_to_zone(u64 pfn_limit);
extern void paging_init(void);

/*
 * kern_addr_valid is intended to indicate whether an address is a valid
 * kernel address.  Most 32-bit archs define it as always true (like this)
 * but most 64-bit archs actually perform a test.  What should we do here?
 */
#define kern_addr_valid(addr)	(1)

#include <asm-generic/pgtable.h>


/*
 * This gets called at the end of handling a page fault, when
 * the kernel has put a new PTE into the page table for the process.
 * We use it to ensure coherency between the i-cache and d-cache
 * for the page which has just been mapped in.
 * On machines which use an MMU hash table, we use this to put a
 * corresponding HPTE into the hash table ahead of time, instead of
 * waiting for the inevitable extra hash-table miss exception.
 */
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t *);

extern int gup_hugepte(pte_t *ptep, unsigned long sz, unsigned long addr,
		       unsigned long end, int write,
		       struct page **pages, int *nr);
#ifndef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_large(pmd)		0
#endif
pte_t *__find_linux_pte_or_hugepte(pgd_t *pgdir, unsigned long ea,
				   bool *is_thp, unsigned *shift);
static inline pte_t *find_linux_pte_or_hugepte(pgd_t *pgdir, unsigned long ea,
					       bool *is_thp, unsigned *shift)
{
	if (!arch_irqs_disabled()) {
		pr_info("%s called with irq enabled\n", __func__);
		dump_stack();
	}
	return __find_linux_pte_or_hugepte(pgdir, ea, is_thp, shift);
}

unsigned long vmalloc_to_phys(void *vmalloc_addr);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_PGTABLE_H */
