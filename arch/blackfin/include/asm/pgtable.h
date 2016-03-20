/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_PGTABLE_H
#define _BLACKFIN_PGTABLE_H

#include <asm-generic/4level-fixup.h>

#include <asm/page.h>
#include <asm/def_LPBlackfin.h>

typedef pte_t *pte_addr_t;
/*
* Trivial page table functions.
*/
#define pgd_present(pgd)	(1)
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr)	(1)

#define pmd_offset(a, b)	((void *)0)
#define pmd_none(x)		(!pmd_val(x))
#define pmd_present(x)		(pmd_val(x))
#define pmd_clear(xp)		do { set_pmd(xp, __pmd(0)); } while (0)
#define pmd_bad(x)		(pmd_val(x) & ~PAGE_MASK)

#define kern_addr_valid(addr) (1)

#define PAGE_NONE		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_SHARED		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_COPY		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_READONLY		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_KERNEL		__pgprot(0)	/* these mean nothing to NO_MM */
#define pgprot_noncached(prot)	(prot)

extern void paging_init(void);

#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ,off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#define set_pte(pteptr, pteval) (*(pteptr) = pteval)
#define set_pte_at(mm, addr, ptep, pteval) set_pte(ptep, pteval)

/*
 * Page assess control based on Blackfin CPLB management
 */
#define _PAGE_RD	(CPLB_USER_RD)
#define _PAGE_WR	(CPLB_USER_WR)
#define _PAGE_USER	(CPLB_USER_RD | CPLB_USER_WR)
#define _PAGE_ACCESSED	CPLB_ALL_ACCESS
#define _PAGE_DIRTY	(CPLB_DIRTY)

#define PTE_BIT_FUNC(fn, op) \
	static inline pte_t pte_##fn(pte_t _pte) { _pte.pte op; return _pte; }

PTE_BIT_FUNC(rdprotect, &= ~_PAGE_RD);
PTE_BIT_FUNC(mkread, |= _PAGE_RD);
PTE_BIT_FUNC(wrprotect, &= ~_PAGE_WR);
PTE_BIT_FUNC(mkwrite, |= _PAGE_WR);
PTE_BIT_FUNC(exprotect, &= ~_PAGE_USER);
PTE_BIT_FUNC(mkexec, |= _PAGE_USER);
PTE_BIT_FUNC(mkclean, &= ~_PAGE_DIRTY);
PTE_BIT_FUNC(mkdirty, |= _PAGE_DIRTY);
PTE_BIT_FUNC(mkold, &= ~_PAGE_ACCESSED);
PTE_BIT_FUNC(mkyoung, |= _PAGE_ACCESSED);

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)	virt_to_page(empty_zero_page)
extern char empty_zero_page[];

#define swapper_pg_dir ((pgd_t *) 0)
/*
 * No page table caches to initialise.
 */
#define pgtable_cache_init()	do { } while (0)

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0
#define	VMALLOC_END	0xffffffff

/* provide a special get_unmapped_area for framebuffer mmaps of nommu */
extern unsigned long get_fb_unmapped_area(struct file *filp, unsigned long,
					  unsigned long, unsigned long,
					  unsigned long);
#define HAVE_ARCH_FB_UNMAPPED_AREA

#define pgprot_writecombine pgprot_noncached

#include <asm-generic/pgtable.h>

#endif				/* _BLACKFIN_PGTABLE_H */
