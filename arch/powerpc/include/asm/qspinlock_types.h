/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_QSPINLOCK_TYPES_H
#define _ASM_POWERPC_QSPINLOCK_TYPES_H

#include <linux/types.h>

typedef struct qspinlock {
	atomic_t val;
} arch_spinlock_t;

#define	__ARCH_SPIN_LOCK_UNLOCKED	{ .val = ATOMIC_INIT(0) }

#endif /* _ASM_POWERPC_QSPINLOCK_TYPES_H */
