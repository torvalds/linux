/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H8300_PGTABLE_H
#define _H8300_PGTABLE_H
#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopud.h>
#include <asm-generic/pgtable.h>
#define pgtable_cache_init()   do { } while (0)
extern void paging_init(void);
#define PAGE_NONE		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_SHARED		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_COPY		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_READONLY	__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_KERNEL		__pgprot(0)    /* these mean nothing to NO_MM */
#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ, off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })
#define kern_addr_valid(addr)	(1)
#define pgprot_writecombine(prot)  (prot)
#define pgprot_noncached pgprot_writecombine

static inline int pte_file(pte_t pte) { return 0; }
#define swapper_pg_dir ((pgd_t *) 0)
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)	(virt_to_page(0))

/*
 * These would be in other places but having them here reduces the diffs.
 */
extern unsigned int kobjsize(const void *objp);
extern int is_in_rom(unsigned long);

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()   do { } while (0)

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0
#define	VMALLOC_END	0xffffffff

#define arch_enter_lazy_cpu_mode()    do {} while (0)

#endif /* _H8300_PGTABLE_H */
