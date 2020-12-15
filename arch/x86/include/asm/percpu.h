/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PERCPU_H
#define _ASM_X86_PERCPU_H

#ifdef CONFIG_X86_64
#define __percpu_seg		gs
#else
#define __percpu_seg		fs
#endif

#ifdef __ASSEMBLY__

#ifdef CONFIG_SMP
#define PER_CPU_VAR(var)	%__percpu_seg:var
#else /* ! SMP */
#define PER_CPU_VAR(var)	var
#endif	/* SMP */

#ifdef CONFIG_X86_64_SMP
#define INIT_PER_CPU_VAR(var)  init_per_cpu__##var
#else
#define INIT_PER_CPU_VAR(var)  var
#endif

#else /* ...!ASSEMBLY */

#include <linux/kernel.h>
#include <linux/stringify.h>

#ifdef CONFIG_SMP
#define __percpu_prefix		"%%"__stringify(__percpu_seg)":"
#define __my_cpu_offset		this_cpu_read(this_cpu_off)

/*
 * Compared to the generic __my_cpu_offset version, the following
 * saves one instruction and avoids clobbering a temp register.
 */
#define arch_raw_cpu_ptr(ptr)				\
({							\
	unsigned long tcp_ptr__;			\
	asm volatile("add " __percpu_arg(1) ", %0"	\
		     : "=r" (tcp_ptr__)			\
		     : "m" (this_cpu_off), "0" (ptr));	\
	(typeof(*(ptr)) __kernel __force *)tcp_ptr__;	\
})
#else
#define __percpu_prefix		""
#endif

#define __percpu_arg(x)		__percpu_prefix "%" #x

/*
 * Initialized pointers to per-cpu variables needed for the boot
 * processor need to use these macros to get the proper address
 * offset from __per_cpu_load on SMP.
 *
 * There also must be an entry in vmlinux_64.lds.S
 */
#define DECLARE_INIT_PER_CPU(var) \
       extern typeof(var) init_per_cpu_var(var)

#ifdef CONFIG_X86_64_SMP
#define init_per_cpu_var(var)  init_per_cpu__##var
#else
#define init_per_cpu_var(var)  var
#endif

/* For arch-specific code, we can use direct single-insn ops (they
 * don't give an lvalue though). */

#define __pcpu_type_1 u8
#define __pcpu_type_2 u16
#define __pcpu_type_4 u32
#define __pcpu_type_8 u64

#define __pcpu_cast_1(val) ((u8)(((unsigned long) val) & 0xff))
#define __pcpu_cast_2(val) ((u16)(((unsigned long) val) & 0xffff))
#define __pcpu_cast_4(val) ((u32)(((unsigned long) val) & 0xffffffff))
#define __pcpu_cast_8(val) ((u64)(val))

#define __pcpu_op1_1(op, dst) op "b " dst
#define __pcpu_op1_2(op, dst) op "w " dst
#define __pcpu_op1_4(op, dst) op "l " dst
#define __pcpu_op1_8(op, dst) op "q " dst

#define __pcpu_op2_1(op, src, dst) op "b " src ", " dst
#define __pcpu_op2_2(op, src, dst) op "w " src ", " dst
#define __pcpu_op2_4(op, src, dst) op "l " src ", " dst
#define __pcpu_op2_8(op, src, dst) op "q " src ", " dst

#define __pcpu_reg_1(mod, x) mod "q" (x)
#define __pcpu_reg_2(mod, x) mod "r" (x)
#define __pcpu_reg_4(mod, x) mod "r" (x)
#define __pcpu_reg_8(mod, x) mod "r" (x)

#define __pcpu_reg_imm_1(x) "qi" (x)
#define __pcpu_reg_imm_2(x) "ri" (x)
#define __pcpu_reg_imm_4(x) "ri" (x)
#define __pcpu_reg_imm_8(x) "re" (x)

#define percpu_to_op(size, qual, op, _var, _val)			\
do {									\
	__pcpu_type_##size pto_val__ = __pcpu_cast_##size(_val);	\
	if (0) {		                                        \
		typeof(_var) pto_tmp__;					\
		pto_tmp__ = (_val);					\
		(void)pto_tmp__;					\
	}								\
	asm qual(__pcpu_op2_##size(op, "%[val]", __percpu_arg([var]))	\
	    : [var] "+m" (_var)						\
	    : [val] __pcpu_reg_imm_##size(pto_val__));			\
} while (0)

#define percpu_unary_op(size, qual, op, _var)				\
({									\
	asm qual (__pcpu_op1_##size(op, __percpu_arg([var]))		\
	    : [var] "+m" (_var));					\
})

/*
 * Generate a percpu add to memory instruction and optimize code
 * if one is added or subtracted.
 */
#define percpu_add_op(size, qual, var, val)				\
do {									\
	const int pao_ID__ = (__builtin_constant_p(val) &&		\
			      ((val) == 1 || (val) == -1)) ?		\
				(int)(val) : 0;				\
	if (0) {							\
		typeof(var) pao_tmp__;					\
		pao_tmp__ = (val);					\
		(void)pao_tmp__;					\
	}								\
	if (pao_ID__ == 1)						\
		percpu_unary_op(size, qual, "inc", var);		\
	else if (pao_ID__ == -1)					\
		percpu_unary_op(size, qual, "dec", var);		\
	else								\
		percpu_to_op(size, qual, "add", var, val);		\
} while (0)

#define percpu_from_op(size, qual, op, _var)				\
({									\
	__pcpu_type_##size pfo_val__;					\
	asm qual (__pcpu_op2_##size(op, __percpu_arg([var]), "%[val]")	\
	    : [val] __pcpu_reg_##size("=", pfo_val__)			\
	    : [var] "m" (_var));					\
	(typeof(_var))(unsigned long) pfo_val__;			\
})

#define percpu_stable_op(size, op, _var)				\
({									\
	__pcpu_type_##size pfo_val__;					\
	asm(__pcpu_op2_##size(op, __percpu_arg(P[var]), "%[val]")	\
	    : [val] __pcpu_reg_##size("=", pfo_val__)			\
	    : [var] "p" (&(_var)));					\
	(typeof(_var))(unsigned long) pfo_val__;			\
})

/*
 * Add return operation
 */
#define percpu_add_return_op(size, qual, _var, _val)			\
({									\
	__pcpu_type_##size paro_tmp__ = __pcpu_cast_##size(_val);	\
	asm qual (__pcpu_op2_##size("xadd", "%[tmp]",			\
				     __percpu_arg([var]))		\
		  : [tmp] __pcpu_reg_##size("+", paro_tmp__),		\
		    [var] "+m" (_var)					\
		  : : "memory");					\
	(typeof(_var))(unsigned long) (paro_tmp__ + _val);		\
})

/*
 * xchg is implemented using cmpxchg without a lock prefix. xchg is
 * expensive due to the implied lock prefix.  The processor cannot prefetch
 * cachelines if xchg is used.
 */
#define percpu_xchg_op(size, qual, _var, _nval)				\
({									\
	__pcpu_type_##size pxo_old__;					\
	__pcpu_type_##size pxo_new__ = __pcpu_cast_##size(_nval);	\
	asm qual (__pcpu_op2_##size("mov", __percpu_arg([var]),		\
				    "%[oval]")				\
		  "\n1:\t"						\
		  __pcpu_op2_##size("cmpxchg", "%[nval]",		\
				    __percpu_arg([var]))		\
		  "\n\tjnz 1b"						\
		  : [oval] "=&a" (pxo_old__),				\
		    [var] "+m" (_var)					\
		  : [nval] __pcpu_reg_##size(, pxo_new__)		\
		  : "memory");						\
	(typeof(_var))(unsigned long) pxo_old__;			\
})

/*
 * cmpxchg has no such implied lock semantics as a result it is much
 * more efficient for cpu local operations.
 */
#define percpu_cmpxchg_op(size, qual, _var, _oval, _nval)		\
({									\
	__pcpu_type_##size pco_old__ = __pcpu_cast_##size(_oval);	\
	__pcpu_type_##size pco_new__ = __pcpu_cast_##size(_nval);	\
	asm qual (__pcpu_op2_##size("cmpxchg", "%[nval]",		\
				    __percpu_arg([var]))		\
		  : [oval] "+a" (pco_old__),				\
		    [var] "+m" (_var)					\
		  : [nval] __pcpu_reg_##size(, pco_new__)		\
		  : "memory");						\
	(typeof(_var))(unsigned long) pco_old__;			\
})

/*
 * this_cpu_read() makes gcc load the percpu variable every time it is
 * accessed while this_cpu_read_stable() allows the value to be cached.
 * this_cpu_read_stable() is more efficient and can be used if its value
 * is guaranteed to be valid across cpus.  The current users include
 * get_current() and get_thread_info() both of which are actually
 * per-thread variables implemented as per-cpu variables and thus
 * stable for the duration of the respective task.
 */
#define this_cpu_read_stable_1(pcp)	percpu_stable_op(1, "mov", pcp)
#define this_cpu_read_stable_2(pcp)	percpu_stable_op(2, "mov", pcp)
#define this_cpu_read_stable_4(pcp)	percpu_stable_op(4, "mov", pcp)
#define this_cpu_read_stable_8(pcp)	percpu_stable_op(8, "mov", pcp)
#define this_cpu_read_stable(pcp)	__pcpu_size_call_return(this_cpu_read_stable_, pcp)

#define raw_cpu_read_1(pcp)		percpu_from_op(1, , "mov", pcp)
#define raw_cpu_read_2(pcp)		percpu_from_op(2, , "mov", pcp)
#define raw_cpu_read_4(pcp)		percpu_from_op(4, , "mov", pcp)

#define raw_cpu_write_1(pcp, val)	percpu_to_op(1, , "mov", (pcp), val)
#define raw_cpu_write_2(pcp, val)	percpu_to_op(2, , "mov", (pcp), val)
#define raw_cpu_write_4(pcp, val)	percpu_to_op(4, , "mov", (pcp), val)
#define raw_cpu_add_1(pcp, val)		percpu_add_op(1, , (pcp), val)
#define raw_cpu_add_2(pcp, val)		percpu_add_op(2, , (pcp), val)
#define raw_cpu_add_4(pcp, val)		percpu_add_op(4, , (pcp), val)
#define raw_cpu_and_1(pcp, val)		percpu_to_op(1, , "and", (pcp), val)
#define raw_cpu_and_2(pcp, val)		percpu_to_op(2, , "and", (pcp), val)
#define raw_cpu_and_4(pcp, val)		percpu_to_op(4, , "and", (pcp), val)
#define raw_cpu_or_1(pcp, val)		percpu_to_op(1, , "or", (pcp), val)
#define raw_cpu_or_2(pcp, val)		percpu_to_op(2, , "or", (pcp), val)
#define raw_cpu_or_4(pcp, val)		percpu_to_op(4, , "or", (pcp), val)

/*
 * raw_cpu_xchg() can use a load-store since it is not required to be
 * IRQ-safe.
 */
#define raw_percpu_xchg_op(var, nval)					\
({									\
	typeof(var) pxo_ret__ = raw_cpu_read(var);			\
	raw_cpu_write(var, (nval));					\
	pxo_ret__;							\
})

#define raw_cpu_xchg_1(pcp, val)	raw_percpu_xchg_op(pcp, val)
#define raw_cpu_xchg_2(pcp, val)	raw_percpu_xchg_op(pcp, val)
#define raw_cpu_xchg_4(pcp, val)	raw_percpu_xchg_op(pcp, val)

#define this_cpu_read_1(pcp)		percpu_from_op(1, volatile, "mov", pcp)
#define this_cpu_read_2(pcp)		percpu_from_op(2, volatile, "mov", pcp)
#define this_cpu_read_4(pcp)		percpu_from_op(4, volatile, "mov", pcp)
#define this_cpu_write_1(pcp, val)	percpu_to_op(1, volatile, "mov", (pcp), val)
#define this_cpu_write_2(pcp, val)	percpu_to_op(2, volatile, "mov", (pcp), val)
#define this_cpu_write_4(pcp, val)	percpu_to_op(4, volatile, "mov", (pcp), val)
#define this_cpu_add_1(pcp, val)	percpu_add_op(1, volatile, (pcp), val)
#define this_cpu_add_2(pcp, val)	percpu_add_op(2, volatile, (pcp), val)
#define this_cpu_add_4(pcp, val)	percpu_add_op(4, volatile, (pcp), val)
#define this_cpu_and_1(pcp, val)	percpu_to_op(1, volatile, "and", (pcp), val)
#define this_cpu_and_2(pcp, val)	percpu_to_op(2, volatile, "and", (pcp), val)
#define this_cpu_and_4(pcp, val)	percpu_to_op(4, volatile, "and", (pcp), val)
#define this_cpu_or_1(pcp, val)		percpu_to_op(1, volatile, "or", (pcp), val)
#define this_cpu_or_2(pcp, val)		percpu_to_op(2, volatile, "or", (pcp), val)
#define this_cpu_or_4(pcp, val)		percpu_to_op(4, volatile, "or", (pcp), val)
#define this_cpu_xchg_1(pcp, nval)	percpu_xchg_op(1, volatile, pcp, nval)
#define this_cpu_xchg_2(pcp, nval)	percpu_xchg_op(2, volatile, pcp, nval)
#define this_cpu_xchg_4(pcp, nval)	percpu_xchg_op(4, volatile, pcp, nval)

#define raw_cpu_add_return_1(pcp, val)		percpu_add_return_op(1, , pcp, val)
#define raw_cpu_add_return_2(pcp, val)		percpu_add_return_op(2, , pcp, val)
#define raw_cpu_add_return_4(pcp, val)		percpu_add_return_op(4, , pcp, val)
#define raw_cpu_cmpxchg_1(pcp, oval, nval)	percpu_cmpxchg_op(1, , pcp, oval, nval)
#define raw_cpu_cmpxchg_2(pcp, oval, nval)	percpu_cmpxchg_op(2, , pcp, oval, nval)
#define raw_cpu_cmpxchg_4(pcp, oval, nval)	percpu_cmpxchg_op(4, , pcp, oval, nval)

#define this_cpu_add_return_1(pcp, val)		percpu_add_return_op(1, volatile, pcp, val)
#define this_cpu_add_return_2(pcp, val)		percpu_add_return_op(2, volatile, pcp, val)
#define this_cpu_add_return_4(pcp, val)		percpu_add_return_op(4, volatile, pcp, val)
#define this_cpu_cmpxchg_1(pcp, oval, nval)	percpu_cmpxchg_op(1, volatile, pcp, oval, nval)
#define this_cpu_cmpxchg_2(pcp, oval, nval)	percpu_cmpxchg_op(2, volatile, pcp, oval, nval)
#define this_cpu_cmpxchg_4(pcp, oval, nval)	percpu_cmpxchg_op(4, volatile, pcp, oval, nval)

#ifdef CONFIG_X86_CMPXCHG64
#define percpu_cmpxchg8b_double(pcp1, pcp2, o1, o2, n1, n2)		\
({									\
	bool __ret;							\
	typeof(pcp1) __o1 = (o1), __n1 = (n1);				\
	typeof(pcp2) __o2 = (o2), __n2 = (n2);				\
	asm volatile("cmpxchg8b "__percpu_arg(1)			\
		     CC_SET(z)						\
		     : CC_OUT(z) (__ret), "+m" (pcp1), "+m" (pcp2), "+a" (__o1), "+d" (__o2) \
		     : "b" (__n1), "c" (__n2));				\
	__ret;								\
})

#define raw_cpu_cmpxchg_double_4	percpu_cmpxchg8b_double
#define this_cpu_cmpxchg_double_4	percpu_cmpxchg8b_double
#endif /* CONFIG_X86_CMPXCHG64 */

/*
 * Per cpu atomic 64 bit operations are only available under 64 bit.
 * 32 bit must fall back to generic operations.
 */
#ifdef CONFIG_X86_64
#define raw_cpu_read_8(pcp)			percpu_from_op(8, , "mov", pcp)
#define raw_cpu_write_8(pcp, val)		percpu_to_op(8, , "mov", (pcp), val)
#define raw_cpu_add_8(pcp, val)			percpu_add_op(8, , (pcp), val)
#define raw_cpu_and_8(pcp, val)			percpu_to_op(8, , "and", (pcp), val)
#define raw_cpu_or_8(pcp, val)			percpu_to_op(8, , "or", (pcp), val)
#define raw_cpu_add_return_8(pcp, val)		percpu_add_return_op(8, , pcp, val)
#define raw_cpu_xchg_8(pcp, nval)		raw_percpu_xchg_op(pcp, nval)
#define raw_cpu_cmpxchg_8(pcp, oval, nval)	percpu_cmpxchg_op(8, , pcp, oval, nval)

#define this_cpu_read_8(pcp)			percpu_from_op(8, volatile, "mov", pcp)
#define this_cpu_write_8(pcp, val)		percpu_to_op(8, volatile, "mov", (pcp), val)
#define this_cpu_add_8(pcp, val)		percpu_add_op(8, volatile, (pcp), val)
#define this_cpu_and_8(pcp, val)		percpu_to_op(8, volatile, "and", (pcp), val)
#define this_cpu_or_8(pcp, val)			percpu_to_op(8, volatile, "or", (pcp), val)
#define this_cpu_add_return_8(pcp, val)		percpu_add_return_op(8, volatile, pcp, val)
#define this_cpu_xchg_8(pcp, nval)		percpu_xchg_op(8, volatile, pcp, nval)
#define this_cpu_cmpxchg_8(pcp, oval, nval)	percpu_cmpxchg_op(8, volatile, pcp, oval, nval)

/*
 * Pretty complex macro to generate cmpxchg16 instruction.  The instruction
 * is not supported on early AMD64 processors so we must be able to emulate
 * it in software.  The address used in the cmpxchg16 instruction must be
 * aligned to a 16 byte boundary.
 */
#define percpu_cmpxchg16b_double(pcp1, pcp2, o1, o2, n1, n2)		\
({									\
	bool __ret;							\
	typeof(pcp1) __o1 = (o1), __n1 = (n1);				\
	typeof(pcp2) __o2 = (o2), __n2 = (n2);				\
	alternative_io("leaq %P1,%%rsi\n\tcall this_cpu_cmpxchg16b_emu\n\t", \
		       "cmpxchg16b " __percpu_arg(1) "\n\tsetz %0\n\t",	\
		       X86_FEATURE_CX16,				\
		       ASM_OUTPUT2("=a" (__ret), "+m" (pcp1),		\
				   "+m" (pcp2), "+d" (__o2)),		\
		       "b" (__n1), "c" (__n2), "a" (__o1) : "rsi");	\
	__ret;								\
})

#define raw_cpu_cmpxchg_double_8	percpu_cmpxchg16b_double
#define this_cpu_cmpxchg_double_8	percpu_cmpxchg16b_double

#endif

static __always_inline bool x86_this_cpu_constant_test_bit(unsigned int nr,
                        const unsigned long __percpu *addr)
{
	unsigned long __percpu *a =
		(unsigned long __percpu *)addr + nr / BITS_PER_LONG;

#ifdef CONFIG_X86_64
	return ((1UL << (nr % BITS_PER_LONG)) & raw_cpu_read_8(*a)) != 0;
#else
	return ((1UL << (nr % BITS_PER_LONG)) & raw_cpu_read_4(*a)) != 0;
#endif
}

static inline bool x86_this_cpu_variable_test_bit(int nr,
                        const unsigned long __percpu *addr)
{
	bool oldbit;

	asm volatile("btl "__percpu_arg(2)",%1"
			CC_SET(c)
			: CC_OUT(c) (oldbit)
			: "m" (*(unsigned long __percpu *)addr), "Ir" (nr));

	return oldbit;
}

#define x86_this_cpu_test_bit(nr, addr)			\
	(__builtin_constant_p((nr))			\
	 ? x86_this_cpu_constant_test_bit((nr), (addr))	\
	 : x86_this_cpu_variable_test_bit((nr), (addr)))


#include <asm-generic/percpu.h>

/* We can use this directly for local CPU (faster). */
DECLARE_PER_CPU_READ_MOSTLY(unsigned long, this_cpu_off);

#endif /* !__ASSEMBLY__ */

#ifdef CONFIG_SMP

/*
 * Define the "EARLY_PER_CPU" macros.  These are used for some per_cpu
 * variables that are initialized and accessed before there are per_cpu
 * areas allocated.
 */

#define	DEFINE_EARLY_PER_CPU(_type, _name, _initvalue)			\
	DEFINE_PER_CPU(_type, _name) = _initvalue;			\
	__typeof__(_type) _name##_early_map[NR_CPUS] __initdata =	\
				{ [0 ... NR_CPUS-1] = _initvalue };	\
	__typeof__(_type) *_name##_early_ptr __refdata = _name##_early_map

#define DEFINE_EARLY_PER_CPU_READ_MOSTLY(_type, _name, _initvalue)	\
	DEFINE_PER_CPU_READ_MOSTLY(_type, _name) = _initvalue;		\
	__typeof__(_type) _name##_early_map[NR_CPUS] __initdata =	\
				{ [0 ... NR_CPUS-1] = _initvalue };	\
	__typeof__(_type) *_name##_early_ptr __refdata = _name##_early_map

#define EXPORT_EARLY_PER_CPU_SYMBOL(_name)			\
	EXPORT_PER_CPU_SYMBOL(_name)

#define DECLARE_EARLY_PER_CPU(_type, _name)			\
	DECLARE_PER_CPU(_type, _name);				\
	extern __typeof__(_type) *_name##_early_ptr;		\
	extern __typeof__(_type)  _name##_early_map[]

#define DECLARE_EARLY_PER_CPU_READ_MOSTLY(_type, _name)		\
	DECLARE_PER_CPU_READ_MOSTLY(_type, _name);		\
	extern __typeof__(_type) *_name##_early_ptr;		\
	extern __typeof__(_type)  _name##_early_map[]

#define	early_per_cpu_ptr(_name) (_name##_early_ptr)
#define	early_per_cpu_map(_name, _idx) (_name##_early_map[_idx])
#define	early_per_cpu(_name, _cpu) 				\
	*(early_per_cpu_ptr(_name) ?				\
		&early_per_cpu_ptr(_name)[_cpu] :		\
		&per_cpu(_name, _cpu))

#else	/* !CONFIG_SMP */
#define	DEFINE_EARLY_PER_CPU(_type, _name, _initvalue)		\
	DEFINE_PER_CPU(_type, _name) = _initvalue

#define DEFINE_EARLY_PER_CPU_READ_MOSTLY(_type, _name, _initvalue)	\
	DEFINE_PER_CPU_READ_MOSTLY(_type, _name) = _initvalue

#define EXPORT_EARLY_PER_CPU_SYMBOL(_name)			\
	EXPORT_PER_CPU_SYMBOL(_name)

#define DECLARE_EARLY_PER_CPU(_type, _name)			\
	DECLARE_PER_CPU(_type, _name)

#define DECLARE_EARLY_PER_CPU_READ_MOSTLY(_type, _name)		\
	DECLARE_PER_CPU_READ_MOSTLY(_type, _name)

#define	early_per_cpu(_name, _cpu) per_cpu(_name, _cpu)
#define	early_per_cpu_ptr(_name) NULL
/* no early_per_cpu_map() */

#endif	/* !CONFIG_SMP */

#endif /* _ASM_X86_PERCPU_H */
