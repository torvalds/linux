/*
 * linux/arch/unicore32/include/asm/pgtable-hwdef.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_PGTABLE_HWDEF_H__
#define __UNICORE_PGTABLE_HWDEF_H__

/*
 * Hardware page table definitions.
 *
 * + Level 1 descriptor (PMD)
 *   - common
 */
#define PMD_TYPE_MASK		(3 << 0)
#define PMD_TYPE_TABLE		(0 << 0)
/*#define PMD_TYPE_LARGE	(1 << 0) */
#define PMD_TYPE_INVALID	(2 << 0)
#define PMD_TYPE_SECT		(3 << 0)

#define PMD_PRESENT		(1 << 2)
#define PMD_YOUNG		(1 << 3)

/*#define PMD_SECT_DIRTY	(1 << 4) */
#define PMD_SECT_CACHEABLE	(1 << 5)
#define PMD_SECT_EXEC		(1 << 6)
#define PMD_SECT_WRITE		(1 << 7)
#define PMD_SECT_READ		(1 << 8)

/*
 * + Level 2 descriptor (PTE)
 *   - common
 */
#define PTE_TYPE_MASK		(3 << 0)
#define PTE_TYPE_SMALL		(0 << 0)
#define PTE_TYPE_MIDDLE		(1 << 0)
#define PTE_TYPE_LARGE		(2 << 0)
#define PTE_TYPE_INVALID	(3 << 0)

#define PTE_PRESENT		(1 << 2)
#define PTE_YOUNG		(1 << 3)
#define PTE_DIRTY		(1 << 4)
#define PTE_CACHEABLE		(1 << 5)
#define PTE_EXEC		(1 << 6)
#define PTE_WRITE		(1 << 7)
#define PTE_READ		(1 << 8)

#endif
