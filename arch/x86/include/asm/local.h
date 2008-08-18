#ifndef ASM_X86__LOCAL_H
#define ASM_X86__LOCAL_H

#include <linux/percpu.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/asm.h>

typedef struct {
	atomic_long_t a;
} local_t;

#define LOCAL_INIT(i)	{ ATOMIC_LONG_INIT(i) }

#define local_read(l)	atomic_long_read(&(l)->a)
#define local_set(l, i)	atomic_long_set(&(l)->a, (i))

static inline void local_inc(local_t *l)
{
	asm volatile(_ASM_INC "%0"
		     : "+m" (l->a.counter));
}

static inline void local_dec(local_t *l)
{
	asm volatile(_ASM_DEC "%0"
		     : "+m" (l->a.counter));
}

static inline void local_add(long i, local_t *l)
{
	asm volatile(_ASM_ADD "%1,%0"
		     : "+m" (l->a.counter)
		     : "ir" (i));
}

static inline void local_sub(long i, local_t *l)
{
	asm volatile(_ASM_SUB "%1,%0"
		     : "+m" (l->a.counter)
		     : "ir" (i));
}

/**
 * local_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @l: pointer to type local_t
 *
 * Atomically subtracts @i from @l and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static inline int local_sub_and_test(long i, local_t *l)
{
	unsigned char c;

	asm volatile(_ASM_SUB "%2,%0; sete %1"
		     : "+m" (l->a.counter), "=qm" (c)
		     : "ir" (i) : "memory");
	return c;
}

/**
 * local_dec_and_test - decrement and test
 * @l: pointer to type local_t
 *
 * Atomically decrements @l by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline int local_dec_and_test(local_t *l)
{
	unsigned char c;

	asm volatile(_ASM_DEC "%0; sete %1"
		     : "+m" (l->a.counter), "=qm" (c)
		     : : "memory");
	return c != 0;
}

/**
 * local_inc_and_test - increment and test
 * @l: pointer to type local_t
 *
 * Atomically increments @l by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
static inline int local_inc_and_test(local_t *l)
{
	unsigned char c;

	asm volatile(_ASM_INC "%0; sete %1"
		     : "+m" (l->a.counter), "=qm" (c)
		     : : "memory");
	return c != 0;
}

/**
 * local_add_negative - add and test if negative
 * @i: integer value to add
 * @l: pointer to type local_t
 *
 * Atomically adds @i to @l and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
static inline int local_add_negative(long i, local_t *l)
{
	unsigned char c;

	asm volatile(_ASM_ADD "%2,%0; sets %1"
		     : "+m" (l->a.counter), "=qm" (c)
		     : "ir" (i) : "memory");
	return c;
}

/**
 * local_add_return - add and return
 * @i: integer value to add
 * @l: pointer to type local_t
 *
 * Atomically adds @i to @l and returns @i + @l
 */
static inline long local_add_return(long i, local_t *l)
{
	long __i;
#ifdef CONFIG_M386
	unsigned long flags;
	if (unlikely(boot_cpu_data.x86 <= 3))
		goto no_xadd;
#endif
	/* Modern 486+ processor */
	__i = i;
	asm volatile(_ASM_XADD "%0, %1;"
		     : "+r" (i), "+m" (l->a.counter)
		     : : "memory");
	return i + __i;

#ifdef CONFIG_M386
no_xadd: /* Legacy 386 processor */
	local_irq_save(flags);
	__i = local_read(l);
	local_set(l, i + __i);
	local_irq_restore(flags);
	return i + __i;
#endif
}

static inline long local_sub_return(long i, local_t *l)
{
	return local_add_return(-i, l);
}

#define local_inc_return(l)  (local_add_return(1, l))
#define local_dec_return(l)  (local_sub_return(1, l))

#define local_cmpxchg(l, o, n) \
	(cmpxchg_local(&((l)->a.counter), (o), (n)))
/* Always has a lock prefix */
#define local_xchg(l, n) (xchg(&((l)->a.counter), (n)))

/**
 * local_add_unless - add unless the number is a given value
 * @l: pointer of type local_t
 * @a: the amount to add to l...
 * @u: ...unless l is equal to u.
 *
 * Atomically adds @a to @l, so long as it was not @u.
 * Returns non-zero if @l was not @u, and zero otherwise.
 */
#define local_add_unless(l, a, u)				\
({								\
	long c, old;						\
	c = local_read((l));					\
	for (;;) {						\
		if (unlikely(c == (u)))				\
			break;					\
		old = local_cmpxchg((l), c, c + (a));		\
		if (likely(old == c))				\
			break;					\
		c = old;					\
	}							\
	c != (u);						\
})
#define local_inc_not_zero(l) local_add_unless((l), 1, 0)

/* On x86_32, these are no better than the atomic variants.
 * On x86-64 these are better than the atomic variants on SMP kernels
 * because they dont use a lock prefix.
 */
#define __local_inc(l)		local_inc(l)
#define __local_dec(l)		local_dec(l)
#define __local_add(i, l)	local_add((i), (l))
#define __local_sub(i, l)	local_sub((i), (l))

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 *
 * X86_64: This could be done better if we moved the per cpu data directly
 * after GS.
 */

/* Need to disable preemption for the cpu local counters otherwise we could
   still access a variable of a previous CPU in a non atomic way. */
#define cpu_local_wrap_v(l)		\
({					\
	local_t res__;			\
	preempt_disable(); 		\
	res__ = (l);			\
	preempt_enable();		\
	res__;				\
})
#define cpu_local_wrap(l)		\
({					\
	preempt_disable();		\
	(l);				\
	preempt_enable();		\
})					\

#define cpu_local_read(l)    cpu_local_wrap_v(local_read(&__get_cpu_var((l))))
#define cpu_local_set(l, i)  cpu_local_wrap(local_set(&__get_cpu_var((l)), (i)))
#define cpu_local_inc(l)     cpu_local_wrap(local_inc(&__get_cpu_var((l))))
#define cpu_local_dec(l)     cpu_local_wrap(local_dec(&__get_cpu_var((l))))
#define cpu_local_add(i, l)  cpu_local_wrap(local_add((i), &__get_cpu_var((l))))
#define cpu_local_sub(i, l)  cpu_local_wrap(local_sub((i), &__get_cpu_var((l))))

#define __cpu_local_inc(l)	cpu_local_inc((l))
#define __cpu_local_dec(l)	cpu_local_dec((l))
#define __cpu_local_add(i, l)	cpu_local_add((i), (l))
#define __cpu_local_sub(i, l)	cpu_local_sub((i), (l))

#endif /* ASM_X86__LOCAL_H */
