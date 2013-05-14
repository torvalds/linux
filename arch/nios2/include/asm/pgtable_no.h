/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * Based on asm/pgtable_no.h from m68k which is:
 *
 * Copyright (C) 2000-2002 Greg Ungerer <gerg@snapgear.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PGTABLE_NO_H
#define _ASM_NIOS2_PGTABLE_NO_H

#include <asm-generic/4level-fixup.h>

#include <linux/io.h>

typedef pte_t *pte_addr_t;

#define pgd_present(pgd)	(1)	/* pages are always present on NO_MM */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr)	(1)
#define pmd_offset(a, b)	((void *)0)

#define PAGE_NONE		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_SHARED		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_COPY		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_READONLY		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_KERNEL		__pgprot(0)    /* these mean nothing to NO_MM */

extern void paging_init(void);
#define swapper_pg_dir ((pgd_t *) 0)

#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ, off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

static inline int pte_file(pte_t pte) { return 0; }

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)	(virt_to_page(0))

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()   do { } while (0)

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0
#define	VMALLOC_END	0xffffffff

#define arch_enter_lazy_mmu_mode()	do {} while (0)
#define arch_leave_lazy_mmu_mode()	do {} while (0)
#define arch_flush_lazy_mmu_mode()	do {} while (0)
#define arch_enter_lazy_cpu_mode()	do {} while (0)
#define arch_leave_lazy_cpu_mode()	do {} while (0)
#define arch_flush_lazy_cpu_mode()	do {} while (0)

#include <asm-generic/pgtable.h>

/* We provide a special get_unmapped_area for framebuffer mmaps of nommu */
extern unsigned long get_fb_unmapped_area(struct file *filp, unsigned long,
					  unsigned long, unsigned long,
					  unsigned long);
#define HAVE_ARCH_FB_UNMAPPED_AREA

#endif /* _ASM_NIOS2_PGTABLE_NO_H */
