/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SPINLOCK_TYPES_H
#define _ASM_X86_SPINLOCK_TYPES_H

#include <linux/types.h>

#ifdef CONFIG_PARAVIRT_SPINLOCKS
#define __TICKET_LOCK_INC	2
#define TICKET_SLOWPATH_FLAG	((__ticket_t)1)
#else
#define __TICKET_LOCK_INC	1
#define TICKET_SLOWPATH_FLAG	((__ticket_t)0)
#endif

#if (CONFIG_NR_CPUS < (256 / __TICKET_LOCK_INC))
typedef u8  __ticket_t;
typedef u16 __ticketpair_t;
#else
typedef u16 __ticket_t;
typedef u32 __ticketpair_t;
#endif

#define TICKET_LOCK_INC	((__ticket_t)__TICKET_LOCK_INC)

#define TICKET_SHIFT	(sizeof(__ticket_t) * 8)

#include <asm-generic/qspinlock_types.h>

#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_X86_SPINLOCK_TYPES_H */
