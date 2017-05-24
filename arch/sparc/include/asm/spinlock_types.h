#ifndef __SPARC_SPINLOCK_TYPES_H
#define __SPARC_SPINLOCK_TYPES_H

typedef struct {
	volatile unsigned char lock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

#ifdef CONFIG_QUEUED_RWLOCKS
#include <asm-generic/qrwlock_types.h>
#else
typedef struct {
	volatile unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }
#endif /* CONFIG_QUEUED_RWLOCKS */
#endif
