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

#define _PAGE_SHIFT	CONFIG_PAGE_SHIFT
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
struct folio;
void arch_free_page(struct page *page, int order);
void arch_alloc_page(struct page *page, int order);

static inline int devmem_is_allowed(unsigned long pfn)
{
	return 0;
}

#define HAVE_ARCH_FREE_PAGE
#define HAVE_ARCH_ALLOC_PAGE

int arch_make_folio_accessible(struct folio *folio);
#define HAVE_ARCH_MAKE_FOLIO_ACCESSIBLE

struct vm_layout {
	unsigned long kaslr_offset;
	unsigned long kaslr_offset_phys;
	unsigned long identity_base;
	unsigned long identity_size;
};

extern struct vm_layout vm_layout;

#define __kaslr_offset		vm_layout.kaslr_offset
#define __kaslr_offset_phys	vm_layout.kaslr_offset_phys
#define __identity_base		vm_layout.identity_base
#define ident_map_size		vm_layout.identity_size

static inline unsigned long kaslr_offset(void)
{
	return __kaslr_offset;
}

extern int __kaslr_enabled;
static inline int kaslr_enabled(void)
{
	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE))
		return __kaslr_enabled;
	return 0;
}

#define __PAGE_OFFSET		__identity_base
#define PAGE_OFFSET		__PAGE_OFFSET

#ifdef __DECOMPRESSOR

#define __pa_nodebug(x)		((unsigned long)(x))
#define __pa(x)			__pa_nodebug(x)
#define __pa32(x)		__pa(x)
#define __va(x)			((void *)(unsigned long)(x))

#else /* __DECOMPRESSOR */

static inline unsigned long __pa_nodebug(unsigned long x)
{
	if (x < __kaslr_offset)
		return x - __identity_base;
	return x - __kaslr_offset + __kaslr_offset_phys;
}

#ifdef CONFIG_DEBUG_VIRTUAL

unsigned long __phys_addr(unsigned long x, bool is_31bit);

#else /* CONFIG_DEBUG_VIRTUAL */

static inline unsigned long __phys_addr(unsigned long x, bool is_31bit)
{
	return __pa_nodebug(x);
}

#endif /* CONFIG_DEBUG_VIRTUAL */

#define __pa(x)			__phys_addr((unsigned long)(x), false)
#define __pa32(x)		__phys_addr((unsigned long)(x), true)
#define __va(x)			((void *)((unsigned long)(x) + __identity_base))

#endif /* __DECOMPRESSOR */

#define phys_to_pfn(phys)	((phys) >> PAGE_SHIFT)
#define pfn_to_phys(pfn)	((pfn) << PAGE_SHIFT)

#define phys_to_page(phys)	pfn_to_page(phys_to_pfn(phys))
#define phys_to_folio(phys)	page_folio(phys_to_page(phys))
#define page_to_phys(page)	pfn_to_phys(page_to_pfn(page))
#define folio_to_phys(page)	pfn_to_phys(folio_pfn(folio))

static inline void *pfn_to_virt(unsigned long pfn)
{
	return __va(pfn_to_phys(pfn));
}

static inline unsigned long virt_to_pfn(const void *kaddr)
{
	return phys_to_pfn(__pa(kaddr));
}

#define pfn_to_kaddr(pfn)	pfn_to_virt(pfn)

#define virt_to_page(kaddr)	pfn_to_page(virt_to_pfn(kaddr))
#define page_to_virt(page)	pfn_to_virt(page_to_pfn(page))

#define virt_addr_valid(kaddr)	pfn_valid(phys_to_pfn(__pa_nodebug((unsigned long)(kaddr))))

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_NON_EXEC

#endif /* !__ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#define AMODE31_SIZE		(3 * PAGE_SIZE)

#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)
#define __NO_KASLR_START_KERNEL	CONFIG_KERNEL_IMAGE_BASE
#define __NO_KASLR_END_KERNEL	(__NO_KASLR_START_KERNEL + KERNEL_IMAGE_SIZE)

#define TEXT_OFFSET		0x100000

#endif /* _S390_PAGE_H */
