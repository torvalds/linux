/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PAGE_32_DEFS_H
#define _ASM_X86_PAGE_32_DEFS_H

#include <linux/const.h>

/*
 * This handles the memory map.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB.
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 */
#define __PAGE_OFFSET_BASE	_AC(CONFIG_PAGE_OFFSET, UL)
#define __PAGE_OFFSET		__PAGE_OFFSET_BASE

#define __START_KERNEL_map	__PAGE_OFFSET

#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#define IRQ_STACK_SIZE		THREAD_SIZE

#define N_EXCEPTION_STACKS	1

#ifdef CONFIG_X86_PAE
/*
 * This is beyond the 44 bit limit imposed by the 32bit long pfns,
 * but we need the full mask to make sure inverted PROT_NONE
 * entries have all the host bits set in a guest.
 * The real limit is still 44 bits.
 */
#define __PHYSICAL_MASK_SHIFT	52
#define __VIRTUAL_MASK_SHIFT	32

#else  /* !CONFIG_X86_PAE */
#define __PHYSICAL_MASK_SHIFT	32
#define __VIRTUAL_MASK_SHIFT	32
#endif	/* CONFIG_X86_PAE */

/*
 * User space process size: 3GB (default).
 */
#define IA32_PAGE_OFFSET	__PAGE_OFFSET
#define TASK_SIZE		__PAGE_OFFSET
#define TASK_SIZE_LOW		TASK_SIZE
#define TASK_SIZE_MAX		TASK_SIZE
#define DEFAULT_MAP_WINDOW	TASK_SIZE
#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		STACK_TOP

/*
 * Kernel image size is limited to 512 MB (see in arch/x86/kernel/head_32.S)
 */
#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)

#ifndef __ASSEMBLY__

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;
extern int sysctl_legacy_va_layout;

extern void find_low_pfn_range(void);
extern void setup_bootmem_allocator(void);

#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_X86_PAGE_32_DEFS_H */
