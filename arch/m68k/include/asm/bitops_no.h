#ifndef _M68KNOMMU_BITOPS_H
#define _M68KNOMMU_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#if defined (__mcfisaaplus__) || defined (__mcfisac__)
static inline int ffs(unsigned int val)
{
        if (!val)
                return 0;

        asm volatile(
                        "bitrev %0\n\t"
                        "ff1 %0\n\t"
                        : "=d" (val)
                        : "0" (val)
		    );
        val++;
        return val;
}

static inline int __ffs(unsigned int val)
{
        asm volatile(
                        "bitrev %0\n\t"
                        "ff1 %0\n\t"
                        : "=d" (val)
                        : "0" (val)
		    );
        return val;
}

#else
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#endif

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffz.h>

static __inline__ void set_bit(int nr, volatile unsigned long * addr)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %0,%%a0; bset %1,(%%a0)"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0", "cc");
#else
	__asm__ __volatile__ ("bset %1,%0"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     : "cc");
#endif
}

#define __set_bit(nr, addr) set_bit(nr, addr)

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

static __inline__ void clear_bit(int nr, volatile unsigned long * addr)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %0,%%a0; bclr %1,(%%a0)"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0", "cc");
#else
	__asm__ __volatile__ ("bclr %1,%0"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     : "cc");
#endif
}

#define __clear_bit(nr, addr) clear_bit(nr, addr)

static __inline__ void change_bit(int nr, volatile unsigned long * addr)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %0,%%a0; bchg %1,(%%a0)"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0", "cc");
#else
	__asm__ __volatile__ ("bchg %1,%0"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     : "cc");
#endif
}

#define __change_bit(nr, addr) change_bit(nr, addr)

static __inline__ int test_and_set_bit(int nr, volatile unsigned long * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bset %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bset %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define __test_and_set_bit(nr, addr) test_and_set_bit(nr, addr)

static __inline__ int test_and_clear_bit(int nr, volatile unsigned long * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bclr %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bclr %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define __test_and_clear_bit(nr, addr) test_and_clear_bit(nr, addr)

static __inline__ int test_and_change_bit(int nr, volatile unsigned long * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0\n\tbchg %2,(%%a0)\n\tsne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bchg %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define __test_and_change_bit(nr, addr) test_and_change_bit(nr, addr)

/*
 * This routine doesn't need to be atomic.
 */
static __inline__ int __constant_test_bit(int nr, const volatile unsigned long * addr)
{
	return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static __inline__ int __test_bit(int nr, const volatile unsigned long * addr)
{
	int 	* a = (int *) addr;
	int	mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 __constant_test_bit((nr),(addr)) : \
 __test_bit((nr),(addr)))

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#define BITOP_LE_SWIZZLE	((BITS_PER_LONG-1) & ~0x7)

static inline void __set_bit_le(int nr, void *addr)
{
	__set_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline void __clear_bit_le(int nr, void *addr)
{
	__clear_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline int __test_and_set_bit_le(int nr, volatile void *addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bset %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bset %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

static inline int __test_and_clear_bit_le(int nr, volatile void *addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bclr %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bclr %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define ext2_set_bit_atomic(lock, nr, addr)		\
	({						\
		int ret;				\
		spin_lock(lock);			\
		ret = __test_and_set_bit_le((nr), (addr));	\
		spin_unlock(lock);			\
		ret;					\
	})

#define ext2_clear_bit_atomic(lock, nr, addr)		\
	({						\
		int ret;				\
		spin_lock(lock);			\
		ret = __test_and_clear_bit_le((nr), (addr));	\
		spin_unlock(lock);			\
		ret;					\
	})

static inline int test_bit_le(int nr, const volatile void *addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; btst %2,(%%a0); sne %0"
	     : "=d" (retval)
	     : "m" (((const volatile char *)addr)[nr >> 3]), "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("btst %2,%1; sne %0"
	     : "=d" (retval)
	     : "m" (((const volatile char *)addr)[nr >> 3]), "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define find_first_zero_bit_le(addr, size)	\
	find_next_zero_bit_le((addr), (size), 0)

static inline unsigned long find_next_zero_bit_le(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		/* We hold the little endian value in tmp, but then the
		 * shift is illegal. So we could keep a big endian value
		 * in tmp, like this:
		 *
		 * tmp = __swab32(*(p++));
		 * tmp |= ~0UL >> (32-offset);
		 *
		 * but this would decrease performance, so we change the
		 * shift:
		 */
		tmp = *(p++);
		tmp |= __swab32(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	/* tmp is little endian, so we would have to swab the shift,
	 * see above. But then we have to swab tmp below for ffz, so
	 * we might as well do this here.
	 */
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}

#endif /* __KERNEL__ */

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#endif /* _M68KNOMMU_BITOPS_H */
