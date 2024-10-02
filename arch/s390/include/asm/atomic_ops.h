/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Low level function for atomic operations
 *
 * Copyright IBM Corp. 1999, 2016
 */

#ifndef __ARCH_S390_ATOMIC_OPS__
#define __ARCH_S390_ATOMIC_OPS__

#include <linux/limits.h>
#include <asm/march.h>

static __always_inline int __atomic_read(const atomic_t *v)
{
	int c;

	asm volatile(
		"	l	%[c],%[counter]\n"
		: [c] "=d" (c) : [counter] "R" (v->counter));
	return c;
}

static __always_inline void __atomic_set(atomic_t *v, int i)
{
	if (__builtin_constant_p(i) && i >= S16_MIN && i <= S16_MAX) {
		asm volatile(
			"	mvhi	%[counter], %[i]\n"
			: [counter] "=Q" (v->counter) : [i] "K" (i));
	} else {
		asm volatile(
			"	st	%[i],%[counter]\n"
			: [counter] "=R" (v->counter) : [i] "d" (i));
	}
}

static __always_inline s64 __atomic64_read(const atomic64_t *v)
{
	s64 c;

	asm volatile(
		"	lg	%[c],%[counter]\n"
		: [c] "=d" (c) : [counter] "RT" (v->counter));
	return c;
}

static __always_inline void __atomic64_set(atomic64_t *v, s64 i)
{
	if (__builtin_constant_p(i) && i >= S16_MIN && i <= S16_MAX) {
		asm volatile(
			"	mvghi	%[counter], %[i]\n"
			: [counter] "=Q" (v->counter) : [i] "K" (i));
	} else {
		asm volatile(
			"	stg	%[i],%[counter]\n"
			: [counter] "=RT" (v->counter) : [i] "d" (i));
	}
}

#ifdef MARCH_HAS_Z196_FEATURES

#define __ATOMIC_OP(op_name, op_type, op_string, op_barrier)		\
static __always_inline op_type op_name(op_type val, op_type *ptr)	\
{									\
	op_type old;							\
									\
	asm volatile(							\
		op_string "	%[old],%[val],%[ptr]\n"			\
		op_barrier						\
		: [old] "=d" (old), [ptr] "+QS" (*ptr)			\
		: [val] "d" (val) : "cc", "memory");			\
	return old;							\
}									\

#define __ATOMIC_OPS(op_name, op_type, op_string)			\
	__ATOMIC_OP(op_name, op_type, op_string, "\n")			\
	__ATOMIC_OP(op_name##_barrier, op_type, op_string, "bcr 14,0\n")

__ATOMIC_OPS(__atomic_add, int, "laa")
__ATOMIC_OPS(__atomic_and, int, "lan")
__ATOMIC_OPS(__atomic_or,  int, "lao")
__ATOMIC_OPS(__atomic_xor, int, "lax")

__ATOMIC_OPS(__atomic64_add, long, "laag")
__ATOMIC_OPS(__atomic64_and, long, "lang")
__ATOMIC_OPS(__atomic64_or,  long, "laog")
__ATOMIC_OPS(__atomic64_xor, long, "laxg")

#undef __ATOMIC_OPS
#undef __ATOMIC_OP

#define __ATOMIC_CONST_OP(op_name, op_type, op_string, op_barrier)	\
static __always_inline void op_name(op_type val, op_type *ptr)		\
{									\
	asm volatile(							\
		op_string "	%[ptr],%[val]\n"			\
		op_barrier						\
		: [ptr] "+QS" (*ptr) : [val] "i" (val) : "cc", "memory");\
}

#define __ATOMIC_CONST_OPS(op_name, op_type, op_string)			\
	__ATOMIC_CONST_OP(op_name, op_type, op_string, "\n")		\
	__ATOMIC_CONST_OP(op_name##_barrier, op_type, op_string, "bcr 14,0\n")

__ATOMIC_CONST_OPS(__atomic_add_const, int, "asi")
__ATOMIC_CONST_OPS(__atomic64_add_const, long, "agsi")

#undef __ATOMIC_CONST_OPS
#undef __ATOMIC_CONST_OP

#else /* MARCH_HAS_Z196_FEATURES */

#define __ATOMIC_OP(op_name, op_string)					\
static __always_inline int op_name(int val, int *ptr)			\
{									\
	int old, new;							\
									\
	asm volatile(							\
		"0:	lr	%[new],%[old]\n"			\
		op_string "	%[new],%[val]\n"			\
		"	cs	%[old],%[new],%[ptr]\n"			\
		"	jl	0b"					\
		: [old] "=d" (old), [new] "=&d" (new), [ptr] "+Q" (*ptr)\
		: [val] "d" (val), "0" (*ptr) : "cc", "memory");	\
	return old;							\
}

#define __ATOMIC_OPS(op_name, op_string)				\
	__ATOMIC_OP(op_name, op_string)					\
	__ATOMIC_OP(op_name##_barrier, op_string)

__ATOMIC_OPS(__atomic_add, "ar")
__ATOMIC_OPS(__atomic_and, "nr")
__ATOMIC_OPS(__atomic_or,  "or")
__ATOMIC_OPS(__atomic_xor, "xr")

#undef __ATOMIC_OPS

#define __ATOMIC64_OP(op_name, op_string)				\
static __always_inline long op_name(long val, long *ptr)		\
{									\
	long old, new;							\
									\
	asm volatile(							\
		"0:	lgr	%[new],%[old]\n"			\
		op_string "	%[new],%[val]\n"			\
		"	csg	%[old],%[new],%[ptr]\n"			\
		"	jl	0b"					\
		: [old] "=d" (old), [new] "=&d" (new), [ptr] "+QS" (*ptr)\
		: [val] "d" (val), "0" (*ptr) : "cc", "memory");	\
	return old;							\
}

#define __ATOMIC64_OPS(op_name, op_string)				\
	__ATOMIC64_OP(op_name, op_string)				\
	__ATOMIC64_OP(op_name##_barrier, op_string)

__ATOMIC64_OPS(__atomic64_add, "agr")
__ATOMIC64_OPS(__atomic64_and, "ngr")
__ATOMIC64_OPS(__atomic64_or,  "ogr")
__ATOMIC64_OPS(__atomic64_xor, "xgr")

#undef __ATOMIC64_OPS

#define __atomic_add_const(val, ptr)		__atomic_add(val, ptr)
#define __atomic_add_const_barrier(val, ptr)	__atomic_add(val, ptr)
#define __atomic64_add_const(val, ptr)		__atomic64_add(val, ptr)
#define __atomic64_add_const_barrier(val, ptr)	__atomic64_add(val, ptr)

#endif /* MARCH_HAS_Z196_FEATURES */

static __always_inline int __atomic_cmpxchg(int *ptr, int old, int new)
{
	asm volatile(
		"	cs	%[old],%[new],%[ptr]"
		: [old] "+d" (old), [ptr] "+Q" (*ptr)
		: [new] "d" (new)
		: "cc", "memory");
	return old;
}

static __always_inline long __atomic64_cmpxchg(long *ptr, long old, long new)
{
	asm volatile(
		"	csg	%[old],%[new],%[ptr]"
		: [old] "+d" (old), [ptr] "+QS" (*ptr)
		: [new] "d" (new)
		: "cc", "memory");
	return old;
}

/* GCC versions before 14.2.0 may die with an ICE in some configurations. */
#if defined(__GCC_ASM_FLAG_OUTPUTS__) && !(IS_ENABLED(CONFIG_CC_IS_GCC) && (GCC_VERSION < 140200))

static __always_inline bool __atomic_cmpxchg_bool(int *ptr, int old, int new)
{
	int cc;

	asm volatile(
		"	cs	%[old],%[new],%[ptr]"
		: [old] "+d" (old), [ptr] "+Q" (*ptr), "=@cc" (cc)
		: [new] "d" (new)
		: "memory");
	return cc == 0;
}

static __always_inline bool __atomic64_cmpxchg_bool(long *ptr, long old, long new)
{
	int cc;

	asm volatile(
		"	csg	%[old],%[new],%[ptr]"
		: [old] "+d" (old), [ptr] "+QS" (*ptr), "=@cc" (cc)
		: [new] "d" (new)
		: "memory");
	return cc == 0;
}

#else /* __GCC_ASM_FLAG_OUTPUTS__ */

static __always_inline bool __atomic_cmpxchg_bool(int *ptr, int old, int new)
{
	int old_expected = old;

	asm volatile(
		"	cs	%[old],%[new],%[ptr]"
		: [old] "+d" (old), [ptr] "+Q" (*ptr)
		: [new] "d" (new)
		: "cc", "memory");
	return old == old_expected;
}

static __always_inline bool __atomic64_cmpxchg_bool(long *ptr, long old, long new)
{
	long old_expected = old;

	asm volatile(
		"	csg	%[old],%[new],%[ptr]"
		: [old] "+d" (old), [ptr] "+QS" (*ptr)
		: [new] "d" (new)
		: "cc", "memory");
	return old == old_expected;
}

#endif /* __GCC_ASM_FLAG_OUTPUTS__ */

#endif /* __ARCH_S390_ATOMIC_OPS__  */
