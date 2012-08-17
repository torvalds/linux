/*
 * Copyright (C) 1998 Ingo Molnar
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_FIXMAP_H
#define _ASM_TILE_FIXMAP_H

#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))
#define __virt_to_fix(x)	((FIXADDR_TOP - ((x)&PAGE_MASK)) >> PAGE_SHIFT)

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special addresses
 * from the end of supervisor virtual memory backwards.
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
#ifdef __tilegx__
	/*
	 * TILEPro has unmapped memory above so the hole isn't needed,
	 * and in any case the hole pushes us over a single 16MB pmd.
	 */
	FIX_HOLE,
#endif
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_END = FIX_KMAP_BEGIN+(KM_TYPE_NR*NR_CPUS)-1,
#endif
#ifdef __tilegx__  /* see homecache.c */
	FIX_HOMECACHE_BEGIN,
	FIX_HOMECACHE_END = FIX_HOMECACHE_BEGIN+(NR_CPUS)-1,
#endif
	__end_of_permanent_fixed_addresses,

	/*
	 * Temporary boot-time mappings, used before ioremap() is functional.
	 * Not currently needed by the Tile architecture.
	 */
#define NR_FIX_BTMAPS	0
#if NR_FIX_BTMAPS
	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + NR_FIX_BTMAPS - 1,
	__end_of_fixed_addresses
#else
	__end_of_fixed_addresses = __end_of_permanent_fixed_addresses
#endif
};

extern void __set_fixmap(enum fixed_addresses idx,
			 unsigned long phys, pgprot_t flags);

#define set_fixmap(idx, phys) \
		__set_fixmap(idx, phys, PAGE_KERNEL)
#define clear_fixmap(idx) \
		__set_fixmap(idx, 0, __pgprot(0))

#define __FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define __FIXADDR_BOOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP + PAGE_SIZE - __FIXADDR_SIZE)
#define FIXADDR_BOOT_START	(FIXADDR_TOP + PAGE_SIZE - __FIXADDR_BOOT_SIZE)

extern void __this_fixmap_does_not_exist(void);

/*
 * 'index to address' translation. If anyone tries to use the idx
 * directly without tranlation, we catch the bug with a NULL-deference
 * kernel oops. Illegal ranges of incoming indices are caught too.
 */
static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	/*
	 * this branch gets completely eliminated after inlining,
	 * except when someone tries to use fixaddr indices in an
	 * illegal way. (such as mixing up address types or using
	 * out-of-range indices).
	 *
	 * If it doesn't get removed, the linker will complain
	 * loudly with a reasonably clear error message..
	 */
	if (idx >= __end_of_fixed_addresses)
		__this_fixmap_does_not_exist();

	return __fix_to_virt(idx);
}

static inline unsigned long virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_FIXMAP_H */
