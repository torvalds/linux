/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 06, 07 by Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/bug.h>
#include <linux/irqflags.h>
#include <asm/compiler.h>
#include <asm/war.h>

/*
 * Using a branch-likely instruction to check the result of an sc instruction
 * works around a bug present in R10000 CPUs prior to revision 3.0 that could
 * cause ll-sc sequences to execute non-atomically.
 */
#if R10000_LLSC_WAR
# define __scbeqz "beqzl"
#else
# define __scbeqz "beqz"
#endif

/*
 * These functions doesn't exist, so if they are called you'll either:
 *
 * - Get an error at compile-time due to __compiletime_error, if supported by
 *   your compiler.
 *
 * or:
 *
 * - Get an error at link-time due to the call to the missing function.
 */
extern void __cmpxchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for cmpxchg");
extern unsigned long __xchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for xchg");

#define __xchg_asm(ld, st, m, val)					\
({									\
	__typeof(*(m)) __ret;						\
									\
	if (kernel_uses_llsc) {						\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	" MIPS_ISA_ARCH_LEVEL "		\n"	\
		"1:	" ld "	%0, %2		# __xchg_asm	\n"	\
		"	.set	mips0				\n"	\
		"	move	$1, %z3				\n"	\
		"	.set	" MIPS_ISA_ARCH_LEVEL "		\n"	\
		"	" st "	$1, %1				\n"	\
		"\t" __scbeqz "	$1, 1b				\n"	\
		"	.set	pop				\n"	\
		: "=&r" (__ret), "=" GCC_OFF_SMALL_ASM() (*m)		\
		: GCC_OFF_SMALL_ASM() (*m), "Jr" (val)			\
		: "memory");						\
	} else {							\
		unsigned long __flags;					\
									\
		raw_local_irq_save(__flags);				\
		__ret = *m;						\
		*m = val;						\
		raw_local_irq_restore(__flags);				\
	}								\
									\
	__ret;								\
})

static inline unsigned long __xchg_u32(volatile int * m, unsigned int val)
{
	__u32 retval;

	smp_mb__before_llsc();
	retval = __xchg_asm("ll", "sc", m, val);
	smp_llsc_mb();

	return retval;
}

#ifdef CONFIG_64BIT
static inline __u64 __xchg_u64(volatile __u64 * m, __u64 val)
{
	__u64 retval;

	smp_mb__before_llsc();
	retval = __xchg_asm("lld", "scd", m, val);
	smp_llsc_mb();

	return retval;
}
#else
extern __u64 __xchg_u64_unsupported_on_32bit_kernels(volatile __u64 * m, __u64 val);
#define __xchg_u64 __xchg_u64_unsupported_on_32bit_kernels
#endif

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	case 4:
		return __xchg_u32(ptr, x);
	case 8:
		return __xchg_u64(ptr, x);
	default:
		return __xchg_called_with_bad_pointer();
	}
}

#define xchg(ptr, x)							\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) & ~0xc);				\
									\
	((__typeof__(*(ptr)))						\
		__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))));	\
})

#define __cmpxchg_asm(ld, st, m, old, new)				\
({									\
	__typeof(*(m)) __ret;						\
									\
	if (kernel_uses_llsc) {						\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"	\
		"1:	" ld "	%0, %2		# __cmpxchg_asm \n"	\
		"	bne	%0, %z3, 2f			\n"	\
		"	.set	mips0				\n"	\
		"	move	$1, %z4				\n"	\
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"	\
		"	" st "	$1, %1				\n"	\
		"\t" __scbeqz "	$1, 1b				\n"	\
		"	.set	pop				\n"	\
		"2:						\n"	\
		: "=&r" (__ret), "=" GCC_OFF_SMALL_ASM() (*m)		\
		: GCC_OFF_SMALL_ASM() (*m), "Jr" (old), "Jr" (new)		\
		: "memory");						\
	} else {							\
		unsigned long __flags;					\
									\
		raw_local_irq_save(__flags);				\
		__ret = *m;						\
		if (__ret == old)					\
			*m = new;					\
		raw_local_irq_restore(__flags);				\
	}								\
									\
	__ret;								\
})

#define __cmpxchg(ptr, old, new, pre_barrier, post_barrier)		\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	__typeof__(*(ptr)) __old = (old);				\
	__typeof__(*(ptr)) __new = (new);				\
	__typeof__(*(ptr)) __res = 0;					\
									\
	pre_barrier;							\
									\
	switch (sizeof(*(__ptr))) {					\
	case 4:								\
		__res = __cmpxchg_asm("ll", "sc", __ptr, __old, __new); \
		break;							\
	case 8:								\
		if (sizeof(long) == 8) {				\
			__res = __cmpxchg_asm("lld", "scd", __ptr,	\
					   __old, __new);		\
			break;						\
		}							\
	default:							\
		__cmpxchg_called_with_bad_pointer();			\
		break;							\
	}								\
									\
	post_barrier;							\
									\
	__res;								\
})

#define cmpxchg(ptr, old, new)		__cmpxchg(ptr, old, new, smp_mb__before_llsc(), smp_llsc_mb())
#define cmpxchg_local(ptr, old, new)	__cmpxchg(ptr, old, new, , )

#ifdef CONFIG_64BIT
#define cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
  })

#define cmpxchg64(ptr, o, n)						\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg((ptr), (o), (n));					\
  })
#else
#include <asm-generic/cmpxchg-local.h>
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))
#define cmpxchg64(ptr, o, n) cmpxchg64_local((ptr), (o), (n))
#endif

#undef __scbeqz

#endif /* __ASM_CMPXCHG_H */
