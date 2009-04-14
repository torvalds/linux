/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PGTABLE_H
#define _ASM_MICROBLAZE_PGTABLE_H

#include <asm/setup.h>

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)

#define pgd_present(pgd)	(1) /* pages are always present on non MMU */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr)	(1)
#define	pmd_offset(a, b)	((void *) 0)

#define PAGE_NONE		__pgprot(0) /* these mean nothing to non MMU */
#define PAGE_SHARED		__pgprot(0) /* these mean nothing to non MMU */
#define PAGE_COPY		__pgprot(0) /* these mean nothing to non MMU */
#define PAGE_READONLY		__pgprot(0) /* these mean nothing to non MMU */
#define PAGE_KERNEL		__pgprot(0) /* these mean nothing to non MMU */

#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ, off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#ifndef __ASSEMBLY__
static inline int pte_file(pte_t pte) { return 0; }
#endif /* __ASSEMBLY__ */

#define ZERO_PAGE(vaddr)	({ BUG(); NULL; })

#define swapper_pg_dir ((pgd_t *) NULL)

#define pgtable_cache_init()	do {} while (0)

#define arch_enter_lazy_cpu_mode()	do {} while (0)

#ifndef __ASSEMBLY__
#include <asm-generic/pgtable.h>

void setup_memory(void);
#endif /* __ASSEMBLY__ */

#endif /* _ASM_MICROBLAZE_PGTABLE_H */
