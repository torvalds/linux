/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PGTABLE_BITS_H
#define __ASM_CSKY_PGTABLE_BITS_H

/* implemented in software */
#define _PAGE_PRESENT		(1<<0)
#define _PAGE_READ		(1<<1)
#define _PAGE_WRITE		(1<<2)
#define _PAGE_ACCESSED		(1<<3)
#define _PAGE_MODIFIED		(1<<4)

/* We borrow bit 9 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	(1<<9)

/* implemented in hardware */
#define _PAGE_GLOBAL		(1<<6)
#define _PAGE_VALID		(1<<7)
#define _PAGE_DIRTY		(1<<8)

#define _PAGE_CACHE		(3<<9)
#define _PAGE_UNCACHE		(2<<9)
#define _PAGE_SO		_PAGE_UNCACHE
#define _CACHE_MASK		(7<<9)

#define _CACHE_CACHED		_PAGE_CACHE
#define _CACHE_UNCACHED		_PAGE_UNCACHE

#define _PAGE_PROT_NONE		_PAGE_READ

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 *
 * Format of swap PTE:
 *     bit          0:    _PAGE_PRESENT (zero)
 *     bit          1:    _PAGE_READ (zero)
 *     bit      2 - 5:    swap type[0 - 3]
 *     bit          6:    _PAGE_GLOBAL (zero)
 *     bit          7:    _PAGE_VALID (zero)
 *     bit          8:    swap type[4]
 *     bit          9:    exclusive marker
 *     bit    10 - 31:    swap offset
 */
#define __swp_type(x)			((((x).val >> 2) & 0xf) | \
					(((x).val >> 4) & 0x10))
#define __swp_offset(x)			((x).val >> 10)
#define __swp_entry(type, offset)	((swp_entry_t) { \
					((type & 0xf) << 2) | \
					((type & 0x10) << 4) | \
					((offset) << 10)})

#define HAVE_ARCH_UNMAPPED_AREA

#endif /* __ASM_CSKY_PGTABLE_BITS_H */
