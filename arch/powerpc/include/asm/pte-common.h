/* SPDX-License-Identifier: GPL-2.0 */
/* Included from asm/pgtable-*.h only ! */

/*
 * Some bits are only used on some cpu families... Make sure that all
 * the undefined gets a sensible default
 */
#ifndef _PAGE_HWWRITE
#define _PAGE_HWWRITE	0
#endif
#ifndef _PAGE_COHERENT
#define _PAGE_COHERENT	0
#endif
#ifndef _PAGE_WRITETHRU
#define _PAGE_WRITETHRU	0
#endif
/* _PAGE_RO and _PAGE_RW shall not be defined at the same time */
#ifndef _PAGE_RO
#define _PAGE_RO 0
#else
#define _PAGE_RW 0
#endif

/* At least one of _PAGE_PRIVILEGED or _PAGE_USER must be defined */
#ifndef _PAGE_PRIVILEGED
#define _PAGE_PRIVILEGED 0
#else
#ifndef _PAGE_USER
#define _PAGE_USER 0
#endif
#endif
#ifndef _PAGE_NA
#define _PAGE_NA 0
#endif
#ifndef _PAGE_HUGE
#define _PAGE_HUGE 0
#endif

/* Location of the PFN in the PTE. Most 32-bit platforms use the same
 * as _PAGE_SHIFT here (ie, naturally aligned).
 * Platform who don't just pre-define the value so we don't override it here
 */
#ifndef PTE_RPN_SHIFT
#define PTE_RPN_SHIFT	(PAGE_SHIFT)
#endif

/* The mask covered by the RPN must be a ULL on 32-bit platforms with
 * 64-bit PTEs
 */
#if defined(CONFIG_PPC32) && defined(CONFIG_PTE_64BIT)
#define PTE_RPN_MASK	(~((1ULL<<PTE_RPN_SHIFT)-1))
#else
#define PTE_RPN_MASK	(~((1UL<<PTE_RPN_SHIFT)-1))
#endif

/* _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_DIRTY | \
                         _PAGE_ACCESSED | _PAGE_SPECIAL)
