/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PGTABLE_BITS_H
#define _ASM_NIOS2_PGTABLE_BITS_H

/*
 * These are actual hardware defined protection bits in the tlbacc register
 * which looks like this:
 *
 * 31 30 ... 26 25 24 23 22 21 20 19 18 ...  1  0
 * ignored........  C  R  W  X  G PFN............
 */
#define _PAGE_GLOBAL	(1<<20)
#define _PAGE_EXEC	(1<<21)
#define _PAGE_WRITE	(1<<22)
#define _PAGE_READ	(1<<23)
#define _PAGE_CACHED	(1<<24)	/* C: data access cacheable */

/*
 * Software defined bits. They are ignored by the hardware and always read back
 * as zero, but can be written as non-zero.
 */
#define _PAGE_PRESENT	(1<<25)	/* PTE contains a translation */
#define _PAGE_ACCESSED	(1<<26)	/* page referenced */
#define _PAGE_DIRTY	(1<<27)	/* dirty page */

#endif /* _ASM_NIOS2_PGTABLE_BITS_H */
