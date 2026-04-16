/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/bits.h>
#include <linux/build_bug.h>
#include <asm/barrier.h>
#include <asm/cpu-features.h>

#define __xchg_amo_asm(amswap_db, m, val)	\
({						\
	__typeof(val) __ret;			\
						\
	__asm__ __volatile__ (			\
	" "amswap_db" %1, %z2, %0 \n"		\
	: "+ZB" (*m), "=&r" (__ret)		\
	: "Jr" (val)				\
	: "memory");				\
						\
	__ret;					\
})

#define __xchg_llsc_asm(ld, st, m, val)			\
({							\
	__typeof(val) __ret, __tmp;			\
							\
	asm volatile (					\
	"1:	ll.w	%0, %3		\n"		\
	"	move	%1, %z4		\n"		\
	"	sc.w	%1, %2		\n"		\
	"	beqz	%1, 1b		\n"		\
	: "=&r" (__ret), "=&r" (__tmp), "=ZC" (*m)	\
	: "ZC" (*m), "Jr" (val)				\
	: "memory");					\
							\
	__ret;						\
})

static inline unsigned int __xchg_small(volatile void *ptr, unsigned int val,
					unsigned int size)
{
	unsigned int shift;
	u32 old32, mask, temp;
	volatile u32 *ptr32;

	/* Mask value to the correct size. */
	mask = GENMASK((size * BITS_PER_BYTE) - 1, 0);
	val &= mask;

	/*
	 * Calculate a shift & mask that correspond to the value we wish to
	 * exchange within the naturally aligned 4 byte integerthat includes
	 * it.
	 */
	shift = (unsigned long)ptr & 0x3;
	shift *= BITS_PER_BYTE;
	mask <<= shift;

	/*
	 * Calculate a pointer to the naturally aligned 4 byte integer that
	 * includes our byte of interest, and load its value.
	 */
	ptr32 = (volatile u32 *)((unsigned long)ptr & ~0x3);

	asm volatile (
	"1:	ll.w		%0, %3		\n"
	"	andn		%1, %0, %z4	\n"
	"	or		%1, %1, %z5	\n"
	"	sc.w		%1, %2		\n"
	"	beqz		%1, 1b		\n"
	: "=&r" (old32), "=&r" (temp), "=ZC" (*ptr32)
	: "ZC" (*ptr32), "Jr" (mask), "Jr" (val << shift)
	: "memory");

	return (old32 & mask) >> shift;
}

static __always_inline unsigned long
__arch_xchg(volatile void *ptr, unsigned long x, int size)
{
	switch (size) {
	case 1:
	case 2:
		return __xchg_small((volatile void *)ptr, x, size);

	case 4:
#ifdef CONFIG_CPU_HAS_AMO
		return __xchg_amo_asm("amswap_db.w", (volatile u32 *)ptr, (u32)x);
#else
		return __xchg_llsc_asm("ll.w", "sc.w", (volatile u32 *)ptr, (u32)x);
#endif /* CONFIG_CPU_HAS_AMO */

#ifdef CONFIG_64BIT
	case 8:
#ifdef CONFIG_CPU_HAS_AMO
		return __xchg_amo_asm("amswap_db.d", (volatile u64 *)ptr, (u64)x);
#else
		return __xchg_llsc_asm("ll.d", "sc.d", (volatile u64 *)ptr, (u64)x);
#endif /* CONFIG_CPU_HAS_AMO */
#endif /* CONFIG_64BIT */

	default:
		BUILD_BUG();
	}

	return 0;
}

#define arch_xchg(ptr, x)						\
({									\
	__typeof__(*(ptr)) __res;					\
									\
	__res = (__typeof__(*(ptr)))					\
		__arch_xchg((ptr), (unsigned long)(x), sizeof(*(ptr)));	\
									\
	__res;								\
})

#define __cmpxchg_asm(ld, st, m, old, new)				\
({									\
	__typeof(old) __ret;						\
									\
	__asm__ __volatile__(						\
	"1:	" ld "	%0, %2		# __cmpxchg_asm \n"		\
	"	bne	%0, %z3, 2f			\n"		\
	"	move	$t0, %z4			\n"		\
	"	" st "	$t0, %1				\n"		\
	"	beqz	$t0, 1b				\n"		\
	"2:						\n"		\
	__WEAK_LLSC_MB							\
	: "=&r" (__ret), "=ZB"(*m)					\
	: "ZB"(*m), "Jr" (old), "Jr" (new)				\
	: "t0", "memory");						\
									\
	__ret;								\
})

static inline unsigned int __cmpxchg_small(volatile void *ptr, unsigned int old,
					   unsigned int new, unsigned int size)
{
	unsigned int shift;
	u32 old32, mask, temp;
	volatile u32 *ptr32;

	/* Mask inputs to the correct size. */
	mask = GENMASK((size * BITS_PER_BYTE) - 1, 0);
	old &= mask;
	new &= mask;

	/*
	 * Calculate a shift & mask that correspond to the value we wish to
	 * compare & exchange within the naturally aligned 4 byte integer
	 * that includes it.
	 */
	shift = (unsigned long)ptr & 0x3;
	shift *= BITS_PER_BYTE;
	old <<= shift;
	new <<= shift;
	mask <<= shift;

	/*
	 * Calculate a pointer to the naturally aligned 4 byte integer that
	 * includes our byte of interest, and load its value.
	 */
	ptr32 = (volatile u32 *)((unsigned long)ptr & ~0x3);

	asm volatile (
	"1:	ll.w		%0, %3		\n"
	"	and		%1, %0, %z4	\n"
	"	bne		%1, %z5, 2f	\n"
	"	andn		%1, %0, %z4	\n"
	"	or		%1, %1, %z6	\n"
	"	sc.w		%1, %2		\n"
	"	beqz		%1, 1b		\n"
	"	b		3f		\n"
	"2:					\n"
	__WEAK_LLSC_MB
	"3:					\n"
	: "=&r" (old32), "=&r" (temp), "=ZC" (*ptr32)
	: "ZC" (*ptr32), "Jr" (mask), "Jr" (old), "Jr" (new)
	: "memory");

	return (old32 & mask) >> shift;
}

static __always_inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, unsigned int size)
{
	switch (size) {
	case 1:
	case 2:
		return __cmpxchg_small(ptr, old, new, size);

	case 4:
		return __cmpxchg_asm("ll.w", "sc.w", (volatile u32 *)ptr,
				     (u32)old, new);

	case 8:
		return __cmpxchg_asm("ll.d", "sc.d", (volatile u64 *)ptr,
				     (u64)old, new);

	default:
		BUILD_BUG();
	}

	return 0;
}

#define arch_cmpxchg_local(ptr, old, new)				\
	((__typeof__(*(ptr)))						\
		__cmpxchg((ptr),					\
			  (unsigned long)(__typeof__(*(ptr)))(old),	\
			  (unsigned long)(__typeof__(*(ptr)))(new),	\
			  sizeof(*(ptr))))

#define arch_cmpxchg(ptr, old, new)					\
({									\
	__typeof__(*(ptr)) __res;					\
									\
	__res = arch_cmpxchg_local((ptr), (old), (new));		\
									\
	__res;								\
})

#ifdef CONFIG_64BIT
#define arch_cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_local((ptr), (o), (n));				\
  })

#define arch_cmpxchg64(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg((ptr), (o), (n));					\
  })

union __u128_halves {
	u128 full;
	struct {
		u64 low;
		u64 high;
	};
};

#define system_has_cmpxchg128()	cpu_opt(LOONGARCH_CPU_SCQ)

#define __arch_cmpxchg128(ptr, old, new, llsc_mb)			\
({									\
	union __u128_halves __old, __new, __ret;			\
	volatile u64 *__ptr = (volatile u64 *)(ptr);			\
									\
	__old.full = (old);                                             \
	__new.full = (new);						\
									\
	__asm__ __volatile__(						\
	"1:   ll.d    %0, %3		# 128-bit cmpxchg low	\n"	\
	llsc_mb								\
	"     ld.d    %1, %4		# 128-bit cmpxchg high	\n"	\
	"     move    $t0, %0					\n"	\
	"     move    $t1, %1					\n"	\
	"     bne     %0, %z5, 2f				\n"	\
	"     bne     %1, %z6, 2f				\n"	\
	"     move    $t0, %z7					\n"	\
	"     move    $t1, %z8					\n"	\
	"2:   sc.q    $t0, $t1, %2				\n"	\
	"     beqz    $t0, 1b					\n"	\
	llsc_mb								\
	: "=&r" (__ret.low), "=&r" (__ret.high)				\
	: "r" (__ptr),							\
	  "ZC" (__ptr[0]), "m" (__ptr[1]),				\
	  "Jr" (__old.low), "Jr" (__old.high),				\
	  "Jr" (__new.low), "Jr" (__new.high)				\
	: "t0", "t1", "memory");					\
									\
	__ret.full;							\
})

#define arch_cmpxchg128(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 16);				\
	__arch_cmpxchg128(ptr, o, n, __WEAK_LLSC_MB);			\
})

#define arch_cmpxchg128_local(ptr, o, n)				\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 16);				\
	__arch_cmpxchg128(ptr, o, n, "");				\
})
#else
#include <asm-generic/cmpxchg-local.h>
#define arch_cmpxchg64_local(ptr, o, n) __generic_cmpxchg64_local((ptr), (o), (n))
#define arch_cmpxchg64(ptr, o, n) arch_cmpxchg64_local((ptr), (o), (n))
#endif

#endif /* __ASM_CMPXCHG_H */
