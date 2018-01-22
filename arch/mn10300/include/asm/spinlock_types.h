/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SPINLOCK_TYPES_H
#define _ASM_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

typedef struct arch_spinlock {
	unsigned int slock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

typedef struct {
	unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ RW_LOCK_BIAS }

#endif /* _ASM_SPINLOCK_TYPES_H */
