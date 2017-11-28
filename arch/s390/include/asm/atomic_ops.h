/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Low level function for atomic operations
 *
 * Copyright IBM Corp. 1999, 2016
 */

#ifndef __ARCH_S390_ATOMIC_OPS__
#define __ARCH_S390_ATOMIC_OPS__

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

#define __ATOMIC_OP(op_name, op_type, op_string, op_barrier)		\
static inline op_type op_name(op_type val, op_type *ptr)		\
{									\
	op_type old;							\
									\
	asm volatile(							\
		op_string "	%[old],%[val],%[ptr]\n"			\
		op_barrier						\
		: [old] "=d" (old), [ptr] "+Q" (*ptr)			\
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
static inline void op_name(op_type val, op_type *ptr)			\
{									\
	asm volatile(							\
		op_string "	%[ptr],%[val]\n"			\
		op_barrier						\
		: [ptr] "+Q" (*ptr) : [val] "i" (val) : "cc", "memory");\
}

#define __ATOMIC_CONST_OPS(op_name, op_type, op_string)			\
	__ATOMIC_CONST_OP(op_name, op_type, op_string, "\n")		\
	__ATOMIC_CONST_OP(op_name##_barrier, op_type, op_string, "bcr 14,0\n")

__ATOMIC_CONST_OPS(__atomic_add_const, int, "asi")
__ATOMIC_CONST_OPS(__atomic64_add_const, long, "agsi")

#undef __ATOMIC_CONST_OPS
#undef __ATOMIC_CONST_OP

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define __ATOMIC_OP(op_name, op_string)					\
static inline int op_name(int val, int *ptr)				\
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
static inline long op_name(long val, long *ptr)				\
{									\
	long old, new;							\
									\
	asm volatile(							\
		"0:	lgr	%[new],%[old]\n"			\
		op_string "	%[new],%[val]\n"			\
		"	csg	%[old],%[new],%[ptr]\n"			\
		"	jl	0b"					\
		: [old] "=d" (old), [new] "=&d" (new), [ptr] "+Q" (*ptr)\
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

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

static inline int __atomic_cmpxchg(int *ptr, int old, int new)
{
	return __sync_val_compare_and_swap(ptr, old, new);
}

static inline int __atomic_cmpxchg_bool(int *ptr, int old, int new)
{
	return __sync_bool_compare_and_swap(ptr, old, new);
}

static inline long __atomic64_cmpxchg(long *ptr, long old, long new)
{
	return __sync_val_compare_and_swap(ptr, old, new);
}

static inline long __atomic64_cmpxchg_bool(long *ptr, long old, long new)
{
	return __sync_bool_compare_and_swap(ptr, old, new);
}

#endif /* __ARCH_S390_ATOMIC_OPS__  */
