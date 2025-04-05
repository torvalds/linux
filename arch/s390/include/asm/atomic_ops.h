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
#include <asm/asm.h>

static __always_inline int __atomic_read(const int *ptr)
{
	int val;

	asm volatile(
		"	l	%[val],%[ptr]\n"
		: [val] "=d" (val) : [ptr] "R" (*ptr));
	return val;
}

static __always_inline void __atomic_set(int *ptr, int val)
{
	if (__builtin_constant_p(val) && val >= S16_MIN && val <= S16_MAX) {
		asm volatile(
			"	mvhi	%[ptr],%[val]\n"
			: [ptr] "=Q" (*ptr) : [val] "K" (val));
	} else {
		asm volatile(
			"	st	%[val],%[ptr]\n"
			: [ptr] "=R" (*ptr) : [val] "d" (val));
	}
}

static __always_inline long __atomic64_read(const long *ptr)
{
	long val;

	asm volatile(
		"	lg	%[val],%[ptr]\n"
		: [val] "=d" (val) : [ptr] "RT" (*ptr));
	return val;
}

static __always_inline void __atomic64_set(long *ptr, long val)
{
	if (__builtin_constant_p(val) && val >= S16_MIN && val <= S16_MAX) {
		asm volatile(
			"	mvghi	%[ptr],%[val]\n"
			: [ptr] "=Q" (*ptr) : [val] "K" (val));
	} else {
		asm volatile(
			"	stg	%[val],%[ptr]\n"
			: [ptr] "=RT" (*ptr) : [val] "d" (val));
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
	__ATOMIC_OP(op_name, op_type, op_string, "")			\
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
	__ATOMIC_CONST_OP(op_name, op_type, op_string, "")		\
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

#if defined(MARCH_HAS_Z196_FEATURES) && defined(__HAVE_ASM_FLAG_OUTPUTS__)

#define __ATOMIC_TEST_OP(op_name, op_type, op_string, op_barrier)	\
static __always_inline bool op_name(op_type val, op_type *ptr)		\
{									\
	op_type tmp;							\
	int cc;								\
									\
	asm volatile(							\
		op_string "	%[tmp],%[val],%[ptr]\n"			\
		op_barrier						\
		: "=@cc" (cc), [tmp] "=d" (tmp), [ptr] "+QS" (*ptr)	\
		: [val] "d" (val)					\
		: "memory");						\
	return (cc == 0) || (cc == 2);					\
}									\

#define __ATOMIC_TEST_OPS(op_name, op_type, op_string)			\
	__ATOMIC_TEST_OP(op_name, op_type, op_string, "")		\
	__ATOMIC_TEST_OP(op_name##_barrier, op_type, op_string, "bcr 14,0\n")

__ATOMIC_TEST_OPS(__atomic_add_and_test, int, "laal")
__ATOMIC_TEST_OPS(__atomic64_add_and_test, long, "laalg")

#undef __ATOMIC_TEST_OPS
#undef __ATOMIC_TEST_OP

#define __ATOMIC_CONST_TEST_OP(op_name, op_type, op_string, op_barrier)	\
static __always_inline bool op_name(op_type val, op_type *ptr)		\
{									\
	int cc;								\
									\
	asm volatile(							\
		op_string "	%[ptr],%[val]\n"			\
		op_barrier						\
		: "=@cc" (cc), [ptr] "+QS" (*ptr)			\
		: [val] "i" (val)					\
		: "memory");						\
	return (cc == 0) || (cc == 2);					\
}

#define __ATOMIC_CONST_TEST_OPS(op_name, op_type, op_string)		\
	__ATOMIC_CONST_TEST_OP(op_name, op_type, op_string, "")		\
	__ATOMIC_CONST_TEST_OP(op_name##_barrier, op_type, op_string, "bcr 14,0\n")

__ATOMIC_CONST_TEST_OPS(__atomic_add_const_and_test, int, "alsi")
__ATOMIC_CONST_TEST_OPS(__atomic64_add_const_and_test, long, "algsi")

#undef __ATOMIC_CONST_TEST_OPS
#undef __ATOMIC_CONST_TEST_OP

#else /* defined(MARCH_HAS_Z196_FEATURES) && defined(__HAVE_ASM_FLAG_OUTPUTS__) */

#define __ATOMIC_TEST_OP(op_name, op_func, op_type)			\
static __always_inline bool op_name(op_type val, op_type *ptr)		\
{									\
	return op_func(val, ptr) == -val;				\
}

__ATOMIC_TEST_OP(__atomic_add_and_test,			__atomic_add,		int)
__ATOMIC_TEST_OP(__atomic_add_and_test_barrier,		__atomic_add_barrier,	int)
__ATOMIC_TEST_OP(__atomic_add_const_and_test,		__atomic_add,		int)
__ATOMIC_TEST_OP(__atomic_add_const_and_test_barrier,	__atomic_add_barrier,	int)
__ATOMIC_TEST_OP(__atomic64_add_and_test,		__atomic64_add,		long)
__ATOMIC_TEST_OP(__atomic64_add_and_test_barrier,	__atomic64_add_barrier, long)
__ATOMIC_TEST_OP(__atomic64_add_const_and_test,		__atomic64_add,		long)
__ATOMIC_TEST_OP(__atomic64_add_const_and_test_barrier,	__atomic64_add_barrier,	long)

#undef __ATOMIC_TEST_OP

#endif /* defined(MARCH_HAS_Z196_FEATURES) && defined(__HAVE_ASM_FLAG_OUTPUTS__) */

#endif /* __ARCH_S390_ATOMIC_OPS__  */
