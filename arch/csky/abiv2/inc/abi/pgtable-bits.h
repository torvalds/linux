/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PGTABLE_BITS_H
#define __ASM_CSKY_PGTABLE_BITS_H

/* implemented in software */
#define _PAGE_ACCESSED		(1<<7)
#define _PAGE_READ		(1<<8)
#define _PAGE_WRITE		(1<<9)
#define _PAGE_PRESENT		(1<<10)
#define _PAGE_MODIFIED		(1<<11)

/* implemented in hardware */
#define _PAGE_GLOBAL		(1<<0)
#define _PAGE_VALID		(1<<1)
#define _PAGE_DIRTY		(1<<2)

#define _PAGE_SO		(1<<5)
#define _PAGE_BUF		(1<<6)
#define _PAGE_CACHE		(1<<3)
#define _CACHE_MASK		_PAGE_CACHE

#define _CACHE_CACHED		(_PAGE_CACHE | _PAGE_BUF)
#define _CACHE_UNCACHED		(0)

#define _PAGE_PROT_NONE		_PAGE_WRITE

/*
 * Encode and decode a swap entry
 *
 * Format of swap PTE:
 *     bit          0:    _PAGE_GLOBAL (zero)
 *     bit          1:    _PAGE_VALID (zero)
 *     bit      2 - 6:    swap type
 *     bit      7 - 8:    swap offset[0 - 1]
 *     bit          9:    _PAGE_WRITE (zero)
 *     bit         10:    _PAGE_PRESENT (zero)
 *     bit    11 - 31:    swap offset[2 - 22]
 */
#define __swp_type(x)			(((x).val >> 2) & 0x1f)
#define __swp_offset(x)			((((x).val >> 7) & 0x3) | \
					(((x).val >> 9) & 0x7ffffc))
#define __swp_entry(type, offset)	((swp_entry_t) { \
					((type & 0x1f) << 2) | \
					((offset & 0x3) << 7) | \
					((offset & 0x7ffffc) << 9)})

#endif /* __ASM_CSKY_PGTABLE_BITS_H */
