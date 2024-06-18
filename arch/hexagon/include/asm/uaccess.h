/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * User memory access support for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H
/*
 * User space memory access functions
 */
#include <asm/sections.h>

/*
 * When a kernel-mode page fault is taken, the faulting instruction
 * address is checked against a table of exception_table_entries.
 * Each entry is a tuple of the address of an instruction that may
 * be authorized to fault, and the address at which execution should
 * be resumed instead of the faulting instruction, so as to effect
 * a workaround.
 */

/*  Assembly somewhat optimized copy routines  */
unsigned long raw_copy_from_user(void *to, const void __user *from,
				     unsigned long n);
unsigned long raw_copy_to_user(void __user *to, const void *from,
				   unsigned long n);
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

__kernel_size_t __clear_user_hexagon(void __user *dest, unsigned long count);
#define __clear_user(a, s) __clear_user_hexagon((a), (s))

#include <asm-generic/uaccess.h>


#endif
