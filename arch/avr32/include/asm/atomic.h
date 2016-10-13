/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc.
 *
 * But use these as seldom as possible since they are slower than
 * regular operations.
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_ATOMIC_H
#define __ASM_AVR32_ATOMIC_H

#include <linux/types.h>
#include <asm/cmpxchg.h>

#define ATOMIC_INIT(i)  { (i) }

#define atomic_read(v)		READ_ONCE((v)->counter)
#define atomic_set(v, i)	WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP_RETURN(op, asm_op, asm_con)				\
static inline int __atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int result;							\
									\
	asm volatile(							\
		"/* atomic_" #op "_return */\n"				\
		"1:	ssrf	5\n"					\
		"	ld.w	%0, %2\n"				\
		"	" #asm_op "	%0, %3\n"			\
		"	stcond	%1, %0\n"				\
		"	brne	1b"					\
		: "=&r" (result), "=o" (v->counter)			\
		: "m" (v->counter), #asm_con (i)			\
		: "cc");						\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(op, asm_op, asm_con)				\
static inline int __atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	int result, val;						\
									\
	asm volatile(							\
		"/* atomic_fetch_" #op " */\n"				\
		"1:	ssrf	5\n"					\
		"	ld.w	%0, %3\n"				\
		"	mov	%1, %0\n"				\
		"	" #asm_op "	%1, %4\n"			\
		"	stcond	%2, %1\n"				\
		"	brne	1b"					\
		: "=&r" (result), "=&r" (val), "=o" (v->counter)	\
		: "m" (v->counter), #asm_con (i)			\
		: "cc");						\
									\
	return result;							\
}

ATOMIC_OP_RETURN(sub, sub, rKs21)
ATOMIC_OP_RETURN(add, add, r)
ATOMIC_FETCH_OP (sub, sub, rKs21)
ATOMIC_FETCH_OP (add, add, r)

#define ATOMIC_OPS(op, asm_op)						\
ATOMIC_OP_RETURN(op, asm_op, r)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	(void)__atomic_##op##_return(i, v);				\
}									\
ATOMIC_FETCH_OP(op, asm_op, r)						\
static inline int atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	return __atomic_fetch_##op(i, v);				\
}

ATOMIC_OPS(and, and)
ATOMIC_OPS(or, or)
ATOMIC_OPS(xor, eor)

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN

/*
 * Probably found the reason why we want to use sub with the signed 21-bit
 * limit, it uses one less register than the add instruction that can add up to
 * 32-bit values.
 *
 * Both instructions are 32-bit, to use a 16-bit instruction the immediate is
 * very small; 4 bit.
 *
 * sub 32-bit, type IV, takes a register and subtracts a 21-bit immediate.
 * add 32-bit, type II, adds two register values together.
 */
#define IS_21BIT_CONST(i)						\
	(__builtin_constant_p(i) && ((i) >= -1048575) && ((i) <= 1048576))

/*
 * atomic_add_return - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v. Returns the resulting value.
 */
static inline int atomic_add_return(int i, atomic_t *v)
{
	if (IS_21BIT_CONST(i))
		return __atomic_sub_return(-i, v);

	return __atomic_add_return(i, v);
}

static inline int atomic_fetch_add(int i, atomic_t *v)
{
	if (IS_21BIT_CONST(i))
		return __atomic_fetch_sub(-i, v);

	return __atomic_fetch_add(i, v);
}

/*
 * atomic_sub_return - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v. Returns the resulting value.
 */
static inline int atomic_sub_return(int i, atomic_t *v)
{
	if (IS_21BIT_CONST(i))
		return __atomic_sub_return(i, v);

	return __atomic_add_return(-i, v);
}

static inline int atomic_fetch_sub(int i, atomic_t *v)
{
	if (IS_21BIT_CONST(i))
		return __atomic_fetch_sub(i, v);

	return __atomic_fetch_add(-i, v);
}

/*
 * __atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
*/
static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int tmp, old = atomic_read(v);

	if (IS_21BIT_CONST(a)) {
		asm volatile(
			"/* __atomic_sub_unless */\n"
			"1:	ssrf	5\n"
			"	ld.w	%0, %2\n"
			"	cp.w	%0, %4\n"
			"	breq	1f\n"
			"	sub	%0, %3\n"
			"	stcond	%1, %0\n"
			"	brne	1b\n"
			"1:"
			: "=&r"(tmp), "=o"(v->counter)
			: "m"(v->counter), "rKs21"(-a), "rKs21"(u)
			: "cc", "memory");
	} else {
		asm volatile(
			"/* __atomic_add_unless */\n"
			"1:	ssrf	5\n"
			"	ld.w	%0, %2\n"
			"	cp.w	%0, %4\n"
			"	breq	1f\n"
			"	add	%0, %3\n"
			"	stcond	%1, %0\n"
			"	brne	1b\n"
			"1:"
			: "=&r"(tmp), "=o"(v->counter)
			: "m"(v->counter), "r"(a), "ir"(u)
			: "cc", "memory");
	}

	return old;
}

#undef IS_21BIT_CONST

/*
 * atomic_sub_if_positive - conditionally subtract integer from atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically test @v and subtract @i if @v is greater or equal than @i.
 * The function returns the old value of @v minus @i.
 */
static inline int atomic_sub_if_positive(int i, atomic_t *v)
{
	int result;

	asm volatile(
		"/* atomic_sub_if_positive */\n"
		"1:	ssrf	5\n"
		"	ld.w	%0, %2\n"
		"	sub	%0, %3\n"
		"	brlt	1f\n"
		"	stcond	%1, %0\n"
		"	brne	1b\n"
		"1:"
		: "=&r"(result), "=o"(v->counter)
		: "m"(v->counter), "ir"(i)
		: "cc", "memory");

	return result;
}

#define atomic_xchg(v, new)	(xchg(&((v)->counter), new))
#define atomic_cmpxchg(v, o, n)	(cmpxchg(&((v)->counter), (o), (n)))

#define atomic_sub(i, v)	(void)atomic_sub_return(i, v)
#define atomic_add(i, v)	(void)atomic_add_return(i, v)
#define atomic_dec(v)		atomic_sub(1, (v))
#define atomic_inc(v)		atomic_add(1, (v))

#define atomic_dec_return(v)	atomic_sub_return(1, v)
#define atomic_inc_return(v)	atomic_add_return(1, v)

#define atomic_sub_and_test(i, v) (atomic_sub_return(i, v) == 0)
#define atomic_inc_and_test(v) (atomic_add_return(1, v) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, v) == 0)
#define atomic_add_negative(i, v) (atomic_add_return(i, v) < 0)

#define atomic_dec_if_positive(v) atomic_sub_if_positive(1, v)

#endif /*  __ASM_AVR32_ATOMIC_H */
