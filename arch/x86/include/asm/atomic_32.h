#ifndef _ASM_X86_ATOMIC_32_H
#define _ASM_X86_ATOMIC_32_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/processor.h>
#include <asm/cmpxchg.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
static inline int atomic_read(const atomic_t *v)
{
	return v->counter;
}

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
static inline void atomic_set(atomic_t *v, int i)
{
	v->counter = i;
}

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	asm volatile(LOCK_PREFIX "addl %1,%0"
		     : "+m" (v->counter)
		     : "ir" (i));
}

/**
 * atomic_sub - subtract integer from atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.
 */
static inline void atomic_sub(int i, atomic_t *v)
{
	asm volatile(LOCK_PREFIX "subl %1,%0"
		     : "+m" (v->counter)
		     : "ir" (i));
}

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic_sub_and_test(int i, atomic_t *v)
{
	unsigned char c;

	asm volatile(LOCK_PREFIX "subl %2,%0; sete %1"
		     : "+m" (v->counter), "=qm" (c)
		     : "ir" (i) : "memory");
	return c;
}

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
static inline void atomic_inc(atomic_t *v)
{
	asm volatile(LOCK_PREFIX "incl %0"
		     : "+m" (v->counter));
}

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
static inline void atomic_dec(atomic_t *v)
{
	asm volatile(LOCK_PREFIX "decl %0"
		     : "+m" (v->counter));
}

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline int atomic_dec_and_test(atomic_t *v)
{
	unsigned char c;

	asm volatile(LOCK_PREFIX "decl %0; sete %1"
		     : "+m" (v->counter), "=qm" (c)
		     : : "memory");
	return c != 0;
}

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic_inc_and_test(atomic_t *v)
{
	unsigned char c;

	asm volatile(LOCK_PREFIX "incl %0; sete %1"
		     : "+m" (v->counter), "=qm" (c)
		     : : "memory");
	return c != 0;
}

/**
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
static inline int atomic_add_negative(int i, atomic_t *v)
{
	unsigned char c;

	asm volatile(LOCK_PREFIX "addl %2,%0; sets %1"
		     : "+m" (v->counter), "=qm" (c)
		     : "ir" (i) : "memory");
	return c;
}

/**
 * atomic_add_return - add integer and return
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static inline int atomic_add_return(int i, atomic_t *v)
{
	int __i;
#ifdef CONFIG_M386
	unsigned long flags;
	if (unlikely(boot_cpu_data.x86 <= 3))
		goto no_xadd;
#endif
	/* Modern 486+ processor */
	__i = i;
	asm volatile(LOCK_PREFIX "xaddl %0, %1"
		     : "+r" (i), "+m" (v->counter)
		     : : "memory");
	return i + __i;

#ifdef CONFIG_M386
no_xadd: /* Legacy 386 processor */
	local_irq_save(flags);
	__i = atomic_read(v);
	atomic_set(v, i + __i);
	local_irq_restore(flags);
	return i + __i;
#endif
}

/**
 * atomic_sub_return - subtract integer and return
 * @v: pointer of type atomic_t
 * @i: integer value to subtract
 *
 * Atomically subtracts @i from @v and returns @v - @i
 */
static inline int atomic_sub_return(int i, atomic_t *v)
{
	return atomic_add_return(-i, v);
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	return cmpxchg(&v->counter, old, new);
}

static inline int atomic_xchg(atomic_t *v, int new)
{
	return xchg(&v->counter, new);
}

/**
 * atomic_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as @v was not already @u.
 * Returns non-zero if @v was not @u, and zero otherwise.
 */
static inline int atomic_add_unless(atomic_t *v, int a, int u)
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

#define atomic_inc_return(v)  (atomic_add_return(1, v))
#define atomic_dec_return(v)  (atomic_sub_return(1, v))

/* These are x86-specific, used by some header files */
#define atomic_clear_mask(mask, addr)				\
	asm volatile(LOCK_PREFIX "andl %0,%1"			\
		     : : "r" (~(mask)), "m" (*(addr)) : "memory")

#define atomic_set_mask(mask, addr)				\
	asm volatile(LOCK_PREFIX "orl %0,%1"				\
		     : : "r" (mask), "m" (*(addr)) : "memory")

/* Atomic operations are already serializing on x86 */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

/* An 64bit atomic type */

typedef struct {
	u64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(val)	{ (val) }

extern u64 atomic64_cmpxchg(atomic64_t *ptr, u64 old_val, u64 new_val);

/**
 * atomic64_xchg - xchg atomic64 variable
 * @ptr:      pointer to type atomic64_t
 * @new_val:  value to assign
 *
 * Atomically xchgs the value of @ptr to @new_val and returns
 * the old value.
 */
extern u64 atomic64_xchg(atomic64_t *ptr, u64 new_val);

/**
 * atomic64_set - set atomic64 variable
 * @ptr:      pointer to type atomic64_t
 * @new_val:  value to assign
 *
 * Atomically sets the value of @ptr to @new_val.
 */
extern void atomic64_set(atomic64_t *ptr, u64 new_val);

/**
 * atomic64_read - read atomic64 variable
 * @ptr:      pointer to type atomic64_t
 *
 * Atomically reads the value of @ptr and returns it.
 */
static inline u64 atomic64_read(atomic64_t *ptr)
{
	u64 res;

	/*
	 * Note, we inline this atomic64_t primitive because
	 * it only clobbers EAX/EDX and leaves the others
	 * untouched. We also (somewhat subtly) rely on the
	 * fact that cmpxchg8b returns the current 64-bit value
	 * of the memory location we are touching:
	 */
	asm volatile(
		"mov %%ebx, %%eax\n\t"
		"mov %%ecx, %%edx\n\t"
		LOCK_PREFIX "cmpxchg8b %1\n"
			: "=&A" (res)
			: "m" (*ptr)
		);

	return res;
}

extern u64 atomic64_read(atomic64_t *ptr);

/**
 * atomic64_add_return - add and return
 * @delta: integer value to add
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically adds @delta to @ptr and returns @delta + *@ptr
 */
extern u64 atomic64_add_return(u64 delta, atomic64_t *ptr);

/*
 * Other variants with different arithmetic operators:
 */
extern u64 atomic64_sub_return(u64 delta, atomic64_t *ptr);
extern u64 atomic64_inc_return(atomic64_t *ptr);
extern u64 atomic64_dec_return(atomic64_t *ptr);

/**
 * atomic64_add - add integer to atomic64 variable
 * @delta: integer value to add
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically adds @delta to @ptr.
 */
extern void atomic64_add(u64 delta, atomic64_t *ptr);

/**
 * atomic64_sub - subtract the atomic64 variable
 * @delta: integer value to subtract
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically subtracts @delta from @ptr.
 */
extern void atomic64_sub(u64 delta, atomic64_t *ptr);

/**
 * atomic64_sub_and_test - subtract value from variable and test result
 * @delta: integer value to subtract
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically subtracts @delta from @ptr and returns
 * true if the result is zero, or false for all
 * other cases.
 */
extern int atomic64_sub_and_test(u64 delta, atomic64_t *ptr);

/**
 * atomic64_inc - increment atomic64 variable
 * @ptr: pointer to type atomic64_t
 *
 * Atomically increments @ptr by 1.
 */
extern void atomic64_inc(atomic64_t *ptr);

/**
 * atomic64_dec - decrement atomic64 variable
 * @ptr: pointer to type atomic64_t
 *
 * Atomically decrements @ptr by 1.
 */
extern void atomic64_dec(atomic64_t *ptr);

/**
 * atomic64_dec_and_test - decrement and test
 * @ptr: pointer to type atomic64_t
 *
 * Atomically decrements @ptr by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
extern int atomic64_dec_and_test(atomic64_t *ptr);

/**
 * atomic64_inc_and_test - increment and test
 * @ptr: pointer to type atomic64_t
 *
 * Atomically increments @ptr by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
extern int atomic64_inc_and_test(atomic64_t *ptr);

/**
 * atomic64_add_negative - add and test if negative
 * @delta: integer value to add
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically adds @delta to @ptr and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
extern int atomic64_add_negative(u64 delta, atomic64_t *ptr);

#include <asm-generic/atomic-long.h>
#endif /* _ASM_X86_ATOMIC_32_H */
