/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_SPINLOCK_TYPES_H
#define _ASM_RISCV_SPINLOCK_TYPES_H

/* This is horible, but the whole file is going away in the next commit. */
#define __ASM_GENERIC_QRWLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
# error "please don't include this file directly"
#endif

#include <asm-generic/spinlock_types.h>

typedef struct {
	volatile unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#endif /* _ASM_RISCV_SPINLOCK_TYPES_H */
