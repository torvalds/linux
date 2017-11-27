/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/barrier.h>
#include <asm/processor.h>

#ifdef CONFIG_METAG_ATOMICITY_LOCK1
#include <asm/spinlock_lock1.h>
#else
#include <asm/spinlock_lnkget.h>
#endif

/*
 * both lock1 and lnkget are test-and-set spinlocks with 0 unlocked and 1
 * locked.
 */

#endif /* __ASM_SPINLOCK_H */
