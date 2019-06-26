/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TASK_SIZE_64_H
#define _ASM_POWERPC_TASK_SIZE_64_H

/*
 * 64-bit user address space can have multiple limits
 * For now supported values are:
 */
#define TASK_SIZE_64TB  (0x0000400000000000UL)
#define TASK_SIZE_128TB (0x0000800000000000UL)
#define TASK_SIZE_512TB (0x0002000000000000UL)
#define TASK_SIZE_1PB   (0x0004000000000000UL)
#define TASK_SIZE_2PB   (0x0008000000000000UL)

/*
 * With 52 bits in the address we can support up to 4PB of range.
 */
#define TASK_SIZE_4PB   (0x0010000000000000UL)

/*
 * For now 512TB is only supported with book3s and 64K linux page size.
 */
#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_PPC_64K_PAGES)
/*
 * Max value currently used:
 */
#define TASK_SIZE_USER64		TASK_SIZE_4PB
#define DEFAULT_MAP_WINDOW_USER64	TASK_SIZE_128TB
#define TASK_CONTEXT_SIZE		TASK_SIZE_512TB
#else
#define TASK_SIZE_USER64		TASK_SIZE_64TB
#define DEFAULT_MAP_WINDOW_USER64	TASK_SIZE_64TB

/*
 * We don't need to allocate extended context ids for 4K page size, because we
 * limit the max effective address on this config to 64TB.
 */
#define TASK_CONTEXT_SIZE TASK_SIZE_64TB
#endif

/*
 * 32-bit user address space is 4GB - 1 page
 * (this 1 page is needed so referencing of 0xFFFFFFFF generates EFAULT
 */
#define TASK_SIZE_USER32 (0x0000000100000000UL - (1 * PAGE_SIZE))

#define TASK_SIZE_OF(tsk)						\
	(test_tsk_thread_flag(tsk, TIF_32BIT) ? TASK_SIZE_USER32 :	\
						TASK_SIZE_USER64)

#define TASK_SIZE TASK_SIZE_OF(current)

#define TASK_UNMAPPED_BASE_USER32 (PAGE_ALIGN(TASK_SIZE_USER32 / 4))
#define TASK_UNMAPPED_BASE_USER64 (PAGE_ALIGN(DEFAULT_MAP_WINDOW_USER64 / 4))

/*
 * This decides where the kernel will search for a free chunk of vm space during
 * mmap's.
 */
#define TASK_UNMAPPED_BASE	\
	((is_32bit_task()) ? TASK_UNMAPPED_BASE_USER32 : TASK_UNMAPPED_BASE_USER64)

/*
 * Initial task size value for user applications. For book3s 64 we start
 * with 128TB and conditionally enable upto 512TB
 */
#ifdef CONFIG_PPC_BOOK3S_64
#define DEFAULT_MAP_WINDOW	\
	((is_32bit_task()) ? TASK_SIZE_USER32 : DEFAULT_MAP_WINDOW_USER64)
#else
#define DEFAULT_MAP_WINDOW	TASK_SIZE
#endif

#define STACK_TOP_USER64 DEFAULT_MAP_WINDOW_USER64
#define STACK_TOP_USER32 TASK_SIZE_USER32
#define STACK_TOP_MAX TASK_SIZE_USER64
#define STACK_TOP (is_32bit_task() ? STACK_TOP_USER32 : STACK_TOP_USER64)

#endif /* _ASM_POWERPC_TASK_SIZE_64_H */
