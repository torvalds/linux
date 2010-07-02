#ifndef _ASM_X86_SPINLOCK_TYPES_H
#define _ASM_X86_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

#include <linux/types.h>

#if (CONFIG_NR_CPUS < 256)
typedef u8  __ticket_t;
#else
typedef u16 __ticket_t;
#endif

#define TICKET_SHIFT	(sizeof(__ticket_t) * 8)
#define TICKET_MASK	((__ticket_t)((1 << TICKET_SHIFT) - 1))

typedef struct arch_spinlock {
	union {
		unsigned int slock;
		struct __raw_tickets {
			__ticket_t head, tail;
		} tickets;
	};
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ { .slock = 0 } }

#include <asm/rwlock.h>

#endif /* _ASM_X86_SPINLOCK_TYPES_H */
