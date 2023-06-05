/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SYNC_BITOPS_H__
#define __ASM_SYNC_BITOPS_H__

#include <asm/bitops.h>

/* sync_bitops functions are equivalent to the SMP implementation of the
 * original functions, independently from CONFIG_SMP being defined.
 *
 * We need them because _set_bit etc are not SMP safe if !CONFIG_SMP. But
 * under Xen you might be communicating with a completely external entity
 * who might be on another CPU (e.g. two uniprocessor guests communicating
 * via event channels and grant tables). So we need a variant of the bit
 * ops which are SMP safe even on a UP kernel.
 */

/*
 * Unordered
 */

#define sync_set_bit(nr, p)		_set_bit(nr, p)
#define sync_clear_bit(nr, p)		_clear_bit(nr, p)
#define sync_change_bit(nr, p)		_change_bit(nr, p)
#define sync_test_bit(nr, addr)		test_bit(nr, addr)

/*
 * Fully ordered
 */

int _sync_test_and_set_bit(int nr, volatile unsigned long * p);
#define sync_test_and_set_bit(nr, p)	_sync_test_and_set_bit(nr, p)

int _sync_test_and_clear_bit(int nr, volatile unsigned long * p);
#define sync_test_and_clear_bit(nr, p)	_sync_test_and_clear_bit(nr, p)

int _sync_test_and_change_bit(int nr, volatile unsigned long * p);
#define sync_test_and_change_bit(nr, p)	_sync_test_and_change_bit(nr, p)

#define arch_sync_cmpxchg(ptr, old, new)				\
({									\
	__typeof__(*(ptr)) __ret;					\
	__smp_mb__before_atomic();					\
	__ret = arch_cmpxchg_relaxed((ptr), (old), (new));		\
	__smp_mb__after_atomic();					\
	__ret;								\
})

#endif
