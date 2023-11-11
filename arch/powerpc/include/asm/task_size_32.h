/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TASK_SIZE_32_H
#define _ASM_POWERPC_TASK_SIZE_32_H

#if CONFIG_TASK_SIZE > CONFIG_KERNEL_START
#error User TASK_SIZE overlaps with KERNEL_START address
#endif

#define TASK_SIZE (CONFIG_TASK_SIZE)

/*
 * This decides where the kernel will search for a free chunk of vm space during
 * mmap's.
 */
#define TASK_UNMAPPED_BASE (TASK_SIZE / 8 * 3)

#define DEFAULT_MAP_WINDOW TASK_SIZE
#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX STACK_TOP

#endif /* _ASM_POWERPC_TASK_SIZE_32_H */
