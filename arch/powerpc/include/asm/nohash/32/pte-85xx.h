/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_PTE_85xx_H
#define _ASM_POWERPC_NOHASH_32_PTE_85xx_H
#ifdef __KERNEL__

/* PTE bit definitions for Freescale BookE SW loaded TLB MMU based
 * processors
 *
   MMU Assist Register 3:

   32 33 34 35 36  ... 50 51 52 53 54 55 56 57 58 59 60 61 62 63
   RPN......................  0  0 U0 U1 U2 U3 UX SX UW SW UR SR

   - PRESENT *must* be in the bottom two bits because swap PTEs use
     the top 30 bits.

*/

/* Definitions for FSL Book-E Cores */
#define _PAGE_READ	0x00001	/* H: Read permission (SR) */
#define _PAGE_PRESENT	0x00002	/* S: PTE contains a translation */
#define _PAGE_WRITE	0x00004	/* S: Write permission (SW) */
#define _PAGE_DIRTY	0x00008	/* S: Page dirty */
#define _PAGE_EXEC	0x00010	/* H: SX permission */
#define _PAGE_ACCESSED	0x00020	/* S: Page referenced */

#define _PAGE_ENDIAN	0x00040	/* H: E bit */
#define _PAGE_GUARDED	0x00080	/* H: G bit */
#define _PAGE_COHERENT	0x00100	/* H: M bit */
#define _PAGE_NO_CACHE	0x00200	/* H: I bit */
#define _PAGE_WRITETHRU	0x00400	/* H: W bit */
#define _PAGE_SPECIAL	0x00800 /* S: Special page */

#define _PMD_PRESENT	0
#define _PMD_PRESENT_MASK (PAGE_MASK)
#define _PMD_BAD	(~PAGE_MASK)
#define _PMD_USER	0

#define _PTE_NONE_MASK	0

#define PTE_WIMGE_SHIFT (6)

/*
 * We define 2 sets of base prot bits, one for basic pages (ie,
 * cacheable kernel and user pages) and one for non cacheable
 * pages. We always set _PAGE_COHERENT when SMP is enabled or
 * the processor might need it for DMA coherency.
 */
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED)
#if defined(CONFIG_SMP) || defined(CONFIG_PPC_E500MC)
#define _PAGE_BASE	(_PAGE_BASE_NC | _PAGE_COHERENT)
#else
#define _PAGE_BASE	(_PAGE_BASE_NC)
#endif

#include <asm/pgtable-masks.h>

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_NOHASH_32_PTE_FSL_85xx_H */
