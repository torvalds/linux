/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_CMPXCHG_H_
#define _ASM_POWERPC_CMPXCHG_H_

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <asm/synch.h>
#include <linux/bug.h>

#ifdef __BIG_ENDIAN
#define BITOFF_CAL(size, off)	((sizeof(u32) - size - off) * BITS_PER_BYTE)
#else
#define BITOFF_CAL(size, off)	(off * BITS_PER_BYTE)
#endif

#define XCHG_GEN(type, sfx, cl)				\
static inline u32 __xchg_##type##sfx(volatile void *p, u32 val)	\
{								\
	unsigned int prev, prev_mask, tmp, bitoff, off;		\
								\
	off = (unsigned long)p % sizeof(u32);			\
	bitoff = BITOFF_CAL(sizeof(type), off);			\
	p -= off;						\
	val <<= bitoff;						\
	prev_mask = (u32)(type)-1 << bitoff;			\
								\
	__asm__ __volatile__(					\
"1:	lwarx   %0,0,%3\n"					\
"	andc	%1,%0,%5\n"					\
"	or	%1,%1,%4\n"					\
"	stwcx.	%1,0,%3\n"					\
"	bne-	1b\n"						\
	: "=&r" (prev), "=&r" (tmp), "+m" (*(u32*)p)		\
	: "r" (p), "r" (val), "r" (prev_mask)			\
	: "cc", cl);						\
								\
	return prev >> bitoff;					\
}

#define CMPXCHG_GEN(type, sfx, br, br2, cl)			\
static inline							\
u32 __cmpxchg_##type##sfx(volatile void *p, u32 old, u32 new)	\
{								\
	unsigned int prev, prev_mask, tmp, bitoff, off;		\
								\
	off = (unsigned long)p % sizeof(u32);			\
	bitoff = BITOFF_CAL(sizeof(type), off);			\
	p -= off;						\
	old <<= bitoff;						\
	new <<= bitoff;						\
	prev_mask = (u32)(type)-1 << bitoff;			\
								\
	__asm__ __volatile__(					\
	br							\
"1:	lwarx   %0,0,%3\n"					\
"	and	%1,%0,%6\n"					\
"	cmpw	0,%1,%4\n"					\
"	bne-	2f\n"						\
"	andc	%1,%0,%6\n"					\
"	or	%1,%1,%5\n"					\
"	stwcx.  %1,0,%3\n"					\
"	bne-    1b\n"						\
	br2							\
	"\n"							\
"2:"								\
	: "=&r" (prev), "=&r" (tmp), "+m" (*(u32*)p)		\
	: "r" (p), "r" (old), "r" (new), "r" (prev_mask)	\
	: "cc", cl);						\
								\
	return prev >> bitoff;					\
}

/*
 * Atomic exchange
 *
 * Changes the memory location '*p' to be val and returns
 * the previous value stored there.
 */

#ifndef CONFIG_PPC_HAS_LBARX_LHARX
XCHG_GEN(u8, _local, "memory");
XCHG_GEN(u8, _relaxed, "cc");
XCHG_GEN(u16, _local, "memory");
XCHG_GEN(u16, _relaxed, "cc");
#else
static __always_inline unsigned long
__xchg_u8_local(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lbarx	%0,0,%2		# __xchg_u8_local\n"
"	stbcx.	%3,0,%2 \n"
"	bne-	1b"
	: "=&r" (prev), "+m" (*(volatile unsigned char *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__xchg_u8_relaxed(u8 *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lbarx	%0,0,%2		# __xchg_u8_relaxed\n"
"	stbcx.	%3,0,%2\n"
"	bne-	1b"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (val)
	: "cc");

	return prev;
}

static __always_inline unsigned long
__xchg_u16_local(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lharx	%0,0,%2		# __xchg_u16_local\n"
"	sthcx.	%3,0,%2\n"
"	bne-	1b"
	: "=&r" (prev), "+m" (*(volatile unsigned short *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__xchg_u16_relaxed(u16 *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lharx	%0,0,%2		# __xchg_u16_relaxed\n"
"	sthcx.	%3,0,%2\n"
"	bne-	1b"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (val)
	: "cc");

	return prev;
}
#endif

static __always_inline unsigned long
__xchg_u32_local(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2 \n"
"	stwcx.	%3,0,%2 \n\
	bne-	1b"
	: "=&r" (prev), "+m" (*(volatile unsigned int *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__xchg_u32_relaxed(u32 *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2\n"
"	stwcx.	%3,0,%2\n"
"	bne-	1b"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (val)
	: "cc");

	return prev;
}

#ifdef CONFIG_PPC64
static __always_inline unsigned long
__xchg_u64_local(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2 \n"
"	stdcx.	%3,0,%2 \n\
	bne-	1b"
	: "=&r" (prev), "+m" (*(volatile unsigned long *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__xchg_u64_relaxed(u64 *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2\n"
"	stdcx.	%3,0,%2\n"
"	bne-	1b"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (val)
	: "cc");

	return prev;
}
#endif

static __always_inline unsigned long
__xchg_local(void *ptr, unsigned long x, unsigned int size)
{
	switch (size) {
	case 1:
		return __xchg_u8_local(ptr, x);
	case 2:
		return __xchg_u16_local(ptr, x);
	case 4:
		return __xchg_u32_local(ptr, x);
#ifdef CONFIG_PPC64
	case 8:
		return __xchg_u64_local(ptr, x);
#endif
	}
	BUILD_BUG_ON_MSG(1, "Unsupported size for __xchg");
	return x;
}

static __always_inline unsigned long
__xchg_relaxed(void *ptr, unsigned long x, unsigned int size)
{
	switch (size) {
	case 1:
		return __xchg_u8_relaxed(ptr, x);
	case 2:
		return __xchg_u16_relaxed(ptr, x);
	case 4:
		return __xchg_u32_relaxed(ptr, x);
#ifdef CONFIG_PPC64
	case 8:
		return __xchg_u64_relaxed(ptr, x);
#endif
	}
	BUILD_BUG_ON_MSG(1, "Unsupported size for __xchg_local");
	return x;
}
#define arch_xchg_local(ptr,x)						     \
  ({									     \
     __typeof__(*(ptr)) _x_ = (x);					     \
     (__typeof__(*(ptr))) __xchg_local((ptr),				     \
     		(unsigned long)_x_, sizeof(*(ptr))); 			     \
  })

#define arch_xchg_relaxed(ptr, x)					\
({									\
	__typeof__(*(ptr)) _x_ = (x);					\
	(__typeof__(*(ptr))) __xchg_relaxed((ptr),			\
			(unsigned long)_x_, sizeof(*(ptr)));		\
})

/*
 * Compare and exchange - if *p == old, set it to new,
 * and return the old value of *p.
 */
#ifndef CONFIG_PPC_HAS_LBARX_LHARX
CMPXCHG_GEN(u8, , PPC_ATOMIC_ENTRY_BARRIER, PPC_ATOMIC_EXIT_BARRIER, "memory");
CMPXCHG_GEN(u8, _local, , , "memory");
CMPXCHG_GEN(u8, _acquire, , PPC_ACQUIRE_BARRIER, "memory");
CMPXCHG_GEN(u8, _relaxed, , , "cc");
CMPXCHG_GEN(u16, , PPC_ATOMIC_ENTRY_BARRIER, PPC_ATOMIC_EXIT_BARRIER, "memory");
CMPXCHG_GEN(u16, _local, , , "memory");
CMPXCHG_GEN(u16, _acquire, , PPC_ACQUIRE_BARRIER, "memory");
CMPXCHG_GEN(u16, _relaxed, , , "cc");
#else
static __always_inline unsigned long
__cmpxchg_u8(volatile unsigned char *p, unsigned long old, unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lbarx	%0,0,%2		# __cmpxchg_u8\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	stbcx.	%4,0,%2\n"
"	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u8_local(volatile unsigned char *p, unsigned long old,
			unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
"1:	lbarx	%0,0,%2		# __cmpxchg_u8_local\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	stbcx.	%4,0,%2\n"
"	bne-	1b\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u8_relaxed(u8 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	lbarx	%0,0,%2		# __cmpxchg_u8_relaxed\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	stbcx.	%4,0,%2\n"
"	bne-	1b\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u8_acquire(u8 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	lbarx	%0,0,%2		# __cmpxchg_u8_acquire\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	stbcx.	%4,0,%2\n"
"	bne-	1b\n"
	PPC_ACQUIRE_BARRIER
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u16(volatile unsigned short *p, unsigned long old, unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lharx	%0,0,%2		# __cmpxchg_u16\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	sthcx.	%4,0,%2\n"
"	bne-	1b\n"
	PPC_ATOMIC_EXIT_BARRIER
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u16_local(volatile unsigned short *p, unsigned long old,
			unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
"1:	lharx	%0,0,%2		# __cmpxchg_u16_local\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	sthcx.	%4,0,%2\n"
"	bne-	1b"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u16_relaxed(u16 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	lharx	%0,0,%2		# __cmpxchg_u16_relaxed\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	sthcx.	%4,0,%2\n"
"	bne-	1b\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u16_acquire(u16 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	lharx	%0,0,%2		# __cmpxchg_u16_acquire\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	sthcx.	%4,0,%2\n"
"	bne-	1b\n"
	PPC_ACQUIRE_BARRIER
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}
#endif

static __always_inline unsigned long
__cmpxchg_u32(volatile unsigned int *p, unsigned long old, unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lwarx	%0,0,%2		# __cmpxchg_u32\n\
	cmpw	0,%0,%3\n\
	bne-	2f\n"
"	stwcx.	%4,0,%2\n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u32_local(volatile unsigned int *p, unsigned long old,
			unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
"1:	lwarx	%0,0,%2		# __cmpxchg_u32\n\
	cmpw	0,%0,%3\n\
	bne-	2f\n"
"	stwcx.	%4,0,%2\n\
	bne-	1b"
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u32_relaxed(u32 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	lwarx	%0,0,%2		# __cmpxchg_u32_relaxed\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	stwcx.	%4,0,%2\n"
"	bne-	1b\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc");

	return prev;
}

/*
 * cmpxchg family don't have order guarantee if cmp part fails, therefore we
 * can avoid superfluous barriers if we use assembly code to implement
 * cmpxchg() and cmpxchg_acquire(), however we don't do the similar for
 * cmpxchg_release() because that will result in putting a barrier in the
 * middle of a ll/sc loop, which is probably a bad idea. For example, this
 * might cause the conditional store more likely to fail.
 */
static __always_inline unsigned long
__cmpxchg_u32_acquire(u32 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	lwarx	%0,0,%2		# __cmpxchg_u32_acquire\n"
"	cmpw	0,%0,%3\n"
"	bne-	2f\n"
"	stwcx.	%4,0,%2\n"
"	bne-	1b\n"
	PPC_ACQUIRE_BARRIER
	"\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

#ifdef CONFIG_PPC64
static __always_inline unsigned long
__cmpxchg_u64(volatile unsigned long *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	ldarx	%0,0,%2		# __cmpxchg_u64\n\
	cmpd	0,%0,%3\n\
	bne-	2f\n\
	stdcx.	%4,0,%2\n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u64_local(volatile unsigned long *p, unsigned long old,
			unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	ldarx	%0,0,%2		# __cmpxchg_u64\n\
	cmpd	0,%0,%3\n\
	bne-	2f\n\
	stdcx.	%4,0,%2\n\
	bne-	1b"
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u64_relaxed(u64 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	ldarx	%0,0,%2		# __cmpxchg_u64_relaxed\n"
"	cmpd	0,%0,%3\n"
"	bne-	2f\n"
"	stdcx.	%4,0,%2\n"
"	bne-	1b\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u64_acquire(u64 *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	ldarx	%0,0,%2		# __cmpxchg_u64_acquire\n"
"	cmpd	0,%0,%3\n"
"	bne-	2f\n"
"	stdcx.	%4,0,%2\n"
"	bne-	1b\n"
	PPC_ACQUIRE_BARRIER
	"\n"
"2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}
#endif

static __always_inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new,
	  unsigned int size)
{
	switch (size) {
	case 1:
		return __cmpxchg_u8(ptr, old, new);
	case 2:
		return __cmpxchg_u16(ptr, old, new);
	case 4:
		return __cmpxchg_u32(ptr, old, new);
#ifdef CONFIG_PPC64
	case 8:
		return __cmpxchg_u64(ptr, old, new);
#endif
	}
	BUILD_BUG_ON_MSG(1, "Unsupported size for __cmpxchg");
	return old;
}

static __always_inline unsigned long
__cmpxchg_local(void *ptr, unsigned long old, unsigned long new,
	  unsigned int size)
{
	switch (size) {
	case 1:
		return __cmpxchg_u8_local(ptr, old, new);
	case 2:
		return __cmpxchg_u16_local(ptr, old, new);
	case 4:
		return __cmpxchg_u32_local(ptr, old, new);
#ifdef CONFIG_PPC64
	case 8:
		return __cmpxchg_u64_local(ptr, old, new);
#endif
	}
	BUILD_BUG_ON_MSG(1, "Unsupported size for __cmpxchg_local");
	return old;
}

static __always_inline unsigned long
__cmpxchg_relaxed(void *ptr, unsigned long old, unsigned long new,
		  unsigned int size)
{
	switch (size) {
	case 1:
		return __cmpxchg_u8_relaxed(ptr, old, new);
	case 2:
		return __cmpxchg_u16_relaxed(ptr, old, new);
	case 4:
		return __cmpxchg_u32_relaxed(ptr, old, new);
#ifdef CONFIG_PPC64
	case 8:
		return __cmpxchg_u64_relaxed(ptr, old, new);
#endif
	}
	BUILD_BUG_ON_MSG(1, "Unsupported size for __cmpxchg_relaxed");
	return old;
}

static __always_inline unsigned long
__cmpxchg_acquire(void *ptr, unsigned long old, unsigned long new,
		  unsigned int size)
{
	switch (size) {
	case 1:
		return __cmpxchg_u8_acquire(ptr, old, new);
	case 2:
		return __cmpxchg_u16_acquire(ptr, old, new);
	case 4:
		return __cmpxchg_u32_acquire(ptr, old, new);
#ifdef CONFIG_PPC64
	case 8:
		return __cmpxchg_u64_acquire(ptr, old, new);
#endif
	}
	BUILD_BUG_ON_MSG(1, "Unsupported size for __cmpxchg_acquire");
	return old;
}
#define arch_cmpxchg(ptr, o, n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })


#define arch_cmpxchg_local(ptr, o, n)					 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg_local((ptr), (unsigned long)_o_,	 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#define arch_cmpxchg_relaxed(ptr, o, n)					\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) __cmpxchg_relaxed((ptr),			\
			(unsigned long)_o_, (unsigned long)_n_,		\
			sizeof(*(ptr)));				\
})

#define arch_cmpxchg_acquire(ptr, o, n)					\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) __cmpxchg_acquire((ptr),			\
			(unsigned long)_o_, (unsigned long)_n_,		\
			sizeof(*(ptr)));				\
})
#ifdef CONFIG_PPC64
#define arch_cmpxchg64(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg((ptr), (o), (n));					\
  })
#define arch_cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_local((ptr), (o), (n));				\
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
#else
#include <asm-generic/cmpxchg-local.h>
#define arch_cmpxchg64_local(ptr, o, n) __generic_cmpxchg64_local((ptr), (o), (n))
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_CMPXCHG_H_ */
