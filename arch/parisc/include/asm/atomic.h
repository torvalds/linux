/* Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 * Copyright (C) 2006 Kyle McMartin <kyle@parisc-linux.org>
 */

#ifndef _ASM_PARISC_ATOMIC_H_
#define _ASM_PARISC_ATOMIC_H_

#include <linux/types.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * And probably incredibly slow on parisc.  OTOH, we don't
 * have to write any serious assembly.   prumpf
 */

#ifdef CONFIG_SMP
#include <asm/spinlock.h>
#include <asm/cache.h>		/* we use L1_CACHE_BYTES */

/* Use an array of spinlocks for our atomic_ts.
 * Hash function to index into a different SPINLOCK.
 * Since "a" is usually an address, use one spinlock per cacheline.
 */
#  define ATOMIC_HASH_SIZE 4
#  define ATOMIC_HASH(a) (&(__atomic_hash[ (((unsigned long) (a))/L1_CACHE_BYTES) & (ATOMIC_HASH_SIZE-1) ]))

extern arch_spinlock_t __atomic_hash[ATOMIC_HASH_SIZE] __lock_aligned;

/* Can't use raw_spin_lock_irq because of #include problems, so
 * this is the substitute */
#define _atomic_spin_lock_irqsave(l,f) do {	\
	arch_spinlock_t *s = ATOMIC_HASH(l);		\
	local_irq_save(f);			\
	arch_spin_lock(s);			\
} while(0)

#define _atomic_spin_unlock_irqrestore(l,f) do {	\
	arch_spinlock_t *s = ATOMIC_HASH(l);			\
	arch_spin_unlock(s);				\
	local_irq_restore(f);				\
} while(0)


#else
#  define _atomic_spin_lock_irqsave(l,f) do { local_irq_save(f); } while (0)
#  define _atomic_spin_unlock_irqrestore(l,f) do { local_irq_restore(f); } while (0)
#endif

/* This should get optimized out since it's never called.
** Or get a link error if xchg is used "wrong".
*/
extern void __xchg_called_with_bad_pointer(void);


/* __xchg32/64 defined in arch/parisc/lib/bitops.c */
extern unsigned long __xchg8(char, char *);
extern unsigned long __xchg32(int, int *);
#ifdef CONFIG_64BIT
extern unsigned long __xchg64(unsigned long, unsigned long *);
#endif

/* optimizer better get rid of switch since size is a constant */
static __inline__ unsigned long
__xchg(unsigned long x, __volatile__ void * ptr, int size)
{
	switch(size) {
#ifdef CONFIG_64BIT
	case 8: return __xchg64(x,(unsigned long *) ptr);
#endif
	case 4: return __xchg32((int) x, (int *) ptr);
	case 1: return __xchg8((char) x, (char *) ptr);
	}
	__xchg_called_with_bad_pointer();
	return x;
}


/*
** REVISIT - Abandoned use of LDCW in xchg() for now:
** o need to test sizeof(*ptr) to avoid clearing adjacent bytes
** o and while we are at it, could CONFIG_64BIT code use LDCD too?
**
**	if (__builtin_constant_p(x) && (x == NULL))
**		if (((unsigned long)p & 0xf) == 0)
**			return __ldcw(p);
*/
#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))


#define __HAVE_ARCH_CMPXCHG	1

/* bug catcher for when unsupported size is used - won't link */
extern void __cmpxchg_called_with_bad_pointer(void);

/* __cmpxchg_u32/u64 defined in arch/parisc/lib/bitops.c */
extern unsigned long __cmpxchg_u32(volatile unsigned int *m, unsigned int old, unsigned int new_);
extern unsigned long __cmpxchg_u64(volatile unsigned long *ptr, unsigned long old, unsigned long new_);

/* don't worry...optimizer will get rid of most of this */
static __inline__ unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new_, int size)
{
	switch(size) {
#ifdef CONFIG_64BIT
	case 8: return __cmpxchg_u64((unsigned long *)ptr, old, new_);
#endif
	case 4: return __cmpxchg_u32((unsigned int *)ptr, (unsigned int) old, (unsigned int) new_);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new_, int size)
{
	switch (size) {
#ifdef CONFIG_64BIT
	case 8:	return __cmpxchg_u64((unsigned long *)ptr, old, new_);
#endif
	case 4:	return __cmpxchg_u32(ptr, old, new_);
	default:
		return __cmpxchg_local_generic(ptr, old, new_, size);
	}
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	\
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))))
#ifdef CONFIG_64BIT
#define cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
  })
#else
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))
#endif

/*
 * Note that we need not lock read accesses - aligned word writes/reads
 * are atomic, so a reader never sees inconsistent values.
 */

/* It's possible to reduce all atomic operations to either
 * __atomic_add_return, atomic_set and atomic_read (the latter
 * is there only for consistency).
 */

static __inline__ int __atomic_add_return(int i, atomic_t *v)
{
	int ret;
	unsigned long flags;
	_atomic_spin_lock_irqsave(v, flags);

	ret = (v->counter += i);

	_atomic_spin_unlock_irqrestore(v, flags);
	return ret;
}

static __inline__ void atomic_set(atomic_t *v, int i) 
{
	unsigned long flags;
	_atomic_spin_lock_irqsave(v, flags);

	v->counter = i;

	_atomic_spin_unlock_irqrestore(v, flags);
}

static __inline__ int atomic_read(const atomic_t *v)
{
	return (*(volatile int *)&(v)->counter);
}

/* exported interface */
#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * __atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c;
}


#define atomic_add(i,v)	((void)(__atomic_add_return( (i),(v))))
#define atomic_sub(i,v)	((void)(__atomic_add_return(-(i),(v))))
#define atomic_inc(v)	((void)(__atomic_add_return(   1,(v))))
#define atomic_dec(v)	((void)(__atomic_add_return(  -1,(v))))

#define atomic_add_return(i,v)	(__atomic_add_return( (i),(v)))
#define atomic_sub_return(i,v)	(__atomic_add_return(-(i),(v)))
#define atomic_inc_return(v)	(__atomic_add_return(   1,(v)))
#define atomic_dec_return(v)	(__atomic_add_return(  -1,(v)))

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

#define atomic_dec_and_test(v)	(atomic_dec_return(v) == 0)

#define atomic_sub_and_test(i,v)	(atomic_sub_return((i),(v)) == 0)

#define ATOMIC_INIT(i)	((atomic_t) { (i) })

#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

#ifdef CONFIG_64BIT

#define ATOMIC64_INIT(i) ((atomic64_t) { (i) })

static __inline__ s64
__atomic64_add_return(s64 i, atomic64_t *v)
{
	s64 ret;
	unsigned long flags;
	_atomic_spin_lock_irqsave(v, flags);

	ret = (v->counter += i);

	_atomic_spin_unlock_irqrestore(v, flags);
	return ret;
}

static __inline__ void
atomic64_set(atomic64_t *v, s64 i)
{
	unsigned long flags;
	_atomic_spin_lock_irqsave(v, flags);

	v->counter = i;

	_atomic_spin_unlock_irqrestore(v, flags);
}

static __inline__ s64
atomic64_read(const atomic64_t *v)
{
	return (*(volatile long *)&(v)->counter);
}

#define atomic64_add(i,v)	((void)(__atomic64_add_return( ((s64)(i)),(v))))
#define atomic64_sub(i,v)	((void)(__atomic64_add_return(-((s64)(i)),(v))))
#define atomic64_inc(v)		((void)(__atomic64_add_return(   1,(v))))
#define atomic64_dec(v)		((void)(__atomic64_add_return(  -1,(v))))

#define atomic64_add_return(i,v)	(__atomic64_add_return( ((s64)(i)),(v)))
#define atomic64_sub_return(i,v)	(__atomic64_add_return(-((s64)(i)),(v)))
#define atomic64_inc_return(v)		(__atomic64_add_return(   1,(v)))
#define atomic64_dec_return(v)		(__atomic64_add_return(  -1,(v)))

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)

#define atomic64_inc_and_test(v) 	(atomic64_inc_return(v) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return(v) == 0)
#define atomic64_sub_and_test(i,v)	(atomic64_sub_return((i),(v)) == 0)

/* exported interface */
#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long c, old;
	c = atomic64_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic64_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define atomic64_inc_not_zero(v) atomic64_add_unless((v), 1, 0)

#endif /* !CONFIG_64BIT */


#endif /* _ASM_PARISC_ATOMIC_H_ */
