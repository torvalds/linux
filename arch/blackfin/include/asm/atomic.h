#ifndef __ARCH_BLACKFIN_ATOMIC__
#define __ARCH_BLACKFIN_ATOMIC__

#include <asm/system.h>	/* local_irq_XXX() */

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * Generally we do not concern about SMP BFIN systems, so we don't have
 * to deal with that.
 *
 * Tony Kou (tonyko@lineo.ca)   Lineo Inc.   2001
 */

typedef struct {
	int counter;
} atomic_t;
#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = i)

static __inline__ void atomic_add(int i, atomic_t * v)
{
	long flags;

	local_irq_save(flags);
	v->counter += i;
	local_irq_restore(flags);
}

static __inline__ void atomic_sub(int i, atomic_t * v)
{
	long flags;

	local_irq_save(flags);
	v->counter -= i;
	local_irq_restore(flags);

}

static inline int atomic_add_return(int i, atomic_t * v)
{
	int __temp = 0;
	long flags;

	local_irq_save(flags);
	v->counter += i;
	__temp = v->counter;
	local_irq_restore(flags);


	return __temp;
}

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)
static inline int atomic_sub_return(int i, atomic_t * v)
{
	int __temp = 0;
	long flags;

	local_irq_save(flags);
	v->counter -= i;
	__temp = v->counter;
	local_irq_restore(flags);

	return __temp;
}

static __inline__ void atomic_inc(volatile atomic_t * v)
{
	long flags;

	local_irq_save(flags);
	v->counter++;
	local_irq_restore(flags);
}

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#define atomic_add_unless(v, a, u)				\
({								\
	int c, old;						\
	c = atomic_read(v);					\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c) \
		c = old;					\
	c != (u);						\
})
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

static __inline__ void atomic_dec(volatile atomic_t * v)
{
	long flags;

	local_irq_save(flags);
	v->counter--;
	local_irq_restore(flags);
}

static __inline__ void atomic_clear_mask(unsigned int mask, atomic_t * v)
{
	long flags;

	local_irq_save(flags);
	v->counter &= ~mask;
	local_irq_restore(flags);
}

static __inline__ void atomic_set_mask(unsigned int mask, atomic_t * v)
{
	long flags;

	local_irq_save(flags);
	v->counter |= mask;
	local_irq_restore(flags);
}

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()    barrier()
#define smp_mb__after_atomic_dec() barrier()
#define smp_mb__before_atomic_inc()    barrier()
#define smp_mb__after_atomic_inc() barrier()

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#include <asm-generic/atomic.h>

#endif				/* __ARCH_BLACKFIN_ATOMIC __ */
