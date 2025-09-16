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
 * If you want more physical memory than this then see the CONFIG_VMSPLIT_2G
 * and CONFIG_HIGHMEM4G options in the kernel configuration.
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
 * In spite of the name, KERNEL_IMAGE_SIZE is a limit on the maximum virtual
 * address for the kernel image, rather than the limit on the size itself. On
 * 32-bit, this is not a strict limit, but this value is used to limit the
 * link-time virtual address range of the kernel, and by KASLR to limit the
 * randomized address from which the kernel is executed. A relocatable kernel
 * can be loaded somewhat higher than KERNEL_IMAGE_SIZE as long as enough space
 * remains for the vmalloc area.
 */
#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)

#ifndef __ASSEMBLER__

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;
extern int sysctl_legacy_va_layout;

extern void find_low_pfn_range(void);

#endif	/* !__ASSEMBLER__ */

#endif /* _ASM_X86_PAGE_32_DEFS_H */
