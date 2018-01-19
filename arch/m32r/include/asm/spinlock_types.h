/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M32R_SPINLOCK_TYPES_H
#define _ASM_M32R_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

typedef struct {
	volatile int slock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 1 }

typedef struct {
	volatile int lock;
} arch_rwlock_t;

#define RW_LOCK_BIAS			0x01000000
#define RW_LOCK_BIAS_STR		"0x01000000"

#define __ARCH_RW_LOCK_UNLOCKED		{ RW_LOCK_BIAS }

#endif /* _ASM_M32R_SPINLOCK_TYPES_H */
