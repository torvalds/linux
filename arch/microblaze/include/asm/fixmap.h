/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 *
 * Copyright 2008 Freescale Semiconductor Inc.
 *   Port to powerpc added by Kumar Gala
 *
 * Copyright 2011 Michal Simek <monstr@monstr.eu>
 * Copyright 2011 PetaLogix Qld Pty Ltd
 *   Port to Microblaze
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/page.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

#define FIXADDR_TOP	((unsigned long)(-PAGE_SIZE))

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special addresses
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
enum fixed_addresses {
	FIX_HOLE,
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * num_possible_cpus()) - 1,
#endif
	__end_of_fixed_addresses
};

extern void __set_fixmap(enum fixed_addresses idx,
					phys_addr_t phys, pgprot_t flags);

#define __FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - __FIXADDR_SIZE)

#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_CI

#include <asm-generic/fixmap.h>

#endif /* !__ASSEMBLY__ */
#endif
