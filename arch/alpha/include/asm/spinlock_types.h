/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_SPINLOCK_TYPES_H
#define _ALPHA_SPINLOCK_TYPES_H

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
