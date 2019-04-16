/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_PGTABLE_BITS_H
#define __ASM_CSKY_PGTABLE_BITS_H

/* implemented in software */
#define _PAGE_ACCESSED		(1<<3)
#define PAGE_ACCESSED_BIT	(3)

#define _PAGE_READ		(1<<1)
#define _PAGE_WRITE		(1<<2)
#define _PAGE_PRESENT		(1<<0)

#define _PAGE_MODIFIED		(1<<4)
#define PAGE_MODIFIED_BIT	(4)

/* implemented in hardware */
#define _PAGE_GLOBAL		(1<<6)

#define _PAGE_VALID		(1<<7)
#define PAGE_VALID_BIT		(7)

#define _PAGE_DIRTY		(1<<8)
#define PAGE_DIRTY_BIT		(8)

#define _PAGE_CACHE		(3<<9)
#define _PAGE_UNCACHE		(2<<9)
#define _PAGE_SO		_PAGE_UNCACHE

#define _CACHE_MASK		(7<<9)

#define _CACHE_CACHED		(_PAGE_VALID | _PAGE_CACHE)
#define _CACHE_UNCACHED		(_PAGE_VALID | _PAGE_UNCACHE)

#define HAVE_ARCH_UNMAPPED_AREA

#endif /* __ASM_CSKY_PGTABLE_BITS_H */
