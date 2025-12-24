/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TASK_SIZE_32_H
#define _ASM_POWERPC_TASK_SIZE_32_H

#include <linux/sizes.h>

#if CONFIG_TASK_SIZE > CONFIG_KERNEL_START
#error User TASK_SIZE overlaps with KERNEL_START address
#endif

#ifdef CONFIG_PPC_8xx
#define MODULES_END	ASM_CONST(CONFIG_PAGE_OFFSET)
#define MODULES_SIZE	(CONFIG_MODULES_SIZE * SZ_1M)
#define MODULES_VADDR	(MODULES_END - MODULES_SIZE)
#define MODULES_BASE	(MODULES_VADDR & ~(UL(SZ_4M) - 1))
#define USER_TOP	(MODULES_BASE - SZ_4M)
#endif

#ifdef CONFIG_PPC_BOOK3S_32
#define MODULES_END	(ASM_CONST(CONFIG_PAGE_OFFSET) & ~(UL(SZ_256M) - 1))
#define MODULES_SIZE	(CONFIG_MODULES_SIZE * SZ_1M)
#define MODULES_VADDR	(MODULES_END - MODULES_SIZE)
#define MODULES_BASE	(MODULES_VADDR & ~(UL(SZ_256M) - 1))
#define USER_TOP	(MODULES_BASE - SZ_4M)
#endif

#ifndef USER_TOP
#define USER_TOP	((ASM_CONST(CONFIG_PAGE_OFFSET) - SZ_128K) & ~(UL(SZ_128K) - 1))
#endif

#if CONFIG_TASK_SIZE < USER_TOP
#define TASK_SIZE ASM_CONST(CONFIG_TASK_SIZE)
#else
#define TASK_SIZE USER_TOP
#endif

/*
 * This decides where the kernel will search for a free chunk of vm space during
 * mmap's.
 */
#define TASK_UNMAPPED_BASE (TASK_SIZE / 8 * 3)

#define DEFAULT_MAP_WINDOW TASK_SIZE
#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX STACK_TOP

#endif /* _ASM_POWERPC_TASK_SIZE_32_H */
