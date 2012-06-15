/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 06, 07 by Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/irqflags.h>
#include <asm/war.h>

static inline unsigned long __xchg_u32(volatile int * m, unsigned int val)
{
	__u32 retval;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		unsigned long dummy;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %3			# xchg_u32	\n"
		"	.set	mips0					\n"
		"	move	%2, %z4					\n"
		"	.set	mips3					\n"
		"	sc	%2, %1					\n"
		"	beqzl	%2, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else if (kernel_uses_llsc) {
		unsigned long dummy;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	ll	%0, %3		# xchg_u32	\n"
			"	.set	mips0				\n"
			"	move	%2, %z4				\n"
			"	.set	mips3				\n"
			"	sc	%2, %1				\n"
			"	.set	mips0				\n"
			: "=&r" (retval), "=m" (*m), "=&r" (dummy)
			: "R" (*m), "Jr" (val)
			: "memory");
		} while (unlikely(!dummy));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		retval = *m;
		*m = val;
		raw_local_irq_restore(flags);	/* implies memory barrier  */
	}

	smp_llsc_mb();

	return retval;
}

#ifdef CONFIG_64BIT
static inline __u64 __xchg_u64(volatile __u64 * m, __u64 val)
{
	__u64 retval;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		unsigned long dummy;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%0, %3			# xchg_u64	\n"
		"	move	%2, %z4					\n"
		"	scd	%2, %1					\n"
		"	beqzl	%2, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else if (kernel_uses_llsc) {
		unsigned long dummy;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	lld	%0, %3		# xchg_u64	\n"
			"	move	%2, %z4				\n"
			"	scd	%2, %1				\n"
			"	.set	mips0				\n"
			: "=&r" (retval), "=m" (*m), "=&r" (dummy)
			: "R" (*m), "Jr" (val)
			: "memory");
		} while (unlikely(!dummy));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		retval = *m;
		*m = val;
		raw_local_irq_restore(flags);	/* implies memory barrier  */
	}

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
	}

	return x;
}

#define xchg(ptr, x)							\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) & ~0xc);				\
									\
	((__typeof__(*(ptr)))						\
		__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))));	\
})

#define __HAVE_ARCH_CMPXCHG 1

#define __cmpxchg_asm(ld, st, m, old, new)				\
({									\
	__typeof(*(m)) __ret;						\
									\
	if (kernel_uses_llsc && R10000_LLSC_WAR) {			\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	mips3				\n"	\
		"1:	" ld "	%0, %2		# __cmpxchg_asm	\n"	\
		"	bne	%0, %z3, 2f			\n"	\
		"	.set	mips0				\n"	\
		"	move	$1, %z4				\n"	\
		"	.set	mips3				\n"	\
		"	" st "	$1, %1				\n"	\
		"	beqzl	$1, 1b				\n"	\
		"2:						\n"	\
		"	.set	pop				\n"	\
		: "=&r" (__ret), "=R" (*m)				\
		: "R" (*m), "Jr" (old), "Jr" (new)			\
		: "memory");						\
	} else if (kernel_uses_llsc) {					\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	mips3				\n"	\
		"1:	" ld "	%0, %2		# __cmpxchg_asm	\n"	\
		"	bne	%0, %z3, 2f			\n"	\
		"	.set	mips0				\n"	\
		"	move	$1, %z4				\n"	\
		"	.set	mips3				\n"	\
		"	" st "	$1, %1				\n"	\
		"	beqz	$1, 1b				\n"	\
		"	.set	pop				\n"	\
		"2:						\n"	\
		: "=&r" (__ret), "=R" (*m)				\
		: "R" (*m), "Jr" (old), "Jr" (new)			\
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

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg().
 */
extern void __cmpxchg_called_with_bad_pointer(void);

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
		__res = __cmpxchg_asm("ll", "sc", __ptr, __old, __new);	\
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

#define cmpxchg64(ptr, o, n)						\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg((ptr), (o), (n));					\
  })

#ifdef CONFIG_64BIT
#define cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
  })
#else
#include <asm-generic/cmpxchg-local.h>
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))
#endif

#endif /* __ASM_CMPXCHG_H */
