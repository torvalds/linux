/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_PTE_E500_H
#define _ASM_POWERPC_NOHASH_PTE_E500_H
#ifdef __KERNEL__

/* PTE bit definitions for processors compliant to the Book3E
 * architecture 2.06 or later. The position of the PTE bits
 * matches the HW definition of the optional Embedded Page Table
 * category.
 */

/* Architected bits */
#define _PAGE_PRESENT	0x000001 /* software: pte contains a translation */
#define _PAGE_SW1	0x000002
#define _PAGE_BAP_SR	0x000004
#define _PAGE_BAP_UR	0x000008
#define _PAGE_BAP_SW	0x000010
#define _PAGE_BAP_UW	0x000020
#define _PAGE_BAP_SX	0x000040
#define _PAGE_BAP_UX	0x000080
#define _PAGE_PSIZE_MSK	0x000f00
#define _PAGE_TSIZE_4K	0x000100
#define _PAGE_DIRTY	0x001000 /* C: page changed */
#define _PAGE_SW0	0x002000
#define _PAGE_U3	0x004000
#define _PAGE_U2	0x008000
#define _PAGE_U1	0x010000
#define _PAGE_U0	0x020000
#define _PAGE_ACCESSED	0x040000
#define _PAGE_ENDIAN	0x080000
#define _PAGE_GUARDED	0x100000
#define _PAGE_COHERENT	0x200000 /* M: enforce memory coherence */
#define _PAGE_NO_CACHE	0x400000 /* I: cache inhibit */
#define _PAGE_WRITETHRU	0x800000 /* W: cache write-through */

#define _PAGE_PSIZE_SHIFT		7
#define _PAGE_PSIZE_SHIFT_OFFSET	10

/* "Higher level" linux bit combinations */
#define _PAGE_EXEC		(_PAGE_BAP_SX | _PAGE_BAP_UX) /* .. and was cache cleaned */
#define _PAGE_READ		(_PAGE_BAP_SR | _PAGE_BAP_UR) /* User read permission */
#define _PAGE_WRITE		(_PAGE_BAP_SW | _PAGE_BAP_UW) /* User write permission */

#define _PAGE_KERNEL_RW		(_PAGE_BAP_SW | _PAGE_BAP_SR | _PAGE_DIRTY)
#define _PAGE_KERNEL_RO		(_PAGE_BAP_SR)
#define _PAGE_KERNEL_RWX	(_PAGE_BAP_SW | _PAGE_BAP_SR | _PAGE_DIRTY | _PAGE_BAP_SX)
#define _PAGE_KERNEL_ROX	(_PAGE_BAP_SR | _PAGE_BAP_SX)

#define _PAGE_NA	0
#define _PAGE_NAX	_PAGE_BAP_UX
#define _PAGE_RO	_PAGE_READ
#define _PAGE_ROX	(_PAGE_READ | _PAGE_BAP_UX)
#define _PAGE_RW	(_PAGE_READ | _PAGE_WRITE)
#define _PAGE_RWX	(_PAGE_READ | _PAGE_WRITE | _PAGE_BAP_UX)

#define _PAGE_SPECIAL	_PAGE_SW0

#define	PTE_RPN_SHIFT	(24)

#define PTE_WIMGE_SHIFT (19)
#define PTE_BAP_SHIFT	(2)

/* On 32-bit, we never clear the top part of the PTE */
#ifdef CONFIG_PPC32
#define _PTE_NONE_MASK	0xffffffff00000000ULL
#define _PMD_PRESENT	0
#define _PMD_PRESENT_MASK (PAGE_MASK)
#define _PMD_BAD	(~PAGE_MASK)
#define _PMD_USER	0
#else
#define _PTE_NONE_MASK	0
#endif

/*
 * We define 2 sets of base prot bits, one for basic pages (ie,
 * cacheable kernel and user pages) and one for non cacheable
 * pages. We always set _PAGE_COHERENT when SMP is enabled or
 * the processor might need it for DMA coherency.
 */
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_TSIZE_4K)
#if defined(CONFIG_SMP)
#define _PAGE_BASE	(_PAGE_BASE_NC | _PAGE_COHERENT)
#else
#define _PAGE_BASE	(_PAGE_BASE_NC)
#endif

#include <asm/pgtable-masks.h>

#ifndef __ASSEMBLER__
static inline pte_t pte_mkexec(pte_t pte)
{
	return __pte((pte_val(pte) & ~_PAGE_BAP_SX) | _PAGE_BAP_UX);
}
#define pte_mkexec pte_mkexec

static inline unsigned long pte_huge_size(pte_t pte)
{
	pte_basic_t val = pte_val(pte);

	return 1UL << (((val & _PAGE_PSIZE_MSK) >> _PAGE_PSIZE_SHIFT) + _PAGE_PSIZE_SHIFT_OFFSET);
}
#define pte_huge_size pte_huge_size

static inline int pmd_leaf(pmd_t pmd)
{
	if (IS_ENABLED(CONFIG_PPC64))
		return (long)pmd_val(pmd) > 0;
	else
		return pmd_val(pmd) & _PAGE_PSIZE_MSK;
}
#define pmd_leaf pmd_leaf

static inline unsigned long pmd_leaf_size(pmd_t pmd)
{
	return pte_huge_size(__pte(pmd_val(pmd)));
}
#define pmd_leaf_size pmd_leaf_size

#ifdef CONFIG_PPC64
static inline int pud_leaf(pud_t pud)
{
	if (IS_ENABLED(CONFIG_PPC64))
		return (long)pud_val(pud) > 0;
	else
		return pud_val(pud) & _PAGE_PSIZE_MSK;
}
#define pud_leaf pud_leaf

static inline unsigned long pud_leaf_size(pud_t pud)
{
	return pte_huge_size(__pte(pud_val(pud)));
}
#define pud_leaf_size pud_leaf_size

#endif

#endif /* __ASSEMBLER__ */

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_NOHASH_PTE_E500_H */
