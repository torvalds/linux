/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2002 by Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 * Copyright (C) 2002  Maciej W. Rozycki
 */
#ifndef _ASM_PGTABLE_BITS_H
#define _ASM_PGTABLE_BITS_H


/*
 * Note that we shift the lower 32bits of each EntryLo[01] entry
 * 6 bits to the left. That way we can convert the PFN into the
 * physical address by a single 'and' operation and gain 6 additional
 * bits for storing information which isn't present in a normal
 * MIPS page table.
 *
 * Similar to the Alpha port, we need to keep track of the ref
 * and mod bits in software.  We have a software "yeah you can read
 * from this page" bit, and a hardware one which actually lets the
 * process read from the page.	On the same token we have a software
 * writable bit and the real hardware one which actually lets the
 * process write to the page, this keeps a mod bit via the hardware
 * dirty bit.
 *
 * Certain revisions of the R4000 and R5000 have a bug where if a
 * certain sequence occurs in the last 3 instructions of an executable
 * page, and the following page is not mapped, the cpu can do
 * unpredictable things.  The code (when it is written) to deal with
 * this problem will be in the update_mmu_cache() code for the r4k.
 */
#if defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)

/*
 * The following bits are implemented by the TLB hardware
 */
#define _PAGE_NO_EXEC_SHIFT	0
#define _PAGE_NO_EXEC		(1 << _PAGE_NO_EXEC_SHIFT)
#define _PAGE_NO_READ_SHIFT	(_PAGE_NO_EXEC_SHIFT + 1)
#define _PAGE_NO_READ		(1 << _PAGE_NO_READ_SHIFT)
#define _PAGE_GLOBAL_SHIFT	(_PAGE_NO_READ_SHIFT + 1)
#define _PAGE_GLOBAL		(1 << _PAGE_GLOBAL_SHIFT)
#define _PAGE_VALID_SHIFT	(_PAGE_GLOBAL_SHIFT + 1)
#define _PAGE_VALID		(1 << _PAGE_VALID_SHIFT)
#define _PAGE_DIRTY_SHIFT	(_PAGE_VALID_SHIFT + 1)
#define _PAGE_DIRTY		(1 << _PAGE_DIRTY_SHIFT)
#define _CACHE_SHIFT		(_PAGE_DIRTY_SHIFT + 1)
#define _CACHE_MASK		(7 << _CACHE_SHIFT)

/*
 * The following bits are implemented in software
 */
#define _PAGE_PRESENT_SHIFT	(24)
#define _PAGE_PRESENT		(1 << _PAGE_PRESENT_SHIFT)
#define _PAGE_READ_SHIFT	(_PAGE_PRESENT_SHIFT + 1)
#define _PAGE_READ		(1 << _PAGE_READ_SHIFT)
#define _PAGE_WRITE_SHIFT	(_PAGE_READ_SHIFT + 1)
#define _PAGE_WRITE		(1 << _PAGE_WRITE_SHIFT)
#define _PAGE_ACCESSED_SHIFT	(_PAGE_WRITE_SHIFT + 1)
#define _PAGE_ACCESSED		(1 << _PAGE_ACCESSED_SHIFT)
#define _PAGE_MODIFIED_SHIFT	(_PAGE_ACCESSED_SHIFT + 1)
#define _PAGE_MODIFIED		(1 << _PAGE_MODIFIED_SHIFT)

#define _PFN_SHIFT		(PAGE_SHIFT - 12 + _CACHE_SHIFT + 3)

/*
 * Bits for extended EntryLo0/EntryLo1 registers
 */
#define _PFNX_MASK		0xffffff

#elif defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

/*
 * The following bits are implemented in software
 */
#define _PAGE_PRESENT_SHIFT	(0)
#define _PAGE_PRESENT		(1 << _PAGE_PRESENT_SHIFT)
#define _PAGE_READ_SHIFT	(_PAGE_PRESENT_SHIFT + 1)
#define _PAGE_READ		(1 << _PAGE_READ_SHIFT)
#define _PAGE_WRITE_SHIFT	(_PAGE_READ_SHIFT + 1)
#define _PAGE_WRITE		(1 << _PAGE_WRITE_SHIFT)
#define _PAGE_ACCESSED_SHIFT	(_PAGE_WRITE_SHIFT + 1)
#define _PAGE_ACCESSED		(1 << _PAGE_ACCESSED_SHIFT)
#define _PAGE_MODIFIED_SHIFT	(_PAGE_ACCESSED_SHIFT + 1)
#define _PAGE_MODIFIED		(1 << _PAGE_MODIFIED_SHIFT)

/*
 * The following bits are implemented by the TLB hardware
 */
#define _PAGE_GLOBAL_SHIFT	(_PAGE_MODIFIED_SHIFT + 4)
#define _PAGE_GLOBAL		(1 << _PAGE_GLOBAL_SHIFT)
#define _PAGE_VALID_SHIFT	(_PAGE_GLOBAL_SHIFT + 1)
#define _PAGE_VALID		(1 << _PAGE_VALID_SHIFT)
#define _PAGE_DIRTY_SHIFT	(_PAGE_VALID_SHIFT + 1)
#define _PAGE_DIRTY		(1 << _PAGE_DIRTY_SHIFT)
#define _CACHE_UNCACHED_SHIFT	(_PAGE_DIRTY_SHIFT + 1)
#define _CACHE_UNCACHED		(1 << _CACHE_UNCACHED_SHIFT)
#define _CACHE_MASK		_CACHE_UNCACHED

#define _PFN_SHIFT		PAGE_SHIFT

#else
/*
 * Below are the "Normal" R4K cases
 */

/*
 * The following bits are implemented in software
 */
#define _PAGE_PRESENT_SHIFT	0
#define _PAGE_PRESENT		(1 << _PAGE_PRESENT_SHIFT)
/* R2 or later cores check for RI/XI support to determine _PAGE_READ */
#ifdef CONFIG_CPU_MIPSR2
#define _PAGE_WRITE_SHIFT	(_PAGE_PRESENT_SHIFT + 1)
#define _PAGE_WRITE		(1 << _PAGE_WRITE_SHIFT)
#else
#define _PAGE_READ_SHIFT	(_PAGE_PRESENT_SHIFT + 1)
#define _PAGE_READ		(1 << _PAGE_READ_SHIFT)
#define _PAGE_WRITE_SHIFT	(_PAGE_READ_SHIFT + 1)
#define _PAGE_WRITE		(1 << _PAGE_WRITE_SHIFT)
#endif
#define _PAGE_ACCESSED_SHIFT	(_PAGE_WRITE_SHIFT + 1)
#define _PAGE_ACCESSED		(1 << _PAGE_ACCESSED_SHIFT)
#define _PAGE_MODIFIED_SHIFT	(_PAGE_ACCESSED_SHIFT + 1)
#define _PAGE_MODIFIED		(1 << _PAGE_MODIFIED_SHIFT)

#if defined(CONFIG_64BIT) && defined(CONFIG_MIPS_HUGE_TLB_SUPPORT)
/* Huge TLB page */
#define _PAGE_HUGE_SHIFT	(_PAGE_MODIFIED_SHIFT + 1)
#define _PAGE_HUGE		(1 << _PAGE_HUGE_SHIFT)
#define _PAGE_SPLITTING_SHIFT	(_PAGE_HUGE_SHIFT + 1)
#define _PAGE_SPLITTING		(1 << _PAGE_SPLITTING_SHIFT)

/* Only R2 or newer cores have the XI bit */
#ifdef CONFIG_CPU_MIPSR2
#define _PAGE_NO_EXEC_SHIFT	(_PAGE_SPLITTING_SHIFT + 1)
#else
#define _PAGE_GLOBAL_SHIFT	(_PAGE_SPLITTING_SHIFT + 1)
#define _PAGE_GLOBAL		(1 << _PAGE_GLOBAL_SHIFT)
#endif	/* CONFIG_CPU_MIPSR2 */

#endif	/* CONFIG_64BIT && CONFIG_MIPS_HUGE_TLB_SUPPORT */

#ifdef CONFIG_CPU_MIPSR2
/* XI - page cannot be executed */
#ifndef _PAGE_NO_EXEC_SHIFT
#define _PAGE_NO_EXEC_SHIFT	(_PAGE_MODIFIED_SHIFT + 1)
#endif
#define _PAGE_NO_EXEC		(cpu_has_rixi ? (1 << _PAGE_NO_EXEC_SHIFT) : 0)

/* RI - page cannot be read */
#define _PAGE_READ_SHIFT	(_PAGE_NO_EXEC_SHIFT + 1)
#define _PAGE_READ		(cpu_has_rixi ? 0 : (1 << _PAGE_READ_SHIFT))
#define _PAGE_NO_READ_SHIFT	_PAGE_READ_SHIFT
#define _PAGE_NO_READ		(cpu_has_rixi ? (1 << _PAGE_READ_SHIFT) : 0)

#define _PAGE_GLOBAL_SHIFT	(_PAGE_NO_READ_SHIFT + 1)
#define _PAGE_GLOBAL		(1 << _PAGE_GLOBAL_SHIFT)

#else	/* !CONFIG_CPU_MIPSR2 */
#define _PAGE_GLOBAL_SHIFT	(_PAGE_MODIFIED_SHIFT + 1)
#define _PAGE_GLOBAL		(1 << _PAGE_GLOBAL_SHIFT)
#endif	/* CONFIG_CPU_MIPSR2 */

#define _PAGE_VALID_SHIFT	(_PAGE_GLOBAL_SHIFT + 1)
#define _PAGE_VALID		(1 << _PAGE_VALID_SHIFT)
#define _PAGE_DIRTY_SHIFT	(_PAGE_VALID_SHIFT + 1)
#define _PAGE_DIRTY		(1 << _PAGE_DIRTY_SHIFT)
#define _CACHE_SHIFT		(_PAGE_DIRTY_SHIFT + 1)
#define _CACHE_MASK		(7 << _CACHE_SHIFT)

#define _PFN_SHIFT		(PAGE_SHIFT - 12 + _CACHE_SHIFT + 3)

#endif /* defined(CONFIG_PHYS_ADDR_T_64BIT && defined(CONFIG_CPU_MIPS32) */

#ifndef _PAGE_NO_EXEC
#define _PAGE_NO_EXEC		0
#endif
#ifndef _PAGE_NO_READ
#define _PAGE_NO_READ		0
#endif

#define _PAGE_SILENT_READ	_PAGE_VALID
#define _PAGE_SILENT_WRITE	_PAGE_DIRTY

#define _PFN_MASK		(~((1 << (_PFN_SHIFT)) - 1))

/*
 * The final layouts of the PTE bits are:
 *
 *   64-bit, R1 or earlier:     CCC D V G [S H] M A W R P
 *   32-bit, R1 or earler:      CCC D V G M A W R P
 *   64-bit, R2 or later:       CCC D V G RI/R XI [S H] M A W P
 *   32-bit, R2 or later:       CCC D V G RI/R XI M A W P
 */


#ifndef __ASSEMBLY__
/*
 * pte_to_entrylo converts a page table entry (PTE) into a Mips
 * entrylo0/1 value.
 */
static inline uint64_t pte_to_entrylo(unsigned long pte_val)
{
#ifdef CONFIG_CPU_MIPSR2
	if (cpu_has_rixi) {
		int sa;
#ifdef CONFIG_32BIT
		sa = 31 - _PAGE_NO_READ_SHIFT;
#else
		sa = 63 - _PAGE_NO_READ_SHIFT;
#endif
		/*
		 * C has no way to express that this is a DSRL
		 * _PAGE_NO_EXEC_SHIFT followed by a ROTR 2.  Luckily
		 * in the fast path this is done in assembly
		 */
		return (pte_val >> _PAGE_GLOBAL_SHIFT) |
			((pte_val & (_PAGE_NO_EXEC | _PAGE_NO_READ)) << sa);
	}
#endif

	return pte_val >> _PAGE_GLOBAL_SHIFT;
}
#endif

/*
 * Cache attributes
 */
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

#define _CACHE_CACHABLE_NONCOHERENT 0
#define _CACHE_UNCACHED_ACCELERATED _CACHE_UNCACHED

#elif defined(CONFIG_CPU_SB1)

/* No penalty for being coherent on the SB1, so just
   use it for "noncoherent" spaces, too.  Shouldn't hurt. */

#define _CACHE_CACHABLE_NONCOHERENT (5<<_CACHE_SHIFT)

#elif defined(CONFIG_CPU_LOONGSON3)

/* Using COHERENT flag for NONCOHERENT doesn't hurt. */

#define _CACHE_CACHABLE_NONCOHERENT (3<<_CACHE_SHIFT)  /* LOONGSON       */
#define _CACHE_CACHABLE_COHERENT    (3<<_CACHE_SHIFT)  /* LOONGSON-3     */

#elif defined(CONFIG_MACH_JZ4740)

/* Ingenic uses the WA bit to achieve write-combine memory writes */
#define _CACHE_UNCACHED_ACCELERATED (1<<_CACHE_SHIFT)

#endif

#ifndef _CACHE_CACHABLE_NO_WA
#define _CACHE_CACHABLE_NO_WA		(0<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_CACHABLE_WA
#define _CACHE_CACHABLE_WA		(1<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_UNCACHED
#define _CACHE_UNCACHED			(2<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_CACHABLE_NONCOHERENT
#define _CACHE_CACHABLE_NONCOHERENT	(3<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_CACHABLE_CE
#define _CACHE_CACHABLE_CE		(4<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_CACHABLE_COW
#define _CACHE_CACHABLE_COW		(5<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_CACHABLE_CUW
#define _CACHE_CACHABLE_CUW		(6<<_CACHE_SHIFT)
#endif
#ifndef _CACHE_UNCACHED_ACCELERATED
#define _CACHE_UNCACHED_ACCELERATED	(7<<_CACHE_SHIFT)
#endif

#define __READABLE	(_PAGE_SILENT_READ | _PAGE_READ | _PAGE_ACCESSED)
#define __WRITEABLE	(_PAGE_SILENT_WRITE | _PAGE_WRITE | _PAGE_MODIFIED)

#define _PAGE_CHG_MASK	(_PAGE_ACCESSED | _PAGE_MODIFIED |	\
			 _PFN_MASK | _CACHE_MASK)

#endif /* _ASM_PGTABLE_BITS_H */
