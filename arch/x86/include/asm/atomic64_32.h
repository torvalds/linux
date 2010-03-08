#ifndef _ASM_X86_ATOMIC64_32_H
#define _ASM_X86_ATOMIC64_32_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/processor.h>
//#include <asm/cmpxchg.h>

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

#endif /* _ASM_X86_ATOMIC64_32_H */
