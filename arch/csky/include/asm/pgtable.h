/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PGTABLE_H
#define __ASM_CSKY_PGTABLE_H

#include <asm/fixmap.h>
#include <asm/memory.h>
#include <asm/addrspace.h>
#include <abi/pgtable-bits.h>
#include <asm-generic/pgtable-nopmd.h>

#define PGDIR_SHIFT		22
#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

#define USER_PTRS_PER_PGD	(PAGE_OFFSET/PGDIR_SIZE)

/*
 * C-SKY is two-level paging structure:
 */

#define PTRS_PER_PGD	(PAGE_SIZE / sizeof(pgd_t))
#define PTRS_PER_PMD	1
#define PTRS_PER_PTE	(PAGE_SIZE / sizeof(pte_t))

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, (e).pte_low)
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

#define PFN_PTE_SHIFT	PAGE_SHIFT
#define pmd_pfn(pmd)	(pmd_phys(pmd) >> PAGE_SHIFT)
#define pmd_page(pmd)	(pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT))
#define pte_clear(mm, addr, ptep)	set_pte((ptep), \
	(((unsigned int) addr >= PAGE_OFFSET) ? __pte(_PAGE_GLOBAL) : __pte(0)))
#define pte_none(pte)		(!(pte_val(pte) & ~_PAGE_GLOBAL))
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_pfn(x)	((unsigned long)((x).pte_low >> PAGE_SHIFT))
#define pfn_pte(pfn, prot) __pte(((unsigned long long)(pfn) << PAGE_SHIFT) \
				| pgprot_val(prot))

#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define pte_page(x)			pfn_to_page(pte_pfn(x))
#define __mk_pte(page_nr, pgprot)	__pte(((page_nr) << PAGE_SHIFT) | \
					pgprot_val(pgprot))

/*
 * C-SKY only has VALID and DIRTY bit in hardware. So we need to use the
 * two bits emulate PRESENT, READ, WRITE, EXEC, MODIFIED, ACCESSED.
 */
#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED)

#define PAGE_NONE	__pgprot(_PAGE_PROT_NONE)
#define PAGE_READ	__pgprot(_PAGE_BASE | _PAGE_READ | \
				_CACHE_CACHED)
#define PAGE_WRITE	__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_WRITE | \
				_CACHE_CACHED)
#define PAGE_SHARED PAGE_WRITE

#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_VALID | \
				_PAGE_WRITE | _PAGE_DIRTY | _PAGE_MODIFIED | \
				_PAGE_GLOBAL | \
				_CACHE_CACHED)

#define _PAGE_IOREMAP		(_PAGE_BASE | _PAGE_READ | _PAGE_VALID | \
				_PAGE_WRITE | _PAGE_DIRTY | _PAGE_MODIFIED | \
				_PAGE_GLOBAL | \
				_CACHE_UNCACHED | _PAGE_SO)

#define _PAGE_CHG_MASK	(~(unsigned long) \
				(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				_CACHE_MASK | _PAGE_GLOBAL))

#define MAX_SWAPFILES_CHECK() \
		BUILD_BUG_ON(MAX_SWAPFILES_SHIFT != 5)

extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

extern void load_pgd(unsigned long pg_dir);
extern pte_t invalid_pte_table[PTRS_PER_PTE];

static inline void set_pte(pte_t *p, pte_t pte)
{
	*p = pte;
#if defined(CONFIG_CPU_NEED_TLBSYNC)
	dcache_wb_line((u32)p);
#endif
	/* prevent out of order excution */
	smp_mb();
}

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	unsigned long ptr;

	ptr = pmd_val(pmd);

	return __va(ptr);
}

#define pmd_phys(pmd) pmd_val(pmd)

static inline void set_pmd(pmd_t *p, pmd_t pmd)
{
	*p = pmd;
#if defined(CONFIG_CPU_NEED_TLBSYNC)
	dcache_wb_line((u32)p);
#endif
	/* prevent specul excute */
	smp_mb();
}


static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == __pa(invalid_pte_table);
}

#define pmd_bad(pmd)	(pmd_val(pmd) & ~PAGE_MASK)

static inline int pmd_present(pmd_t pmd)
{
	return (pmd_val(pmd) != __pa(invalid_pte_table));
}

static inline void pmd_clear(pmd_t *p)
{
	pmd_val(*p) = (__pa(invalid_pte_table));
#if defined(CONFIG_CPU_NEED_TLBSYNC)
	dcache_wb_line((u32)p);
#endif
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)
{
	return pte.pte_low & _PAGE_READ;
}

static inline int pte_write(pte_t pte)
{
	return (pte).pte_low & _PAGE_WRITE;
}

static inline int pte_dirty(pte_t pte)
{
	return (pte).pte_low & _PAGE_MODIFIED;
}

static inline int pte_young(pte_t pte)
{
	return (pte).pte_low & _PAGE_ACCESSED;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_MODIFIED|_PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED|_PAGE_VALID);
	return pte;
}

static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	if (pte_val(pte) & _PAGE_READ)
		pte_val(pte) |= _PAGE_VALID;
	return pte;
}

static inline bool pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	pte_val(pte) |= _PAGE_SWP_EXCLUSIVE;
	return pte;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_SWP_EXCLUSIVE;
	return pte;
}

#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);

/*
 * Macro to make mark a page protection value as "uncacheable".  Note
 * that "protection" is really a misnomer here as the protection value
 * contains the memory attribute bits, dirty bits, and various other
 * bits as well.
 */
#define pgprot_noncached pgprot_noncached

static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED | _PAGE_SO;

	return __pgprot(prot);
}

#define pgprot_writecombine pgprot_writecombine
static inline pgprot_t pgprot_writecombine(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;

	return __pgprot(prot);
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) |
		     (pgprot_val(newprot)));
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void paging_init(void);

void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
		unsigned long address, pte_t *pte, unsigned int nr);
#define update_mmu_cache(vma, addr, ptep) \
	update_mmu_cache_range(NULL, vma, addr, ptep, 1)

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot) \
	remap_pfn_range(vma, vaddr, pfn, size, prot)

#endif /* __ASM_CSKY_PGTABLE_H */
