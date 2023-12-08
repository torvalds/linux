/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 *
 * Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <linux/pgtable.h>
#include <asm/kmap_size.h>

/* The map slots for temporary mappings via kmap_atomic/local(). */
enum fixed_addresses {
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = FIX_KMAP_BEGIN +
		(KM_MAX_IDX * NR_CPUS * DCACHE_N_COLORS) - 1,
	__end_of_fixed_addresses
};

#define FIXADDR_END     (XCHAL_KSEG_CACHED_VADDR - PAGE_SIZE)
#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
/* Enforce that FIXADDR_START is PMD aligned to handle cache aliasing */
#define FIXADDR_START	((FIXADDR_END - FIXADDR_SIZE) & PMD_MASK)
#define FIXADDR_TOP	(FIXADDR_START + FIXADDR_SIZE - PAGE_SIZE)

#include <asm-generic/fixmap.h>

#endif /* CONFIG_HIGHMEM */
#endif
