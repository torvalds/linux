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

#include <asm/pgtable.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special  addresses
 * from the end of the consistent memory region backwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * these 'compile-time allocated' memory buffers are
 * fixed-size 4k pages. (or larger if used with an increment
 * higher than 1) use fixmap_set(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */
enum fixed_addresses {
#define FIX_N_COLOURS 8
#ifdef CONFIG_HIGHMEM
	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = FIX_KMAP_BEGIN+(KM_TYPE_NR*NR_CPUS)-1,
#endif
	__end_of_fixed_addresses
};

#define FIXADDR_TOP     (CONSISTENT_START - PAGE_SIZE)
#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	((FIXADDR_TOP - FIXADDR_SIZE) & PMD_MASK)

#include <asm-generic/fixmap.h>

#define kmap_get_fixmap_pte(vaddr) \
	pte_offset_kernel( \
		pmd_offset(pud_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr)), \
		(vaddr) \
	)

/*
 * Called from pgtable_init()
 */
extern void fixrange_init(unsigned long start, unsigned long end,
	pgd_t *pgd_base);


#endif
