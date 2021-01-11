/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

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

#endif /* __ASM_CSKY_PGTABLE_BITS_H */
