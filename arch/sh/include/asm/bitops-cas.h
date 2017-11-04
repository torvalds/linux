/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_BITOPS_CAS_H
#define __ASM_SH_BITOPS_CAS_H

static inline unsigned __bo_cas(volatile unsigned *p, unsigned old, unsigned new)
{
	__asm__ __volatile__("cas.l %1,%0,@r0"
		: "+r"(new)
		: "r"(old), "z"(p)
		: "t", "memory" );
	return new;
}

static inline void set_bit(int nr, volatile void *addr)
{
	unsigned mask, old;
	volatile unsigned *a = addr;

	a += nr >> 5;
	mask = 1U << (nr & 0x1f);

	do old = *a;
	while (__bo_cas(a, old, old|mask) != old);
}

static inline void clear_bit(int nr, volatile void *addr)
{
	unsigned mask, old;
	volatile unsigned *a = addr;

	a += nr >> 5;
	mask = 1U << (nr & 0x1f);

	do old = *a;
	while (__bo_cas(a, old, old&~mask) != old);
}

static inline void change_bit(int nr, volatile void *addr)
{
	unsigned mask, old;
	volatile unsigned *a = addr;

	a += nr >> 5;
	mask = 1U << (nr & 0x1f);

	do old = *a;
	while (__bo_cas(a, old, old^mask) != old);
}

static inline int test_and_set_bit(int nr, volatile void *addr)
{
	unsigned mask, old;
	volatile unsigned *a = addr;

	a += nr >> 5;
	mask = 1U << (nr & 0x1f);

	do old = *a;
	while (__bo_cas(a, old, old|mask) != old);

	return !!(old & mask);
}

static inline int test_and_clear_bit(int nr, volatile void *addr)
{
	unsigned mask, old;
	volatile unsigned *a = addr;

	a += nr >> 5;
	mask = 1U << (nr & 0x1f);

	do old = *a;
	while (__bo_cas(a, old, old&~mask) != old);

	return !!(old & mask);
}

static inline int test_and_change_bit(int nr, volatile void *addr)
{
	unsigned mask, old;
	volatile unsigned *a = addr;

	a += nr >> 5;
	mask = 1U << (nr & 0x1f);

	do old = *a;
	while (__bo_cas(a, old, old^mask) != old);

	return !!(old & mask);
}

#include <asm-generic/bitops/non-atomic.h>

#endif /* __ASM_SH_BITOPS_CAS_H */
