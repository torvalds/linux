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

#include <linux/kernel.h>
#include <linux/threads.h>
#include <asm/page.h>
#ifdef CONFIG_HIGHMEM
#include <asm/kmap_types.h>
#endif

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special  addresses
 * from the end of P3 backwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * these 'compile-time allocated' memory buffers are
 * fixed-size 4k pages. (or larger if used with an increment
 * highger than 1) use fixmap_set(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */

/*
 * on UP currently we will have no trace of the fixmap mechanizm,
 * no page table allocations, etc. This might change in the
 * future, say framebuffers for the console driver(s) could be
 * fix-mapped?
 */
enum fixed_addresses {
	/*
	 * The FIX_CMAP entries are used by kmap_coherent() to get virtual
	 * addresses which are of a known color, and so their values are
	 * important. __fix_to_virt(FIX_CMAP_END - n) must give an address
	 * which is the same color as a page (n<<PAGE_SHIFT).
	 */
#define FIX_N_COLOURS 8
	FIX_CMAP_BEGIN,
	FIX_CMAP_END = FIX_CMAP_BEGIN + (FIX_N_COLOURS * NR_CPUS) - 1,

#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * NR_CPUS) - 1,
#endif

#ifdef CONFIG_IOREMAP_FIXED
	/*
	 * FIX_IOREMAP entries are useful for mapping physical address
	 * space before ioremap() is useable, e.g. really early in boot
	 * before kmalloc() is working.
	 */
#define FIX_N_IOREMAPS	32
	FIX_IOREMAP_BEGIN,
	FIX_IOREMAP_END = FIX_IOREMAP_BEGIN + FIX_N_IOREMAPS - 1,
#endif

	__end_of_fixed_addresses
};

extern void __set_fixmap(enum fixed_addresses idx,
			 unsigned long phys, pgprot_t flags);
extern void __clear_fixmap(enum fixed_addresses idx, pgprot_t flags);

/*
 * used by vmalloc.c.
 *
 * Leave one empty page between vmalloc'ed areas and
 * the start of the fixmap, and leave one page empty
 * at the top of mem..
 */
#ifdef CONFIG_SUPERH32
#define FIXADDR_TOP	(P4SEG - PAGE_SIZE)
#else
#define FIXADDR_TOP	((unsigned long)(-PAGE_SIZE))
#endif
#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_NOCACHE

#include <asm-generic/fixmap.h>

#endif
