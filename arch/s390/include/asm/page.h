/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 */

#ifndef _S390_PAGE_H
#define _S390_PAGE_H

#include <linux/const.h>
#include <asm/types.h>

#define _PAGE_SHIFT	12
#define _PAGE_SIZE	(_AC(1, UL) << _PAGE_SHIFT)
#define _PAGE_MASK	(~(_PAGE_SIZE - 1))

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	_PAGE_SHIFT
#define PAGE_SIZE	_PAGE_SIZE
#define PAGE_MASK	_PAGE_MASK
#define PAGE_DEFAULT_ACC	_AC(0, UL)
/* storage-protection override */
#define PAGE_SPO_ACC		9
#define PAGE_DEFAULT_KEY	(PAGE_DEFAULT_ACC << 4)

#define HPAGE_SHIFT	20
#define HPAGE_SIZE	(1UL << HPAGE_SHIFT)
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#define HUGE_MAX_HSTATE		2

#define ARCH_HAS_SETCLEAR_HUGE_PTE
#define ARCH_HAS_HUGE_PTE_TYPE
#define ARCH_HAS_PREPARE_HUGEPAGE
#define ARCH_HAS_HUGEPAGE_CLEAR_FLUSH

#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA

#include <asm/setup.h>
#ifndef __ASSEMBLY__

void __storage_key_init_range(unsigned long start, unsigned long end);

static inline void storage_key_init_range(unsigned long start, unsigned long end)
{
	if (PAGE_DEFAULT_KEY != 0)
		__storage_key_init_range(start, end);
}

#define clear_page(page)	memset((page), 0, PAGE_SIZE)

/*
 * copy_page uses the mvcl instruction with 0xb0 padding byte in order to
 * bypass caches when copying a page. Especially when copying huge pages
 * this keeps L1 and L2 data caches alive.
 */
static inline void copy_page(void *to, void *from)
{
	union register_pair dst, src;

	dst.even = (unsigned long) to;
	dst.odd  = 0x1000;
	src.even = (unsigned long) from;
	src.odd  = 0xb0001000;

	asm volatile(
		"	mvcl	%[dst],%[src]"
		: [dst] "+&d" (dst.pair), [src] "+&d" (src.pair)
		: : "memory", "cc");
}

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

#define vma_alloc_zeroed_movable_folio(vma, vaddr) \
	vma_alloc_folio(GFP_HIGHUSER_MOVABLE | __GFP_ZERO, 0, vma, vaddr, false)

/*
 * These are used to make use of C type-checking..
 */

typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long pgste; } pgste_t;
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pud; } pud_t;
typedef struct { unsigned long p4d; } p4d_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef pte_t *pgtable_t;

#define pgprot_val(x)	((x).pgprot)
#define pgste_val(x)	((x).pgste)

static inline unsigned long pte_val(pte_t pte)
{
	return pte.pte;
}

static inline unsigned long pmd_val(pmd_t pmd)
{
	return pmd.pmd;
}

static inline unsigned long pud_val(pud_t pud)
{
	return pud.pud;
}

static inline unsigned long p4d_val(p4d_t p4d)
{
	return p4d.p4d;
}

static inline unsigned long pgd_val(pgd_t pgd)
{
	return pgd.pgd;
}

#define __pgste(x)	((pgste_t) { (x) } )
#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pud(x)	((pud_t) { (x) } )
#define __p4d(x)	((p4d_t) { (x) } )
#define __pgd(x)        ((pgd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

static inline void page_set_storage_key(unsigned long addr,
					unsigned char skey, int mapped)
{
	if (!mapped)
		asm volatile(".insn rrf,0xb22b0000,%0,%1,8,0"
			     : : "d" (skey), "a" (addr));
	else
		asm volatile("sske %0,%1" : : "d" (skey), "a" (addr));
}

static inline unsigned char page_get_storage_key(unsigned long addr)
{
	unsigned char skey;

	asm volatile("iske %0,%1" : "=d" (skey) : "a" (addr));
	return skey;
}

static inline int page_reset_referenced(unsigned long addr)
{
	int cc;

	asm volatile(
		"	rrbe	0,%1\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc) : "a" (addr) : "cc");
	return cc;
}

/* Bits int the storage key */
#define _PAGE_CHANGED		0x02	/* HW changed bit		*/
#define _PAGE_REFERENCED	0x04	/* HW referenced bit		*/
#define _PAGE_FP_BIT		0x08	/* HW fetch protection bit	*/
#define _PAGE_ACC_BITS		0xf0	/* HW access control bits	*/

struct page;
void arch_free_page(struct page *page, int order);
void arch_alloc_page(struct page *page, int order);
void arch_set_page_dat(struct page *page, int order);

static inline int devmem_is_allowed(unsigned long pfn)
{
	return 0;
}

#define HAVE_ARCH_FREE_PAGE
#define HAVE_ARCH_ALLOC_PAGE

#if IS_ENABLED(CONFIG_PGSTE)
int arch_make_page_accessible(struct page *page);
#define HAVE_ARCH_MAKE_PAGE_ACCESSIBLE
#endif

#define __PAGE_OFFSET		0x0UL
#define PAGE_OFFSET		0x0UL

#define __pa(x)			((unsigned long)(x))
#define __va(x)			((void *)(unsigned long)(x))

#define phys_to_pfn(phys)	((phys) >> PAGE_SHIFT)
#define pfn_to_phys(pfn)	((pfn) << PAGE_SHIFT)

#define phys_to_page(phys)	pfn_to_page(phys_to_pfn(phys))
#define page_to_phys(page)	pfn_to_phys(page_to_pfn(page))

#define pfn_to_virt(pfn)	__va(pfn_to_phys(pfn))
#define virt_to_pfn(kaddr)	(phys_to_pfn(__pa(kaddr)))
#define pfn_to_kaddr(pfn)	pfn_to_virt(pfn)

#define virt_to_page(kaddr)	pfn_to_page(virt_to_pfn(kaddr))
#define page_to_virt(page)	pfn_to_virt(page_to_pfn(page))

#define virt_addr_valid(kaddr)	pfn_valid(virt_to_pfn(kaddr))

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_NON_EXEC

#endif /* !__ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _S390_PAGE_H */
