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
#endif

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special addresses
 * from the start of the consistent memory region upwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * these 'compile-time allocated' memory buffers are
 * fixed-size 4k pages. (or larger if used with an increment
 * higher than 1) use fixmap_set(idx,phys) to associate
 * physical memory with fixmap indices.
 */
enum fixed_addresses {
#ifdef CONFIG_HIGHMEM
	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = FIX_KMAP_BEGIN +
		(KM_MAX_IDX * NR_CPUS * DCACHE_N_COLORS) - 1,
#endif
	__end_of_fixed_addresses
};

#define FIXADDR_TOP     (XCHAL_KSEG_CACHED_VADDR - PAGE_SIZE)
#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	((FIXADDR_TOP - FIXADDR_SIZE) & PMD_MASK)

#define __fix_to_virt(x)	(FIXADDR_START + ((x) << PAGE_SHIFT))
#define __virt_to_fix(x)	(((x) - FIXADDR_START) >> PAGE_SHIFT)

#ifndef __ASSEMBLY__
/*
 * 'index to address' translation. If anyone tries to use the idx
 * directly without translation, we catch the bug with a NULL-deference
 * kernel oops. Illegal ranges of incoming indices are caught too.
 */
static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	/* Check if this memory layout is broken because fixmap overlaps page
	 * table.
	 */
	BUILD_BUG_ON(FIXADDR_START <
		     TLBTEMP_BASE_1 + TLBTEMP_SIZE);
	BUILD_BUG_ON(idx >= __end_of_fixed_addresses);
	return __fix_to_virt(idx);
}

static inline unsigned long virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

#endif

#endif
