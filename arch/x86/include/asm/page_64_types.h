/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PAGE_64_DEFS_H
#define _ASM_X86_PAGE_64_DEFS_H

#ifndef __ASSEMBLY__
#include <asm/kaslr.h>
#endif

#ifdef CONFIG_KASAN
#define KASAN_STACK_ORDER 1
#else
#define KASAN_STACK_ORDER 0
#endif

#define THREAD_SIZE_ORDER	(2 + KASAN_STACK_ORDER)
#define THREAD_SIZE  (PAGE_SIZE << THREAD_SIZE_ORDER)
#define CURRENT_MASK (~(THREAD_SIZE - 1))

#define EXCEPTION_STACK_ORDER (0 + KASAN_STACK_ORDER)
#define EXCEPTION_STKSZ (PAGE_SIZE << EXCEPTION_STACK_ORDER)

#define DEBUG_STACK_ORDER (EXCEPTION_STACK_ORDER + 1)
#define DEBUG_STKSZ (PAGE_SIZE << DEBUG_STACK_ORDER)

#define IRQ_STACK_ORDER (2 + KASAN_STACK_ORDER)
#define IRQ_STACK_SIZE (PAGE_SIZE << IRQ_STACK_ORDER)

#define DOUBLEFAULT_STACK 1
#define NMI_STACK 2
#define DEBUG_STACK 3
#define MCE_STACK 4
#define N_EXCEPTION_STACKS 4  /* hw limit: 7 */

/*
 * Set __PAGE_OFFSET to the most negative possible address +
 * PGDIR_SIZE*17 (pgd slot 273).
 *
 * The gap is to allow a space for LDT remap for PTI (1 pgd slot) and space for
 * a hypervisor (16 slots). Choosing 16 slots for a hypervisor is arbitrary,
 * but it's what Xen requires.
 */
#define __PAGE_OFFSET_BASE_L5	_AC(0xff11000000000000, UL)
#define __PAGE_OFFSET_BASE_L4	_AC(0xffff888000000000, UL)

#ifdef CONFIG_DYNAMIC_MEMORY_LAYOUT
#define __PAGE_OFFSET           page_offset_base
#else
#define __PAGE_OFFSET           __PAGE_OFFSET_BASE_L4
#endif /* CONFIG_DYNAMIC_MEMORY_LAYOUT */

#define __START_KERNEL_map	_AC(0xffffffff80000000, UL)

/* See Documentation/x86/x86_64/mm.txt for a description of the memory map. */

#define __PHYSICAL_MASK_SHIFT	52

#ifdef CONFIG_X86_5LEVEL
#define __VIRTUAL_MASK_SHIFT	(pgtable_l5_enabled() ? 56 : 47)
#else
#define __VIRTUAL_MASK_SHIFT	47
#endif

/*
 * Maximum kernel image size is limited to 1 GiB, due to the fixmap living
 * in the next 1 GiB (see level2_kernel_pgt in arch/x86/kernel/head_64.S).
 *
 * On KASLR use 1 GiB by default, leaving 1 GiB for modules once the
 * page tables are fully set up.
 *
 * If KASLR is disabled we can shrink it to 0.5 GiB and increase the size
 * of the modules area to 1.5 GiB.
 */
#ifdef CONFIG_RANDOMIZE_BASE
#define KERNEL_IMAGE_SIZE	(1024 * 1024 * 1024)
#else
#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)
#endif

#endif /* _ASM_X86_PAGE_64_DEFS_H */
