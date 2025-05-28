/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Regents of the University of California
 */

#ifndef _ASM_RISCV_CMPXCHG_H
#define _ASM_RISCV_CMPXCHG_H

#include <linux/bug.h>

#include <asm/alternative-macros.h>
#include <asm/fence.h>
#include <asm/hwcap.h>
#include <asm/insn-def.h>
#include <asm/cpufeature-macros.h>

#define __arch_xchg_masked(sc_sfx, swap_sfx, prepend, sc_append,		\
			   swap_append, r, p, n)				\
({										\
	if (IS_ENABLED(CONFIG_RISCV_ISA_ZABHA) &&				\
	    riscv_has_extension_unlikely(RISCV_ISA_EXT_ZABHA)) {		\
		__asm__ __volatile__ (						\
			prepend							\
			"	amoswap" swap_sfx " %0, %z2, %1\n"		\
			swap_append						\
			: "=&r" (r), "+A" (*(p))				\
			: "rJ" (n)						\
			: "memory");						\
	} else {								\
		u32 *__ptr32b = (u32 *)((ulong)(p) & ~0x3);			\
		ulong __s = ((ulong)(p) & (0x4 - sizeof(*p))) * BITS_PER_BYTE;	\
		ulong __mask = GENMASK(((sizeof(*p)) * BITS_PER_BYTE) - 1, 0)	\
				<< __s;						\
		ulong __newx = (ulong)(n) << __s;				\
		ulong __retx;							\
		ulong __rc;							\
										\
		__asm__ __volatile__ (						\
		       prepend							\
		       "0:	lr.w %0, %2\n"					\
		       "	and  %1, %0, %z4\n"				\
		       "	or   %1, %1, %z3\n"				\
		       "	sc.w" sc_sfx " %1, %1, %2\n"			\
		       "	bnez %1, 0b\n"					\
		       sc_append						\
		       : "=&r" (__retx), "=&r" (__rc), "+A" (*(__ptr32b))	\
		       : "rJ" (__newx), "rJ" (~__mask)				\
		       : "memory");						\
										\
		r = (__typeof__(*(p)))((__retx & __mask) >> __s);		\
	}									\
})

#define __arch_xchg(sfx, prepend, append, r, p, n)			\
({									\
	__asm__ __volatile__ (						\
		prepend							\
		"	amoswap" sfx " %0, %2, %1\n"			\
		append							\
		: "=r" (r), "+A" (*(p))					\
		: "r" (n)						\
		: "memory");						\
})

#define _arch_xchg(ptr, new, sc_sfx, swap_sfx, prepend,			\
		   sc_append, swap_append)				\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	__typeof__(*(__ptr)) __new = (new);				\
	__typeof__(*(__ptr)) __ret;					\
									\
	switch (sizeof(*__ptr)) {					\
	case 1:								\
		__arch_xchg_masked(sc_sfx, ".b" swap_sfx,		\
				   prepend, sc_append, swap_append,	\
				   __ret, __ptr, __new);		\
		break;							\
	case 2:								\
		__arch_xchg_masked(sc_sfx, ".h" swap_sfx,		\
				   prepend, sc_append, swap_append,	\
				   __ret, __ptr, __new);		\
		break;							\
	case 4:								\
		__arch_xchg(".w" swap_sfx, prepend, swap_append,	\
			      __ret, __ptr, __new);			\
		break;							\
	case 8:								\
		__arch_xchg(".d" swap_sfx, prepend, swap_append,	\
			      __ret, __ptr, __new);			\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
	(__typeof__(*(__ptr)))__ret;					\
})

#define arch_xchg_relaxed(ptr, x)					\
	_arch_xchg(ptr, x, "", "", "", "", "")

#define arch_xchg_acquire(ptr, x)					\
	_arch_xchg(ptr, x, "", "", "",					\
		   RISCV_ACQUIRE_BARRIER, RISCV_ACQUIRE_BARRIER)

#define arch_xchg_release(ptr, x)					\
	_arch_xchg(ptr, x, "", "", RISCV_RELEASE_BARRIER, "", "")

#define arch_xchg(ptr, x)						\
	_arch_xchg(ptr, x, ".rl", ".aqrl", "", RISCV_FULL_BARRIER, "")

#define xchg32(ptr, x)							\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4);				\
	arch_xchg((ptr), (x));						\
})

#define xchg64(ptr, x)							\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_xchg((ptr), (x));						\
})

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define __arch_cmpxchg_masked(sc_sfx, cas_sfx,					\
			      sc_prepend, sc_append,				\
			      cas_prepend, cas_append,				\
			      r, p, o, n)					\
({										\
	if (IS_ENABLED(CONFIG_RISCV_ISA_ZABHA) &&				\
	    IS_ENABLED(CONFIG_RISCV_ISA_ZACAS) &&				\
	    riscv_has_extension_unlikely(RISCV_ISA_EXT_ZABHA) &&		\
	    riscv_has_extension_unlikely(RISCV_ISA_EXT_ZACAS)) {		\
		r = o;								\
										\
		__asm__ __volatile__ (						\
			cas_prepend							\
			"	amocas" cas_sfx " %0, %z2, %1\n"		\
			cas_append							\
			: "+&r" (r), "+A" (*(p))				\
			: "rJ" (n)						\
			: "memory");						\
	} else {								\
		u32 *__ptr32b = (u32 *)((ulong)(p) & ~0x3);			\
		ulong __s = ((ulong)(p) & (0x4 - sizeof(*p))) * BITS_PER_BYTE;	\
		ulong __mask = GENMASK(((sizeof(*p)) * BITS_PER_BYTE) - 1, 0)	\
			       << __s;						\
		ulong __newx = (ulong)(n) << __s;				\
		ulong __oldx = (ulong)(o) << __s;				\
		ulong __retx;							\
		ulong __rc;							\
										\
		__asm__ __volatile__ (						\
			sc_prepend							\
			"0:	lr.w %0, %2\n"					\
			"	and  %1, %0, %z5\n"				\
			"	bne  %1, %z3, 1f\n"				\
			"	and  %1, %0, %z6\n"				\
			"	or   %1, %1, %z4\n"				\
			"	sc.w" sc_sfx " %1, %1, %2\n"			\
			"	bnez %1, 0b\n"					\
			sc_append							\
			"1:\n"							\
			: "=&r" (__retx), "=&r" (__rc), "+A" (*(__ptr32b))	\
			: "rJ" ((long)__oldx), "rJ" (__newx),			\
			  "rJ" (__mask), "rJ" (~__mask)				\
			: "memory");						\
										\
		r = (__typeof__(*(p)))((__retx & __mask) >> __s);		\
	}									\
})

#define __arch_cmpxchg(lr_sfx, sc_sfx, cas_sfx,				\
		       sc_prepend, sc_append,				\
		       cas_prepend, cas_append,				\
		       r, p, co, o, n)					\
({									\
	if (IS_ENABLED(CONFIG_RISCV_ISA_ZACAS) &&			\
	    riscv_has_extension_unlikely(RISCV_ISA_EXT_ZACAS)) {	\
		r = o;							\
									\
		__asm__ __volatile__ (					\
			cas_prepend					\
			"	amocas" cas_sfx " %0, %z2, %1\n"	\
			cas_append					\
			: "+&r" (r), "+A" (*(p))			\
			: "rJ" (n)					\
			: "memory");					\
	} else {							\
		register unsigned int __rc;				\
									\
		__asm__ __volatile__ (					\
			sc_prepend					\
			"0:	lr" lr_sfx " %0, %2\n"			\
			"	bne  %0, %z3, 1f\n"			\
			"	sc" sc_sfx " %1, %z4, %2\n"		\
			"	bnez %1, 0b\n"				\
			sc_append					\
			"1:\n"						\
			: "=&r" (r), "=&r" (__rc), "+A" (*(p))		\
			: "rJ" (co o), "rJ" (n)				\
			: "memory");					\
	}								\
})

#define _arch_cmpxchg(ptr, old, new, sc_sfx, cas_sfx,			\
		      sc_prepend, sc_append,				\
		      cas_prepend, cas_append)				\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	__typeof__(*(__ptr)) __old = (old);				\
	__typeof__(*(__ptr)) __new = (new);				\
	__typeof__(*(__ptr)) __ret;					\
									\
	switch (sizeof(*__ptr)) {					\
	case 1:								\
		__arch_cmpxchg_masked(sc_sfx, ".b" cas_sfx,		\
				      sc_prepend, sc_append,		\
				      cas_prepend, cas_append,		\
				      __ret, __ptr, __old, __new);	\
		break;							\
	case 2:								\
		__arch_cmpxchg_masked(sc_sfx, ".h" cas_sfx,		\
				      sc_prepend, sc_append,		\
				      cas_prepend, cas_append,		\
				      __ret, __ptr, __old, __new);	\
		break;							\
	case 4:								\
		__arch_cmpxchg(".w", ".w" sc_sfx, ".w" cas_sfx,		\
			       sc_prepend, sc_append,			\
			       cas_prepend, cas_append,			\
			       __ret, __ptr, (long)(int)(long), __old, __new);	\
		break;							\
	case 8:								\
		__arch_cmpxchg(".d", ".d" sc_sfx, ".d" cas_sfx,		\
			       sc_prepend, sc_append,			\
			       cas_prepend, cas_append,			\
			       __ret, __ptr, /**/, __old, __new);	\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
	(__typeof__(*(__ptr)))__ret;					\
})

/*
 * These macros are here to improve the readability of the arch_cmpxchg_XXX()
 * macros.
 */
#define SC_SFX(x)	x
#define CAS_SFX(x)	x
#define SC_PREPEND(x)	x
#define SC_APPEND(x)	x
#define CAS_PREPEND(x)	x
#define CAS_APPEND(x)	x

#define arch_cmpxchg_relaxed(ptr, o, n)					\
	_arch_cmpxchg((ptr), (o), (n),					\
		      SC_SFX(""), CAS_SFX(""),				\
		      SC_PREPEND(""), SC_APPEND(""),			\
		      CAS_PREPEND(""), CAS_APPEND(""))

#define arch_cmpxchg_acquire(ptr, o, n)					\
	_arch_cmpxchg((ptr), (o), (n),					\
		      SC_SFX(""), CAS_SFX(""),				\
		      SC_PREPEND(""), SC_APPEND(RISCV_ACQUIRE_BARRIER),	\
		      CAS_PREPEND(""), CAS_APPEND(RISCV_ACQUIRE_BARRIER))

#define arch_cmpxchg_release(ptr, o, n)					\
	_arch_cmpxchg((ptr), (o), (n),					\
		      SC_SFX(""), CAS_SFX(""),				\
		      SC_PREPEND(RISCV_RELEASE_BARRIER), SC_APPEND(""),	\
		      CAS_PREPEND(RISCV_RELEASE_BARRIER), CAS_APPEND(""))

#define arch_cmpxchg(ptr, o, n)						\
	_arch_cmpxchg((ptr), (o), (n),					\
		      SC_SFX(".rl"), CAS_SFX(".aqrl"),			\
		      SC_PREPEND(""), SC_APPEND(RISCV_FULL_BARRIER),	\
		      CAS_PREPEND(""), CAS_APPEND(""))

#define arch_cmpxchg_local(ptr, o, n)					\
	arch_cmpxchg_relaxed((ptr), (o), (n))

#define arch_cmpxchg64(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg((ptr), (o), (n));					\
})

#define arch_cmpxchg64_local(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_relaxed((ptr), (o), (n));				\
})

#define arch_cmpxchg64_relaxed(ptr, o, n)				\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_relaxed((ptr), (o), (n));				\
})

#define arch_cmpxchg64_acquire(ptr, o, n)				\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_acquire((ptr), (o), (n));				\
})

#define arch_cmpxchg64_release(ptr, o, n)				\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_release((ptr), (o), (n));				\
})

#if defined(CONFIG_64BIT) && defined(CONFIG_RISCV_ISA_ZACAS)

#define system_has_cmpxchg128()        riscv_has_extension_unlikely(RISCV_ISA_EXT_ZACAS)

union __u128_halves {
	u128 full;
	struct {
		u64 low, high;
	};
};

#define __arch_cmpxchg128(p, o, n, cas_sfx)					\
({										\
	__typeof__(*(p)) __o = (o);                                             \
	union __u128_halves __hn = { .full = (n) };				\
	union __u128_halves __ho = { .full = (__o) };				\
	register unsigned long t1 asm ("t1") = __hn.low;			\
	register unsigned long t2 asm ("t2") = __hn.high;			\
	register unsigned long t3 asm ("t3") = __ho.low;			\
	register unsigned long t4 asm ("t4") = __ho.high;			\
										\
	__asm__ __volatile__ (							\
		 "       amocas.q" cas_sfx " %0, %z3, %2"			\
		 : "+&r" (t3), "+&r" (t4), "+A" (*(p))				\
		 : "rJ" (t1), "rJ" (t2)						\
		 : "memory");							\
										\
		 ((u128)t4 << 64) | t3;						\
})

#define arch_cmpxchg128(ptr, o, n)						\
	__arch_cmpxchg128((ptr), (o), (n), ".aqrl")

#define arch_cmpxchg128_local(ptr, o, n)					\
	__arch_cmpxchg128((ptr), (o), (n), "")

#endif /* CONFIG_64BIT && CONFIG_RISCV_ISA_ZACAS */

#ifdef CONFIG_RISCV_ISA_ZAWRS
/*
 * Despite wrs.nto being "WRS-with-no-timeout", in the absence of changes to
 * @val we expect it to still terminate within a "reasonable" amount of time
 * for an implementation-specific other reason, a pending, locally-enabled
 * interrupt, or because it has been configured to raise an illegal
 * instruction exception.
 */
static __always_inline void __cmpwait(volatile void *ptr,
				      unsigned long val,
				      int size)
{
	unsigned long tmp;

	u32 *__ptr32b;
	ulong __s, __val, __mask;

	asm goto(ALTERNATIVE("j %l[no_zawrs]", "nop",
			     0, RISCV_ISA_EXT_ZAWRS, 1)
		 : : : : no_zawrs);

	switch (size) {
	case 1:
		__ptr32b = (u32 *)((ulong)(ptr) & ~0x3);
		__s = ((ulong)(ptr) & 0x3) * BITS_PER_BYTE;
		__val = val << __s;
		__mask = 0xff << __s;

		asm volatile(
		"	lr.w	%0, %1\n"
		"	and	%0, %0, %3\n"
		"	xor	%0, %0, %2\n"
		"	bnez	%0, 1f\n"
			ZAWRS_WRS_NTO "\n"
		"1:"
		: "=&r" (tmp), "+A" (*(__ptr32b))
		: "r" (__val), "r" (__mask)
		: "memory");
		break;
	case 2:
		__ptr32b = (u32 *)((ulong)(ptr) & ~0x3);
		__s = ((ulong)(ptr) & 0x2) * BITS_PER_BYTE;
		__val = val << __s;
		__mask = 0xffff << __s;

		asm volatile(
		"	lr.w	%0, %1\n"
		"	and	%0, %0, %3\n"
		"	xor	%0, %0, %2\n"
		"	bnez	%0, 1f\n"
			ZAWRS_WRS_NTO "\n"
		"1:"
		: "=&r" (tmp), "+A" (*(__ptr32b))
		: "r" (__val), "r" (__mask)
		: "memory");
		break;
	case 4:
		asm volatile(
		"	lr.w	%0, %1\n"
		"	xor	%0, %0, %2\n"
		"	bnez	%0, 1f\n"
			ZAWRS_WRS_NTO "\n"
		"1:"
		: "=&r" (tmp), "+A" (*(u32 *)ptr)
		: "r" (val));
		break;
#if __riscv_xlen == 64
	case 8:
		asm volatile(
		"	lr.d	%0, %1\n"
		"	xor	%0, %0, %2\n"
		"	bnez	%0, 1f\n"
			ZAWRS_WRS_NTO "\n"
		"1:"
		: "=&r" (tmp), "+A" (*(u64 *)ptr)
		: "r" (val));
		break;
#endif
	default:
		BUILD_BUG();
	}

	return;

no_zawrs:
	asm volatile(RISCV_PAUSE : : : "memory");
}

#define __cmpwait_relaxed(ptr, val) \
	__cmpwait((ptr), (unsigned long)(val), sizeof(*(ptr)))
#endif

#endif /* _ASM_RISCV_CMPXCHG_H */
