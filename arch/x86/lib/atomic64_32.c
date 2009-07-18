#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/cmpxchg.h>
#include <asm/atomic.h>

static noinline u64 cmpxchg8b(u64 *ptr, u64 old, u64 new)
{
	u32 low = new;
	u32 high = new >> 32;

	asm volatile(
		LOCK_PREFIX "cmpxchg8b %1\n"
		     : "+A" (old), "+m" (*ptr)
		     :  "b" (low),  "c" (high)
		     );
	return old;
}

u64 atomic64_cmpxchg(atomic64_t *ptr, u64 old_val, u64 new_val)
{
	return cmpxchg8b(&ptr->counter, old_val, new_val);
}
EXPORT_SYMBOL(atomic64_cmpxchg);

/**
 * atomic64_xchg - xchg atomic64 variable
 * @ptr:      pointer to type atomic64_t
 * @new_val:  value to assign
 *
 * Atomically xchgs the value of @ptr to @new_val and returns
 * the old value.
 */
u64 atomic64_xchg(atomic64_t *ptr, u64 new_val)
{
	/*
	 * Try first with a (possibly incorrect) assumption about
	 * what we have there. We'll do two loops most likely,
	 * but we'll get an ownership MESI transaction straight away
	 * instead of a read transaction followed by a
	 * flush-for-ownership transaction:
	 */
	u64 old_val, real_val = 0;

	do {
		old_val = real_val;

		real_val = atomic64_cmpxchg(ptr, old_val, new_val);

	} while (real_val != old_val);

	return old_val;
}
EXPORT_SYMBOL(atomic64_xchg);

/**
 * atomic64_set - set atomic64 variable
 * @ptr:      pointer to type atomic64_t
 * @new_val:  value to assign
 *
 * Atomically sets the value of @ptr to @new_val.
 */
void atomic64_set(atomic64_t *ptr, u64 new_val)
{
	atomic64_xchg(ptr, new_val);
}
EXPORT_SYMBOL(atomic64_set);

/**
EXPORT_SYMBOL(atomic64_read);
 * atomic64_add_return - add and return
 * @delta: integer value to add
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically adds @delta to @ptr and returns @delta + *@ptr
 */
noinline u64 atomic64_add_return(u64 delta, atomic64_t *ptr)
{
	/*
	 * Try first with a (possibly incorrect) assumption about
	 * what we have there. We'll do two loops most likely,
	 * but we'll get an ownership MESI transaction straight away
	 * instead of a read transaction followed by a
	 * flush-for-ownership transaction:
	 */
	u64 old_val, new_val, real_val = 0;

	do {
		old_val = real_val;
		new_val = old_val + delta;

		real_val = atomic64_cmpxchg(ptr, old_val, new_val);

	} while (real_val != old_val);

	return new_val;
}
EXPORT_SYMBOL(atomic64_add_return);

u64 atomic64_sub_return(u64 delta, atomic64_t *ptr)
{
	return atomic64_add_return(-delta, ptr);
}
EXPORT_SYMBOL(atomic64_sub_return);

u64 atomic64_inc_return(atomic64_t *ptr)
{
	return atomic64_add_return(1, ptr);
}
EXPORT_SYMBOL(atomic64_inc_return);

u64 atomic64_dec_return(atomic64_t *ptr)
{
	return atomic64_sub_return(1, ptr);
}
EXPORT_SYMBOL(atomic64_dec_return);

/**
 * atomic64_add - add integer to atomic64 variable
 * @delta: integer value to add
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically adds @delta to @ptr.
 */
void atomic64_add(u64 delta, atomic64_t *ptr)
{
	atomic64_add_return(delta, ptr);
}
EXPORT_SYMBOL(atomic64_add);

/**
 * atomic64_sub - subtract the atomic64 variable
 * @delta: integer value to subtract
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically subtracts @delta from @ptr.
 */
void atomic64_sub(u64 delta, atomic64_t *ptr)
{
	atomic64_add(-delta, ptr);
}
EXPORT_SYMBOL(atomic64_sub);

/**
 * atomic64_sub_and_test - subtract value from variable and test result
 * @delta: integer value to subtract
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically subtracts @delta from @ptr and returns
 * true if the result is zero, or false for all
 * other cases.
 */
int atomic64_sub_and_test(u64 delta, atomic64_t *ptr)
{
	u64 new_val = atomic64_sub_return(delta, ptr);

	return new_val == 0;
}
EXPORT_SYMBOL(atomic64_sub_and_test);

/**
 * atomic64_inc - increment atomic64 variable
 * @ptr: pointer to type atomic64_t
 *
 * Atomically increments @ptr by 1.
 */
void atomic64_inc(atomic64_t *ptr)
{
	atomic64_add(1, ptr);
}
EXPORT_SYMBOL(atomic64_inc);

/**
 * atomic64_dec - decrement atomic64 variable
 * @ptr: pointer to type atomic64_t
 *
 * Atomically decrements @ptr by 1.
 */
void atomic64_dec(atomic64_t *ptr)
{
	atomic64_sub(1, ptr);
}
EXPORT_SYMBOL(atomic64_dec);

/**
 * atomic64_dec_and_test - decrement and test
 * @ptr: pointer to type atomic64_t
 *
 * Atomically decrements @ptr by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
int atomic64_dec_and_test(atomic64_t *ptr)
{
	return atomic64_sub_and_test(1, ptr);
}
EXPORT_SYMBOL(atomic64_dec_and_test);

/**
 * atomic64_inc_and_test - increment and test
 * @ptr: pointer to type atomic64_t
 *
 * Atomically increments @ptr by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
int atomic64_inc_and_test(atomic64_t *ptr)
{
	return atomic64_sub_and_test(-1, ptr);
}
EXPORT_SYMBOL(atomic64_inc_and_test);

/**
 * atomic64_add_negative - add and test if negative
 * @delta: integer value to add
 * @ptr:   pointer to type atomic64_t
 *
 * Atomically adds @delta to @ptr and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
int atomic64_add_negative(u64 delta, atomic64_t *ptr)
{
	s64 new_val = atomic64_add_return(delta, ptr);

	return new_val < 0;
}
EXPORT_SYMBOL(atomic64_add_negative);
