/*
 * bitops.h: Bit string operations on the V9.
 *
 * Copyright 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_BITOPS_H
#define _SPARC64_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/byteorder.h>

extern int test_and_set_bit(unsigned long nr, volatile unsigned long *addr);
extern int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr);
extern int test_and_change_bit(unsigned long nr, volatile unsigned long *addr);
extern void set_bit(unsigned long nr, volatile unsigned long *addr);
extern void clear_bit(unsigned long nr, volatile unsigned long *addr);
extern void change_bit(unsigned long nr, volatile unsigned long *addr);

#include <asm-generic/bitops/non-atomic.h>

#ifdef CONFIG_SMP
#define smp_mb__before_clear_bit()	membar_storeload_loadload()
#define smp_mb__after_clear_bit()	membar_storeload_storestore()
#else
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()
#endif

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffs.h>

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#ifdef ULTRA_HAS_POPULATION_COUNT

static inline unsigned int hweight64(unsigned long w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w));
	return res;
}

static inline unsigned int hweight32(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xffffffff));
	return res;
}

static inline unsigned int hweight16(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xffff));
	return res;
}

static inline unsigned int hweight8(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xff));
	return res;
}

#else

#include <asm-generic/bitops/hweight.h>

#endif
#include <asm-generic/bitops/lock.h>
#endif /* __KERNEL__ */

#include <asm-generic/bitops/find.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(lock,nr,addr) \
	test_and_set_bit((nr) ^ 0x38,(unsigned long *)(addr))
#define ext2_clear_bit_atomic(lock,nr,addr) \
	test_and_clear_bit((nr) ^ 0x38,(unsigned long *)(addr))

#include <asm-generic/bitops/minix.h>

#endif /* __KERNEL__ */

#endif /* defined(_SPARC64_BITOPS_H) */
