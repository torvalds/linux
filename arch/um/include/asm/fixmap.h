/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_FIXMAP_H
#define __UM_FIXMAP_H

#include <asm/processor.h>
#include <asm/kmap_types.h>
#include <asm/archparam.h>
#include <asm/page.h>
#include <linux/threads.h>

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special  addresses
 * from the end of virtual memory (0xfffff000) backwards.
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
	__end_of_fixed_addresses
};

extern void __set_fixmap (enum fixed_addresses idx,
			  unsigned long phys, pgprot_t flags);

/*
 * used by vmalloc.c.
 *
 * Leave one empty page between vmalloc'ed areas and
 * the start of the fixmap, and leave one page empty
 * at the top of mem..
 */

#define FIXADDR_TOP	(TASK_SIZE - 2 * PAGE_SIZE)
#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#include <asm-generic/fixmap.h>

#endif
