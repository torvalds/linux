#ifndef _BLACKFIN_BITOPS_H
#define _BLACKFIN_BITOPS_H

#ifndef CONFIG_SMP
# include <asm-generic/bitops.h>
#else

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */

#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffz.h>

#include <linux/linkage.h>

asmlinkage int __raw_bit_set_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_clear_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_toggle_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_set_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_clear_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_toggle_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_asm(const volatile unsigned long *addr, int nr);

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_set_asm(a, nr & 0x1f);
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_clear_asm(a, nr & 0x1f);
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_toggle_asm(a, nr & 0x1f);
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	volatile const unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_asm(a, nr & 0x1f) != 0;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_set_asm(a, nr & 0x1f);
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_clear_asm(a, nr & 0x1f);
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_toggle_asm(a, nr & 0x1f);
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

#include <asm-generic/bitops/non-atomic.h>

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/ext2-non-atomic.h>

#include <asm-generic/bitops/minix.h>

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#endif /* CONFIG_SMP */

#endif				/* _BLACKFIN_BITOPS_H */
