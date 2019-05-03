/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_SPINLOCK_TYPES_H
#define __ASM_CSKY_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

#define TICKET_NEXT	16

typedef struct {
	union {
		u32 lock;
		struct __raw_tickets {
			/* little endian */
			u16 owner;
			u16 next;
		} tickets;
	};
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ { 0 } }

#ifdef CONFIG_QUEUED_RWLOCKS
#include <asm-generic/qrwlock_types.h>

#else /* CONFIG_NR_CPUS > 2 */

typedef struct {
	u32 lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#endif /* CONFIG_QUEUED_RWLOCKS */
#endif /* __ASM_CSKY_SPINLOCK_TYPES_H */
