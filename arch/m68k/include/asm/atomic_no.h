#ifndef __ARCH_M68KNOMMU_ATOMIC__
#define __ARCH_M68KNOMMU_ATOMIC__

#include <linux/types.h>
#include <asm/system.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * We do not have SMP m68k systems, so we don't have to deal with that.
 */

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		(*(volatile int *)&(v)->counter)
#define atomic_set(v, i)	(((v)->counter) = i)

static __inline__ void atomic_add(int i, atomic_t *v)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__("addl %1,%0" : "+m" (*v) : "d" (i));
#else
	__asm__ __volatile__("addl %1,%0" : "+m" (*v) : "di" (i));
#endif
}

static __inline__ void atomic_sub(int i, atomic_t *v)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__("subl %1,%0" : "+m" (*v) : "d" (i));
#else
	__asm__ __volatile__("subl %1,%0" : "+m" (*v) : "di" (i));
#endif
}

static __inline__ int atomic_sub_and_test(int i, atomic_t * v)
{
	char c;
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__("subl %2,%1; seq %0"
			     : "=d" (c), "+m" (*v)
			     : "d" (i));
#else
	__asm__ __volatile__("subl %2,%1; seq %0"
			     : "=d" (c), "+m" (*v)
			     : "di" (i));
#endif
	return c != 0;
}

static __inline__ void atomic_inc(volatile atomic_t *v)
{
	__asm__ __volatile__("addql #1,%0" : "+m" (*v));
}

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */

static __inline__ int atomic_inc_and_test(volatile atomic_t *v)
{
	char c;
	__asm__ __volatile__("addql #1,%1; seq %0" : "=d" (c), "+m" (*v));
	return c != 0;
}

static __inline__ void atomic_dec(volatile atomic_t *v)
{
	__asm__ __volatile__("subql #1,%0" : "+m" (*v));
}

static __inline__ int atomic_dec_and_test(volatile atomic_t *v)
{
	char c;
	__asm__ __volatile__("subql #1,%1; seq %0" : "=d" (c), "+m" (*v));
	return c != 0;
}

static __inline__ void atomic_clear_mask(unsigned long mask, unsigned long *v)
{
	__asm__ __volatile__("andl %1,%0" : "+m" (*v) : "id" (~(mask)));
}

static __inline__ void atomic_set_mask(unsigned long mask, unsigned long *v)
{
	__asm__ __volatile__("orl %1,%0" : "+m" (*v) : "id" (mask));
}

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()    barrier()
#define smp_mb__after_atomic_dec() barrier()
#define smp_mb__before_atomic_inc()    barrier()
#define smp_mb__after_atomic_inc() barrier()

static inline int atomic_add_return(int i, atomic_t * v)
{
	unsigned long temp, flags;

	local_irq_save(flags);
	temp = *(long *)v;
	temp += i;
	*(long *)v = temp;
	local_irq_restore(flags);

	return temp;
}

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

static inline int atomic_sub_return(int i, atomic_t * v)
{
	unsigned long temp, flags;

	local_irq_save(flags);
	temp = *(long *)v;
	temp -= i;
	*(long *)v = temp;
	local_irq_restore(flags);

	return temp;
}

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static __inline__ int atomic_add_unless(atomic_t *v, int a, int u)
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
	return c != (u);
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#include <asm-generic/atomic-long.h>
#endif /* __ARCH_M68KNOMMU_ATOMIC __ */
