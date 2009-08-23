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
 * process read from the page.  On the same token we have a software
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
#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_CPU_MIPS32)

#define _PAGE_PRESENT               (1<<6)  /* implemented in software */
#define _PAGE_READ                  (1<<7)  /* implemented in software */
#define _PAGE_WRITE                 (1<<8)  /* implemented in software */
#define _PAGE_ACCESSED              (1<<9)  /* implemented in software */
#define _PAGE_MODIFIED              (1<<10) /* implemented in software */
#define _PAGE_FILE                  (1<<10) /* set:pagecache unset:swap */

#define _PAGE_R4KBUG                (1<<0)  /* workaround for r4k bug  */
#define _PAGE_GLOBAL                (1<<0)
#define _PAGE_VALID                 (1<<1)
#define _PAGE_SILENT_READ           (1<<1)  /* synonym                 */
#define _PAGE_DIRTY                 (1<<2)  /* The MIPS dirty bit      */
#define _PAGE_SILENT_WRITE          (1<<2)
#define _CACHE_SHIFT                3
#define _CACHE_MASK                 (7<<3)

#else

#define _PAGE_PRESENT               (1<<0)  /* implemented in software */
#define _PAGE_READ                  (1<<1)  /* implemented in software */
#define _PAGE_WRITE                 (1<<2)  /* implemented in software */
#define _PAGE_ACCESSED              (1<<3)  /* implemented in software */
#define _PAGE_MODIFIED              (1<<4)  /* implemented in software */
#define _PAGE_FILE                  (1<<4)  /* set:pagecache unset:swap */

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

#define _PAGE_GLOBAL                (1<<8)
#define _PAGE_VALID                 (1<<9)
#define _PAGE_SILENT_READ           (1<<9)  /* synonym                 */
#define _PAGE_DIRTY                 (1<<10) /* The MIPS dirty bit      */
#define _PAGE_SILENT_WRITE          (1<<10)
#define _CACHE_UNCACHED             (1<<11)
#define _CACHE_MASK                 (1<<11)

#else

#define _PAGE_R4KBUG                (1<<5)  /* workaround for r4k bug  */
#define _PAGE_HUGE                  (1<<5)  /* huge tlb page */
#define _PAGE_GLOBAL                (1<<6)
#define _PAGE_VALID                 (1<<7)
#define _PAGE_SILENT_READ           (1<<7)  /* synonym                 */
#define _PAGE_DIRTY                 (1<<8)  /* The MIPS dirty bit      */
#define _PAGE_SILENT_WRITE          (1<<8)
#define _CACHE_SHIFT		    9
#define _CACHE_MASK                 (7<<9)

#endif
#endif /* defined(CONFIG_64BIT_PHYS_ADDR && defined(CONFIG_CPU_MIPS32) */


/*
 * Cache attributes
 */
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

#define _CACHE_CACHABLE_NONCOHERENT 0

#elif defined(CONFIG_CPU_SB1)

/* No penalty for being coherent on the SB1, so just
   use it for "noncoherent" spaces, too.  Shouldn't hurt. */

#define _CACHE_UNCACHED             (2<<_CACHE_SHIFT)
#define _CACHE_CACHABLE_COW         (5<<_CACHE_SHIFT)
#define _CACHE_CACHABLE_NONCOHERENT (5<<_CACHE_SHIFT)
#define _CACHE_UNCACHED_ACCELERATED (7<<_CACHE_SHIFT)

#elif defined(CONFIG_CPU_RM9000)

#define _CACHE_WT		    (0<<_CACHE_SHIFT)
#define _CACHE_WTWA		    (1<<_CACHE_SHIFT)
#define _CACHE_UC_B		    (2<<_CACHE_SHIFT)
#define _CACHE_WB		    (3<<_CACHE_SHIFT)
#define _CACHE_CWBEA		    (4<<_CACHE_SHIFT)
#define _CACHE_CWB		    (5<<_CACHE_SHIFT)
#define _CACHE_UCNB		    (6<<_CACHE_SHIFT)
#define _CACHE_FPC		    (7<<_CACHE_SHIFT)

#define _CACHE_UNCACHED		    _CACHE_UC_B
#define _CACHE_CACHABLE_NONCOHERENT _CACHE_WB

#else

#define _CACHE_CACHABLE_NO_WA	    (0<<_CACHE_SHIFT)  /* R4600 only      */
#define _CACHE_CACHABLE_WA	    (1<<_CACHE_SHIFT)  /* R4600 only      */
#define _CACHE_UNCACHED             (2<<_CACHE_SHIFT)  /* R4[0246]00      */
#define _CACHE_CACHABLE_NONCOHERENT (3<<_CACHE_SHIFT)  /* R4[0246]00      */
#define _CACHE_CACHABLE_CE          (4<<_CACHE_SHIFT)  /* R4[04]00MC only */
#define _CACHE_CACHABLE_COW         (5<<_CACHE_SHIFT)  /* R4[04]00MC only */
#define _CACHE_CACHABLE_COHERENT    (5<<_CACHE_SHIFT)  /* MIPS32R2 CMP    */
#define _CACHE_CACHABLE_CUW         (6<<_CACHE_SHIFT)  /* R4[04]00MC only */
#define _CACHE_UNCACHED_ACCELERATED (7<<_CACHE_SHIFT)  /* R10000 only     */

#endif

#define __READABLE	(_PAGE_READ | _PAGE_SILENT_READ | _PAGE_ACCESSED)
#define __WRITEABLE	(_PAGE_WRITE | _PAGE_SILENT_WRITE | _PAGE_MODIFIED)

#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED | _CACHE_MASK)

#endif /* _ASM_PGTABLE_BITS_H */
