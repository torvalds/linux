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
extern unsigned long __cmpxchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for cmpxchg");
extern unsigned long __cmpxchg64_unsupported(void)
	__compiletime_error("cmpxchg64 not available; cpu_has_64bits may be false");
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
		"	.set	push				\n"	\
		"	.set	" MIPS_ISA_ARCH_LEVEL "		\n"	\
		"1:	" ld "	%0, %2		# __xchg_asm	\n"	\
		"	.set	pop				\n"	\
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

extern unsigned long __xchg_small(volatile void *ptr, unsigned long val,
				  unsigned int size);

static inline unsigned long __xchg(volatile void *ptr, unsigned long x,
				   int size)
{
	switch (size) {
	case 1:
	case 2:
		return __xchg_small(ptr, x, size);

	case 4:
		return __xchg_asm("ll", "sc", (volatile u32 *)ptr, x);

	case 8:
		if (!IS_ENABLED(CONFIG_64BIT))
			return __xchg_called_with_bad_pointer();

		return __xchg_asm("lld", "scd", (volatile u64 *)ptr, x);

	default:
		return __xchg_called_with_bad_pointer();
	}
}

#define xchg(ptr, x)							\
({									\
	__typeof__(*(ptr)) __res;					\
									\
	smp_mb__before_llsc();						\
									\
	__res = (__typeof__(*(ptr)))					\
		__xchg((ptr), (unsigned long)(x), sizeof(*(ptr)));	\
									\
	smp_llsc_mb();							\
									\
	__res;								\
})

#define __cmpxchg_asm(ld, st, m, old, new)				\
({									\
	__typeof(*(m)) __ret;						\
									\
	if (kernel_uses_llsc) {						\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	push				\n"	\
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"	\
		"1:	" ld "	%0, %2		# __cmpxchg_asm \n"	\
		"	bne	%0, %z3, 2f			\n"	\
		"	.set	pop				\n"	\
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

extern unsigned long __cmpxchg_small(volatile void *ptr, unsigned long old,
				     unsigned long new, unsigned int size);

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, unsigned int size)
{
	switch (size) {
	case 1:
	case 2:
		return __cmpxchg_small(ptr, old, new, size);

	case 4:
		return __cmpxchg_asm("ll", "sc", (volatile u32 *)ptr,
				     (u32)old, new);

	case 8:
		/* lld/scd are only available for MIPS64 */
		if (!IS_ENABLED(CONFIG_64BIT))
			return __cmpxchg_called_with_bad_pointer();

		return __cmpxchg_asm("lld", "scd", (volatile u64 *)ptr,
				     (u64)old, new);

	default:
		return __cmpxchg_called_with_bad_pointer();
	}
}

#define cmpxchg_local(ptr, old, new)					\
	((__typeof__(*(ptr)))						\
		__cmpxchg((ptr),					\
			  (unsigned long)(__typeof__(*(ptr)))(old),	\
			  (unsigned long)(__typeof__(*(ptr)))(new),	\
			  sizeof(*(ptr))))

#define cmpxchg(ptr, old, new)						\
({									\
	__typeof__(*(ptr)) __res;					\
									\
	smp_mb__before_llsc();						\
	__res = cmpxchg_local((ptr), (old), (new));			\
	smp_llsc_mb();							\
									\
	__res;								\
})

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

# include <asm-generic/cmpxchg-local.h>
# define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

# ifdef CONFIG_SMP

static inline unsigned long __cmpxchg64(volatile void *ptr,
					unsigned long long old,
					unsigned long long new)
{
	unsigned long long tmp, ret;
	unsigned long flags;

	/*
	 * The assembly below has to combine 32 bit values into a 64 bit
	 * register, and split 64 bit values from one register into two. If we
	 * were to take an interrupt in the middle of this we'd only save the
	 * least significant 32 bits of each register & probably clobber the
	 * most significant 32 bits of the 64 bit values we're using. In order
	 * to avoid this we must disable interrupts.
	 */
	local_irq_save(flags);

	asm volatile(
	"	.set	push				\n"
	"	.set	" MIPS_ISA_ARCH_LEVEL "		\n"
	/* Load 64 bits from ptr */
	"1:	lld	%L0, %3		# __cmpxchg64	\n"
	/*
	 * Split the 64 bit value we loaded into the 2 registers that hold the
	 * ret variable.
	 */
	"	dsra	%M0, %L0, 32			\n"
	"	sll	%L0, %L0, 0			\n"
	/*
	 * Compare ret against old, breaking out of the loop if they don't
	 * match.
	 */
	"	bne	%M0, %M4, 2f			\n"
	"	bne	%L0, %L4, 2f			\n"
	/*
	 * Combine the 32 bit halves from the 2 registers that hold the new
	 * variable into a single 64 bit register.
	 */
#  if MIPS_ISA_REV >= 2
	"	move	%L1, %L5			\n"
	"	dins	%L1, %M5, 32, 32		\n"
#  else
	"	dsll	%L1, %L5, 32			\n"
	"	dsrl	%L1, %L1, 32			\n"
	"	.set	noat				\n"
	"	dsll	$at, %M5, 32			\n"
	"	or	%L1, %L1, $at			\n"
	"	.set	at				\n"
#  endif
	/* Attempt to store new at ptr */
	"	scd	%L1, %2				\n"
	/* If we failed, loop! */
	"\t" __scbeqz "	%L1, 1b				\n"
	"	.set	pop				\n"
	"2:						\n"
	: "=&r"(ret),
	  "=&r"(tmp),
	  "=" GCC_OFF_SMALL_ASM() (*(unsigned long long *)ptr)
	: GCC_OFF_SMALL_ASM() (*(unsigned long long *)ptr),
	  "r" (old),
	  "r" (new)
	: "memory");

	local_irq_restore(flags);
	return ret;
}

#  define cmpxchg64(ptr, o, n) ({					\
	unsigned long long __old = (__typeof__(*(ptr)))(o);		\
	unsigned long long __new = (__typeof__(*(ptr)))(n);		\
	__typeof__(*(ptr)) __res;					\
									\
	/*								\
	 * We can only use cmpxchg64 if we know that the CPU supports	\
	 * 64-bits, ie. lld & scd. Our call to __cmpxchg64_unsupported	\
	 * will cause a build error unless cpu_has_64bits is a		\
	 * compile-time constant 1.					\
	 */								\
	if (cpu_has_64bits && kernel_uses_llsc)				\
		__res = __cmpxchg64((ptr), __old, __new);		\
	else								\
		__res = __cmpxchg64_unsupported();			\
									\
	__res;								\
})

# else /* !CONFIG_SMP */
#  define cmpxchg64(ptr, o, n) cmpxchg64_local((ptr), (o), (n))
# endif /* !CONFIG_SMP */
#endif /* !CONFIG_64BIT */

#undef __scbeqz

#endif /* __ASM_CMPXCHG_H */
