/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_QSPINLOCK_TYPES_H
#define _ASM_POWERPC_QSPINLOCK_TYPES_H

#include <linux/types.h>
#include <asm/byteorder.h>

typedef struct qspinlock {
	union {
		u32 val;

#ifdef __LITTLE_ENDIAN
		struct {
			u16	locked;
			u8	reserved[2];
		};
#else
		struct {
			u8	reserved[2];
			u16	locked;
		};
#endif
	};
} arch_spinlock_t;

#define	__ARCH_SPIN_LOCK_UNLOCKED	{ { .val = 0 } }

/*
 * Bitfields in the lock word:
 *
 *     0: locked bit
 *  1-14: lock holder cpu
 *    15: lock owner or queuer vcpus observed to be preempted bit
 *    16: must queue bit
 * 17-31: tail cpu (+1)
 */
#define	_Q_SET_MASK(type)	(((1U << _Q_ ## type ## _BITS) - 1)\
				      << _Q_ ## type ## _OFFSET)
/* 0x00000001 */
#define _Q_LOCKED_OFFSET	0
#define _Q_LOCKED_BITS		1
#define _Q_LOCKED_VAL		(1U << _Q_LOCKED_OFFSET)

/* 0x00007ffe */
#define _Q_OWNER_CPU_OFFSET	1
#define _Q_OWNER_CPU_BITS	14
#define _Q_OWNER_CPU_MASK	_Q_SET_MASK(OWNER_CPU)

#if CONFIG_NR_CPUS > (1U << _Q_OWNER_CPU_BITS)
#error "qspinlock does not support such large CONFIG_NR_CPUS"
#endif

/* 0x00008000 */
#define _Q_SLEEPY_OFFSET	15
#define _Q_SLEEPY_BITS		1
#define _Q_SLEEPY_VAL		(1U << _Q_SLEEPY_OFFSET)

/* 0x00010000 */
#define _Q_MUST_Q_OFFSET	16
#define _Q_MUST_Q_BITS		1
#define _Q_MUST_Q_VAL		(1U << _Q_MUST_Q_OFFSET)

/* 0xfffe0000 */
#define _Q_TAIL_CPU_OFFSET	17
#define _Q_TAIL_CPU_BITS	15
#define _Q_TAIL_CPU_MASK	_Q_SET_MASK(TAIL_CPU)

#if CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS)
#error "qspinlock does not support such large CONFIG_NR_CPUS"
#endif

#endif /* _ASM_POWERPC_QSPINLOCK_TYPES_H */
