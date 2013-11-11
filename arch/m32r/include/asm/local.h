#ifndef __M32R_LOCAL_H
#define __M32R_LOCAL_H

/*
 *  linux/include/asm-m32r/local.h
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 *    Copyright (C) 2007  Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 */

#include <linux/percpu.h>
#include <asm/assembler.h>
#include <asm/local.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } local_t;

#define LOCAL_INIT(i)	{ (i) }

/**
 * local_read - read local variable
 * @l: pointer of type local_t
 *
 * Atomically reads the value of @l.
 */
#define local_read(l)	((l)->counter)

/**
 * local_set - set local variable
 * @l: pointer of type local_t
 * @i: required value
 *
 * Atomically sets the value of @l to @i.
 */
#define local_set(l, i)	(((l)->counter) = (i))

/**
 * local_add_return - add long to local variable and return it
 * @i: long value to add
 * @l: pointer of type local_t
 *
 * Atomically adds @i to @l and return (@i + @l).
 */
static inline long local_add_return(long i, local_t *l)
{
	unsigned long flags;
	long result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# local_add_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		"ld %0, @%1;			\n\t"
		"add	%0, %2;			\n\t"
		"st %0, @%1;			\n\t"
		: "=&r" (result)
		: "r" (&l->counter), "r" (i)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * local_sub_return - subtract long from local variable and return it
 * @i: long value to subtract
 * @l: pointer of type local_t
 *
 * Atomically subtracts @i from @l and return (@l - @i).
 */
static inline long local_sub_return(long i, local_t *l)
{
	unsigned long flags;
	long result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# local_sub_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		"ld %0, @%1;			\n\t"
		"sub	%0, %2;			\n\t"
		"st %0, @%1;			\n\t"
		: "=&r" (result)
		: "r" (&l->counter), "r" (i)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * local_add - add long to local variable
 * @i: long value to add
 * @l: pointer of type local_t
 *
 * Atomically adds @i to @l.
 */
#define local_add(i, l) ((void) local_add_return((i), (l)))

/**
 * local_sub - subtract the local variable
 * @i: long value to subtract
 * @l: pointer of type local_t
 *
 * Atomically subtracts @i from @l.
 */
#define local_sub(i, l) ((void) local_sub_return((i), (l)))

/**
 * local_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @l: pointer of type local_t
 *
 * Atomically subtracts @i from @l and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#define local_sub_and_test(i, l) (local_sub_return((i), (l)) == 0)

/**
 * local_inc_return - increment local variable and return it
 * @l: pointer of type local_t
 *
 * Atomically increments @l by 1 and returns the result.
 */
static inline long local_inc_return(local_t *l)
{
	unsigned long flags;
	long result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# local_inc_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		"ld %0, @%1;			\n\t"
		"addi	%0, #1;			\n\t"
		"st %0, @%1;			\n\t"
		: "=&r" (result)
		: "r" (&l->counter)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * local_dec_return - decrement local variable and return it
 * @l: pointer of type local_t
 *
 * Atomically decrements @l by 1 and returns the result.
 */
static inline long local_dec_return(local_t *l)
{
	unsigned long flags;
	long result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# local_dec_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		"ld %0, @%1;			\n\t"
		"addi	%0, #-1;		\n\t"
		"st %0, @%1;			\n\t"
		: "=&r" (result)
		: "r" (&l->counter)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * local_inc - increment local variable
 * @l: pointer of type local_t
 *
 * Atomically increments @l by 1.
 */
#define local_inc(l) ((void)local_inc_return(l))

/**
 * local_dec - decrement local variable
 * @l: pointer of type local_t
 *
 * Atomically decrements @l by 1.
 */
#define local_dec(l) ((void)local_dec_return(l))

/**
 * local_inc_and_test - increment and test
 * @l: pointer of type local_t
 *
 * Atomically increments @l by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define local_inc_and_test(l) (local_inc_return(l) == 0)

/**
 * local_dec_and_test - decrement and test
 * @l: pointer of type local_t
 *
 * Atomically decrements @l by 1 and
 * returns true if the result is 0, or false for all
 * other cases.
 */
#define local_dec_and_test(l) (local_dec_return(l) == 0)

/**
 * local_add_negative - add and test if negative
 * @l: pointer of type local_t
 * @i: integer value to add
 *
 * Atomically adds @i to @l and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#define local_add_negative(i, l) (local_add_return((i), (l)) < 0)

#define local_cmpxchg(l, o, n) (cmpxchg_local(&((l)->counter), (o), (n)))
#define local_xchg(v, new) (xchg_local(&((l)->counter), new))

/**
 * local_add_unless - add unless the number is a given value
 * @l: pointer of type local_t
 * @a: the amount to add to l...
 * @u: ...unless l is equal to u.
 *
 * Atomically adds @a to @l, so long as it was not @u.
 * Returns non-zero if @l was not @u, and zero otherwise.
 */
static inline int local_add_unless(local_t *l, long a, long u)
{
	long c, old;
	c = local_read(l);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = local_cmpxchg((l), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define local_inc_not_zero(l) local_add_unless((l), 1, 0)

static inline void local_clear_mask(unsigned long  mask, local_t *addr)
{
	unsigned long flags;
	unsigned long tmp;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# local_clear_mask		\n\t"
		DCACHE_CLEAR("%0", "r5", "%1")
		"ld %0, @%1;			\n\t"
		"and	%0, %2;			\n\t"
		"st %0, @%1;			\n\t"
		: "=&r" (tmp)
		: "r" (addr), "r" (~mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

static inline void local_set_mask(unsigned long  mask, local_t *addr)
{
	unsigned long flags;
	unsigned long tmp;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# local_set_mask		\n\t"
		DCACHE_CLEAR("%0", "r5", "%1")
		"ld %0, @%1;			\n\t"
		"or	%0, %2;			\n\t"
		"st %0, @%1;			\n\t"
		: "=&r" (tmp)
		: "r" (addr), "r" (mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/* Atomic operations are already serializing on m32r */
#define smp_mb__before_local_dec()	barrier()
#define smp_mb__after_local_dec()	barrier()
#define smp_mb__before_local_inc()	barrier()
#define smp_mb__after_local_inc()	barrier()

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 */

#define __local_inc(l)		((l)->a.counter++)
#define __local_dec(l)		((l)->a.counter++)
#define __local_add(i, l)	((l)->a.counter += (i))
#define __local_sub(i, l)	((l)->a.counter -= (i))

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 */

#endif /* __M32R_LOCAL_H */
