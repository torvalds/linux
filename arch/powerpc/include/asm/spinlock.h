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

#ifndef CONFIG_PARAVIRT_SPINLOCKS
static inline void pv_spinlocks_init(void) { }
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
