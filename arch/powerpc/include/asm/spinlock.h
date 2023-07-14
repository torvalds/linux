/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H
#ifdef __KERNEL__

#ifdef CONFIG_PPC_QUEUED_SPINLOCKS
#include <asm/qspinlock.h>
#include <asm/qrwlock.h>
#else
#include <asm/simple_spinlock.h>
#endif

/* See include/linux/spinlock.h */
#define smp_mb__after_spinlock()	smp_mb()

#ifndef CONFIG_PPC_QUEUED_SPINLOCKS
static inline void pv_spinlocks_init(void) { }
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
