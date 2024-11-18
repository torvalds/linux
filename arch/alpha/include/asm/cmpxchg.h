/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_CMPXCHG_H
#define _ALPHA_CMPXCHG_H

/*
 * Atomic exchange.
 * Since it can be used to implement critical sections
 * it must clobber "memory" (also for interrupts in UP).
 */

static inline unsigned long
____xchg_u8(volatile char *m, unsigned long val)
{
	unsigned long ret, tmp, addr64;

	__asm__ __volatile__(
	"	andnot	%4,7,%3\n"
	"	insbl	%1,%4,%1\n"
	"1:	ldq_l	%2,0(%3)\n"
	"	extbl	%2,%4,%0\n"
	"	mskbl	%2,%4,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%3)\n"
	"	beq	%2,2f\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (ret), "=&r" (val), "=&r" (tmp), "=&r" (addr64)
	: "r" ((long)m), "1" (val) : "memory");

	return ret;
}

static inline unsigned long
____xchg_u16(volatile short *m, unsigned long val)
{
	unsigned long ret, tmp, addr64;

	__asm__ __volatile__(
	"	andnot	%4,7,%3\n"
	"	inswl	%1,%4,%1\n"
	"1:	ldq_l	%2,0(%3)\n"
	"	extwl	%2,%4,%0\n"
	"	mskwl	%2,%4,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%3)\n"
	"	beq	%2,2f\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (ret), "=&r" (val), "=&r" (tmp), "=&r" (addr64)
	: "r" ((long)m), "1" (val) : "memory");

	return ret;
}

static inline unsigned long
____xchg_u32(volatile int *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldl_l %0,%4\n"
	"	bis $31,%3,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m) : "memory");

	return val;
}

static inline unsigned long
____xchg_u64(volatile long *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldq_l %0,%4\n"
	"	bis $31,%3,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m) : "memory");

	return val;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid xchg().  */
extern void __xchg_called_with_bad_pointer(void);

static __always_inline unsigned long
____xchg(volatile void *ptr, unsigned long x, int size)
{
	return
		size == 1 ? ____xchg_u8(ptr, x) :
		size == 2 ? ____xchg_u16(ptr, x) :
		size == 4 ? ____xchg_u32(ptr, x) :
		size == 8 ? ____xchg_u64(ptr, x) :
			(__xchg_called_with_bad_pointer(), x);
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

static inline unsigned long
____cmpxchg_u8(volatile char *m, unsigned char old, unsigned char new)
{
	unsigned long prev, tmp, cmp, addr64;

	__asm__ __volatile__(
	"	andnot	%5,7,%4\n"
	"	insbl	%1,%5,%1\n"
	"1:	ldq_l	%2,0(%4)\n"
	"	extbl	%2,%5,%0\n"
	"	cmpeq	%0,%6,%3\n"
	"	beq	%3,2f\n"
	"	mskbl	%2,%5,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%4)\n"
	"	beq	%2,3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: "=&r" (prev), "=&r" (new), "=&r" (tmp), "=&r" (cmp), "=&r" (addr64)
	: "r" ((long)m), "Ir" (old), "1" (new) : "memory");

	return prev;
}

static inline unsigned long
____cmpxchg_u16(volatile short *m, unsigned short old, unsigned short new)
{
	unsigned long prev, tmp, cmp, addr64;

	__asm__ __volatile__(
	"	andnot	%5,7,%4\n"
	"	inswl	%1,%5,%1\n"
	"1:	ldq_l	%2,0(%4)\n"
	"	extwl	%2,%5,%0\n"
	"	cmpeq	%0,%6,%3\n"
	"	beq	%3,2f\n"
	"	mskwl	%2,%5,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%4)\n"
	"	beq	%2,3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: "=&r" (prev), "=&r" (new), "=&r" (tmp), "=&r" (cmp), "=&r" (addr64)
	: "r" ((long)m), "Ir" (old), "1" (new) : "memory");

	return prev;
}

static inline unsigned long
____cmpxchg_u32(volatile int *m, int old, int new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldl_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m) : "memory");

	return prev;
}

static inline unsigned long
____cmpxchg_u64(volatile long *m, unsigned long old, unsigned long new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldq_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m) : "memory");

	return prev;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static __always_inline unsigned long
____cmpxchg(volatile void *ptr, unsigned long old, unsigned long new,
	      int size)
{
	return
		size == 1 ? ____cmpxchg_u8(ptr, old, new) :
		size == 2 ? ____cmpxchg_u16(ptr, old, new) :
		size == 4 ? ____cmpxchg_u32(ptr, old, new) :
		size == 8 ? ____cmpxchg_u64(ptr, old, new) :
			(__cmpxchg_called_with_bad_pointer(), old);
}

#define xchg_local(ptr, x)						\
({									\
	__typeof__(*(ptr)) _x_ = (x);					\
	(__typeof__(*(ptr))) ____xchg((ptr), (unsigned long)_x_,	\
					       sizeof(*(ptr)));		\
})

#define arch_cmpxchg_local(ptr, o, n)					\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) ____cmpxchg((ptr), (unsigned long)_o_,	\
					  (unsigned long)_n_,		\
					  sizeof(*(ptr)));		\
})

#define arch_cmpxchg64_local(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
})

/*
 * The leading and the trailing memory barriers guarantee that these
 * operations are fully ordered.
 */
#define arch_xchg(ptr, x)						\
({									\
	__typeof__(*(ptr)) __ret;					\
	__typeof__(*(ptr)) _x_ = (x);					\
	smp_mb();							\
	__ret = (__typeof__(*(ptr)))					\
		____xchg((ptr), (unsigned long)_x_, sizeof(*(ptr)));	\
	smp_mb();							\
	__ret;								\
})

#define arch_cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) __ret;					\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	smp_mb();							\
	__ret = (__typeof__(*(ptr))) ____cmpxchg((ptr),			\
		(unsigned long)_o_, (unsigned long)_n_, sizeof(*(ptr)));\
	smp_mb();							\
	__ret;								\
})

#define arch_cmpxchg64(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg((ptr), (o), (n));					\
})

#endif /* _ALPHA_CMPXCHG_H */
