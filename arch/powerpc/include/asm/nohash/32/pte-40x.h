/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_PTE_40x_H
#define _ASM_POWERPC_NOHASH_32_PTE_40x_H
#ifdef __KERNEL__

/*
 * At present, all PowerPC 400-class processors share a similar TLB
 * architecture. The instruction and data sides share a unified,
 * 64-entry, fully-associative TLB which is maintained totally under
 * software control. In addition, the instruction side has a
 * hardware-managed, 4-entry, fully-associative TLB which serves as a
 * first level to the shared TLB. These two TLBs are known as the UTLB
 * and ITLB, respectively (see "mmu.h" for definitions).
 *
 * There are several potential gotchas here.  The 40x hardware TLBLO
 * field looks like this:
 *
 * 0  1  2  3  4  ... 18 19 20 21 22 23 24 25 26 27 28 29 30 31
 * RPN.....................  0  0 EX WR ZSEL.......  W  I  M  G
 *
 * Where possible we make the Linux PTE bits match up with this
 *
 * - bits 20 and 21 must be cleared, because we use 4k pages (40x can
 *   support down to 1k pages), this is done in the TLBMiss exception
 *   handler.
 * - We use only zones 0 (for kernel pages) and 1 (for user pages)
 *   of the 16 available.  Bit 24-26 of the TLB are cleared in the TLB
 *   miss handler.  Bit 27 is PAGE_USER, thus selecting the correct
 *   zone.
 * - PRESENT *must* be in the bottom two bits because swap PTEs
 *   use the top 30 bits.  Because 40x doesn't support SMP anyway, M is
 *   irrelevant so we borrow it for PAGE_PRESENT.  Bit 30
 *   is cleared in the TLB miss handler before the TLB entry is loaded.
 * - All other bits of the PTE are loaded into TLBLO without
 *   modification, leaving us only the bits 20, 21, 24, 25, 26, 30 for
 *   software PTE bits.  We actually use bits 21, 24, 25, and
 *   30 respectively for the software bits: ACCESSED, DIRTY, RW, and
 *   PRESENT.
 */

#define	_PAGE_GUARDED	0x001	/* G: page is guarded from prefetch */
#define _PAGE_PRESENT	0x002	/* software: PTE contains a translation */
#define	_PAGE_NO_CACHE	0x004	/* I: caching is inhibited */
#define	_PAGE_WRITETHRU	0x008	/* W: caching is write-through */
#define	_PAGE_USER	0x010	/* matches one of the zone permission bits */
#define	_PAGE_SPECIAL	0x020	/* software: Special page */
#define	_PAGE_DIRTY	0x080	/* software: dirty page */
#define _PAGE_RW	0x100	/* hardware: WR, anded with dirty in exception */
#define _PAGE_EXEC	0x200	/* hardware: EX permission */
#define _PAGE_ACCESSED	0x400	/* software: R: page referenced */

/* No page size encoding in the linux PTE */
#define _PAGE_PSIZE		0

/* cache related flags non existing on 40x */
#define _PAGE_COHERENT	0

#define _PAGE_KERNEL_RO		0
#define _PAGE_KERNEL_ROX	_PAGE_EXEC
#define _PAGE_KERNEL_RW		(_PAGE_DIRTY | _PAGE_RW)
#define _PAGE_KERNEL_RWX	(_PAGE_DIRTY | _PAGE_RW | _PAGE_EXEC)

#define _PMD_PRESENT	0x400	/* PMD points to page of PTEs */
#define _PMD_PRESENT_MASK	_PMD_PRESENT
#define _PMD_BAD	0x802
#define _PMD_SIZE_4M	0x0c0
#define _PMD_SIZE_16M	0x0e0
#define _PMD_USER	0

#define _PTE_NONE_MASK	0

/* Until my rework is finished, 40x still needs atomic PTE updates */
#define PTE_ATOMIC_UPDATES	1

#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED)
#define _PAGE_BASE	(_PAGE_BASE_NC)

/* Permission masks used to generate the __P and __S table */
#define PAGE_NONE	__pgprot(_PAGE_BASE)
#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_RW)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_RW | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_NOHASH_32_PTE_40x_H */
