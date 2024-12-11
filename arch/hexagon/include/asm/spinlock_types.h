/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Spinlock support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_SPINLOCK_TYPES_H
#define _ASM_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
# error "Please do not include this file directly."
#endif

typedef struct {
	volatile unsigned int lock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

typedef struct {
	volatile unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#endif
