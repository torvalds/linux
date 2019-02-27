/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PERCPU_H
#define _ASM_X86_PERCPU_H

#ifdef CONFIG_X86_64
#define __percpu_seg		gs
#define __percpu_mov_op		movq
#else
#define __percpu_seg		fs
#define __percpu_mov_op		movl
#endif

#ifdef __ASSEMBLY__

/*
 * PER_CPU finds an address of a per-cpu variable.
 *
 * Args:
 *    var - variable name
 *    reg - 32bit register
 *
 * The resulting address is stored in the "reg" argument.
 *
 * Example:
 *    PER_CPU(cpu_gdt_descr, %ebx)
 */
#ifdef CONFIG_SMP
#define PER_CPU(var, reg)						\
	__percpu_mov_op %__percpu_seg:this_cpu_off, reg;		\
	lea var(reg), reg
#define PER_CPU_VAR(var)	%__percpu_seg:var
#else /* ! SMP */
#define PER_CPU(var, reg)	__percpu_mov_op $var, reg
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
extern void __bad_percpu_size(void);

#define percpu_to_op(qual, op, var, val)		\
do {							\
	typedef typeof(var) pto_T__;			\
	if (0) {					\
		pto_T__ pto_tmp__;			\
		pto_tmp__ = (val);			\
		(void)pto_tmp__;			\
	}						\
	switch (sizeof(var)) {				\
	case 1:						\
		asm qual (op "b %1,"__percpu_arg(0)	\
		    : "+m" (var)			\
		    : "qi" ((pto_T__)(val)));		\
		break;					\
	case 2:						\
		asm qual (op "w %1,"__percpu_arg(0)	\
		    : "+m" (var)			\
		    : "ri" ((pto_T__)(val)));		\
		break;					\
	case 4:						\
		asm qual (op "l %1,"__percpu_arg(0)	\
		    : "+m" (var)			\
		    : "ri" ((pto_T__)(val)));		\
		break;					\
	case 8:						\
		asm qual (op "q %1,"__percpu_arg(0)	\
		    : "+m" (var)			\
		    : "re" ((pto_T__)(val)));		\
		break;					\
	default: __bad_percpu_size();			\
	}						\
} while (0)

/*
 * Generate a percpu add to memory instruction and optimize code
 * if one is added or subtracted.
 */
#define percpu_add_op(qual, var, val)					\
do {									\
	typedef typeof(var) pao_T__;					\
	const int pao_ID__ = (__builtin_constant_p(val) &&		\
			      ((val) == 1 || (val) == -1)) ?		\
				(int)(val) : 0;				\
	if (0) {							\
		pao_T__ pao_tmp__;					\
		pao_tmp__ = (val);					\
		(void)pao_tmp__;					\
	}								\
	switch (sizeof(var)) {						\
	case 1:								\
		if (pao_ID__ == 1)					\
			asm qual ("incb "__percpu_arg(0) : "+m" (var));	\
		else if (pao_ID__ == -1)				\
			asm qual ("decb "__percpu_arg(0) : "+m" (var));	\
		else							\
			asm qual ("addb %1, "__percpu_arg(0)		\
			    : "+m" (var)				\
			    : "qi" ((pao_T__)(val)));			\
		break;							\
	case 2:								\
		if (pao_ID__ == 1)					\
			asm qual ("incw "__percpu_arg(0) : "+m" (var));	\
		else if (pao_ID__ == -1)				\
			asm qual ("decw "__percpu_arg(0) : "+m" (var));	\
		else							\
			asm qual ("addw %1, "__percpu_arg(0)		\
			    : "+m" (var)				\
			    : "ri" ((pao_T__)(val)));			\
		break;							\
	case 4:								\
		if (pao_ID__ == 1)					\
			asm qual ("incl "__percpu_arg(0) : "+m" (var));	\
		else if (pao_ID__ == -1)				\
			asm qual ("decl "__percpu_arg(0) : "+m" (var));	\
		else							\
			asm qual ("addl %1, "__percpu_arg(0)		\
			    : "+m" (var)				\
			    : "ri" ((pao_T__)(val)));			\
		break;							\
	case 8:								\
		if (pao_ID__ == 1)					\
			asm qual ("incq "__percpu_arg(0) : "+m" (var));	\
		else if (pao_ID__ == -1)				\
			asm qual ("decq "__percpu_arg(0) : "+m" (var));	\
		else							\
			asm qual ("addq %1, "__percpu_arg(0)		\
			    : "+m" (var)				\
			    : "re" ((pao_T__)(val)));			\
		break;							\
	default: __bad_percpu_size();					\
	}								\
} while (0)

#define percpu_from_op(qual, op, var)			\
({							\
	typeof(var) pfo_ret__;				\
	switch (sizeof(var)) {				\
	case 1:						\
		asm qual (op "b "__percpu_arg(1)",%0"	\
		    : "=q" (pfo_ret__)			\
		    : "m" (var));			\
		break;					\
	case 2:						\
		asm qual (op "w "__percpu_arg(1)",%0"	\
		    : "=r" (pfo_ret__)			\
		    : "m" (var));			\
		break;					\
	case 4:						\
		asm qual (op "l "__percpu_arg(1)",%0"	\
		    : "=r" (pfo_ret__)			\
		    : "m" (var));			\
		break;					\
	case 8:						\
		asm qual (op "q "__percpu_arg(1)",%0"	\
		    : "=r" (pfo_ret__)			\
		    : "m" (var));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
	pfo_ret__;					\
})

#define percpu_stable_op(op, var)			\
({							\
	typeof(var) pfo_ret__;				\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b "__percpu_arg(P1)",%0"	\
		    : "=q" (pfo_ret__)			\
		    : "p" (&(var)));			\
		break;					\
	case 2:						\
		asm(op "w "__percpu_arg(P1)",%0"	\
		    : "=r" (pfo_ret__)			\
		    : "p" (&(var)));			\
		break;					\
	case 4:						\
		asm(op "l "__percpu_arg(P1)",%0"	\
		    : "=r" (pfo_ret__)			\
		    : "p" (&(var)));			\
		break;					\
	case 8:						\
		asm(op "q "__percpu_arg(P1)",%0"	\
		    : "=r" (pfo_ret__)			\
		    : "p" (&(var)));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
	pfo_ret__;					\
})

#define percpu_unary_op(qual, op, var)			\
({							\
	switch (sizeof(var)) {				\
	case 1:						\
		asm qual (op "b "__percpu_arg(0)	\
		    : "+m" (var));			\
		break;					\
	case 2:						\
		asm qual (op "w "__percpu_arg(0)	\
		    : "+m" (var));			\
		break;					\
	case 4:						\
		asm qual (op "l "__percpu_arg(0)	\
		    : "+m" (var));			\
		break;					\
	case 8:						\
		asm qual (op "q "__percpu_arg(0)	\
		    : "+m" (var));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
})

/*
 * Add return operation
 */
#define percpu_add_return_op(qual, var, val)				\
({									\
	typeof(var) paro_ret__ = val;					\
	switch (sizeof(var)) {						\
	case 1:								\
		asm qual ("xaddb %0, "__percpu_arg(1)			\
			    : "+q" (paro_ret__), "+m" (var)		\
			    : : "memory");				\
		break;							\
	case 2:								\
		asm qual ("xaddw %0, "__percpu_arg(1)			\
			    : "+r" (paro_ret__), "+m" (var)		\
			    : : "memory");				\
		break;							\
	case 4:								\
		asm qual ("xaddl %0, "__percpu_arg(1)			\
			    : "+r" (paro_ret__), "+m" (var)		\
			    : : "memory");				\
		break;							\
	case 8:								\
		asm qual ("xaddq %0, "__percpu_arg(1)			\
			    : "+re" (paro_ret__), "+m" (var)		\
			    : : "memory");				\
		break;							\
	default: __bad_percpu_size();					\
	}								\
	paro_ret__ += val;						\
	paro_ret__;							\
})

/*
 * xchg is implemented using cmpxchg without a lock prefix. xchg is
 * expensive due to the implied lock prefix.  The processor cannot prefetch
 * cachelines if xchg is used.
 */
#define percpu_xchg_op(qual, var, nval)					\
({									\
	typeof(var) pxo_ret__;						\
	typeof(var) pxo_new__ = (nval);					\
	switch (sizeof(var)) {						\
	case 1:								\
		asm qual ("\n\tmov "__percpu_arg(1)",%%al"		\
		    "\n1:\tcmpxchgb %2, "__percpu_arg(1)		\
		    "\n\tjnz 1b"					\
			    : "=&a" (pxo_ret__), "+m" (var)		\
			    : "q" (pxo_new__)				\
			    : "memory");				\
		break;							\
	case 2:								\
		asm qual ("\n\tmov "__percpu_arg(1)",%%ax"		\
		    "\n1:\tcmpxchgw %2, "__percpu_arg(1)		\
		    "\n\tjnz 1b"					\
			    : "=&a" (pxo_ret__), "+m" (var)		\
			    : "r" (pxo_new__)				\
			    : "memory");				\
		break;							\
	case 4:								\
		asm qual ("\n\tmov "__percpu_arg(1)",%%eax"		\
		    "\n1:\tcmpxchgl %2, "__percpu_arg(1)		\
		    "\n\tjnz 1b"					\
			    : "=&a" (pxo_ret__), "+m" (var)		\
			    : "r" (pxo_new__)				\
			    : "memory");				\
		break;							\
	case 8:								\
		asm qual ("\n\tmov "__percpu_arg(1)",%%rax"		\
		    "\n1:\tcmpxchgq %2, "__percpu_arg(1)		\
		    "\n\tjnz 1b"					\
			    : "=&a" (pxo_ret__), "+m" (var)		\
			    : "r" (pxo_new__)				\
			    : "memory");				\
		break;							\
	default: __bad_percpu_size();					\
	}								\
	pxo_ret__;							\
})

/*
 * cmpxchg has no such implied lock semantics as a result it is much
 * more efficient for cpu local operations.
 */
#define percpu_cmpxchg_op(qual, var, oval, nval)			\
({									\
	typeof(var) pco_ret__;						\
	typeof(var) pco_old__ = (oval);					\
	typeof(var) pco_new__ = (nval);					\
	switch (sizeof(var)) {						\
	case 1:								\
		asm qual ("cmpxchgb %2, "__percpu_arg(1)		\
			    : "=a" (pco_ret__), "+m" (var)		\
			    : "q" (pco_new__), "0" (pco_old__)		\
			    : "memory");				\
		break;							\
	case 2:								\
		asm qual ("cmpxchgw %2, "__percpu_arg(1)		\
			    : "=a" (pco_ret__), "+m" (var)		\
			    : "r" (pco_new__), "0" (pco_old__)		\
			    : "memory");				\
		break;							\
	case 4:								\
		asm qual ("cmpxchgl %2, "__percpu_arg(1)		\
			    : "=a" (pco_ret__), "+m" (var)		\
			    : "r" (pco_new__), "0" (pco_old__)		\
			    : "memory");				\
		break;							\
	case 8:								\
		asm qual ("cmpxchgq %2, "__percpu_arg(1)		\
			    : "=a" (pco_ret__), "+m" (var)		\
			    : "r" (pco_new__), "0" (pco_old__)		\
			    : "memory");				\
		break;							\
	default: __bad_percpu_size();					\
	}								\
	pco_ret__;							\
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
#define this_cpu_read_stable(var)	percpu_stable_op("mov", var)

#define raw_cpu_read_1(pcp)		percpu_from_op(, "mov", pcp)
#define raw_cpu_read_2(pcp)		percpu_from_op(, "mov", pcp)
#define raw_cpu_read_4(pcp)		percpu_from_op(, "mov", pcp)

#define raw_cpu_write_1(pcp, val)	percpu_to_op(, "mov", (pcp), val)
#define raw_cpu_write_2(pcp, val)	percpu_to_op(, "mov", (pcp), val)
#define raw_cpu_write_4(pcp, val)	percpu_to_op(, "mov", (pcp), val)
#define raw_cpu_add_1(pcp, val)		percpu_add_op(, (pcp), val)
#define raw_cpu_add_2(pcp, val)		percpu_add_op(, (pcp), val)
#define raw_cpu_add_4(pcp, val)		percpu_add_op(, (pcp), val)
#define raw_cpu_and_1(pcp, val)		percpu_to_op(, "and", (pcp), val)
#define raw_cpu_and_2(pcp, val)		percpu_to_op(, "and", (pcp), val)
#define raw_cpu_and_4(pcp, val)		percpu_to_op(, "and", (pcp), val)
#define raw_cpu_or_1(pcp, val)		percpu_to_op(, "or", (pcp), val)
#define raw_cpu_or_2(pcp, val)		percpu_to_op(, "or", (pcp), val)
#define raw_cpu_or_4(pcp, val)		percpu_to_op(, "or", (pcp), val)

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

#define this_cpu_read_1(pcp)		percpu_from_op(volatile, "mov", pcp)
#define this_cpu_read_2(pcp)		percpu_from_op(volatile, "mov", pcp)
#define this_cpu_read_4(pcp)		percpu_from_op(volatile, "mov", pcp)
#define this_cpu_write_1(pcp, val)	percpu_to_op(volatile, "mov", (pcp), val)
#define this_cpu_write_2(pcp, val)	percpu_to_op(volatile, "mov", (pcp), val)
#define this_cpu_write_4(pcp, val)	percpu_to_op(volatile, "mov", (pcp), val)
#define this_cpu_add_1(pcp, val)	percpu_add_op(volatile, (pcp), val)
#define this_cpu_add_2(pcp, val)	percpu_add_op(volatile, (pcp), val)
#define this_cpu_add_4(pcp, val)	percpu_add_op(volatile, (pcp), val)
#define this_cpu_and_1(pcp, val)	percpu_to_op(volatile, "and", (pcp), val)
#define this_cpu_and_2(pcp, val)	percpu_to_op(volatile, "and", (pcp), val)
#define this_cpu_and_4(pcp, val)	percpu_to_op(volatile, "and", (pcp), val)
#define this_cpu_or_1(pcp, val)		percpu_to_op(volatile, "or", (pcp), val)
#define this_cpu_or_2(pcp, val)		percpu_to_op(volatile, "or", (pcp), val)
#define this_cpu_or_4(pcp, val)		percpu_to_op(volatile, "or", (pcp), val)
#define this_cpu_xchg_1(pcp, nval)	percpu_xchg_op(volatile, pcp, nval)
#define this_cpu_xchg_2(pcp, nval)	percpu_xchg_op(volatile, pcp, nval)
#define this_cpu_xchg_4(pcp, nval)	percpu_xchg_op(volatile, pcp, nval)

#define raw_cpu_add_return_1(pcp, val)		percpu_add_return_op(, pcp, val)
#define raw_cpu_add_return_2(pcp, val)		percpu_add_return_op(, pcp, val)
#define raw_cpu_add_return_4(pcp, val)		percpu_add_return_op(, pcp, val)
#define raw_cpu_cmpxchg_1(pcp, oval, nval)	percpu_cmpxchg_op(, pcp, oval, nval)
#define raw_cpu_cmpxchg_2(pcp, oval, nval)	percpu_cmpxchg_op(, pcp, oval, nval)
#define raw_cpu_cmpxchg_4(pcp, oval, nval)	percpu_cmpxchg_op(, pcp, oval, nval)

#define this_cpu_add_return_1(pcp, val)		percpu_add_return_op(volatile, pcp, val)
#define this_cpu_add_return_2(pcp, val)		percpu_add_return_op(volatile, pcp, val)
#define this_cpu_add_return_4(pcp, val)		percpu_add_return_op(volatile, pcp, val)
#define this_cpu_cmpxchg_1(pcp, oval, nval)	percpu_cmpxchg_op(volatile, pcp, oval, nval)
#define this_cpu_cmpxchg_2(pcp, oval, nval)	percpu_cmpxchg_op(volatile, pcp, oval, nval)
#define this_cpu_cmpxchg_4(pcp, oval, nval)	percpu_cmpxchg_op(volatile, pcp, oval, nval)

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
#define raw_cpu_read_8(pcp)			percpu_from_op(, "mov", pcp)
#define raw_cpu_write_8(pcp, val)		percpu_to_op(, "mov", (pcp), val)
#define raw_cpu_add_8(pcp, val)			percpu_add_op(, (pcp), val)
#define raw_cpu_and_8(pcp, val)			percpu_to_op(, "and", (pcp), val)
#define raw_cpu_or_8(pcp, val)			percpu_to_op(, "or", (pcp), val)
#define raw_cpu_add_return_8(pcp, val)		percpu_add_return_op(, pcp, val)
#define raw_cpu_xchg_8(pcp, nval)		raw_percpu_xchg_op(pcp, nval)
#define raw_cpu_cmpxchg_8(pcp, oval, nval)	percpu_cmpxchg_op(, pcp, oval, nval)

#define this_cpu_read_8(pcp)			percpu_from_op(volatile, "mov", pcp)
#define this_cpu_write_8(pcp, val)		percpu_to_op(volatile, "mov", (pcp), val)
#define this_cpu_add_8(pcp, val)		percpu_add_op(volatile, (pcp), val)
#define this_cpu_and_8(pcp, val)		percpu_to_op(volatile, "and", (pcp), val)
#define this_cpu_or_8(pcp, val)			percpu_to_op(volatile, "or", (pcp), val)
#define this_cpu_add_return_8(pcp, val)		percpu_add_return_op(volatile, pcp, val)
#define this_cpu_xchg_8(pcp, nval)		percpu_xchg_op(volatile, pcp, nval)
#define this_cpu_cmpxchg_8(pcp, oval, nval)	percpu_cmpxchg_op(volatile, pcp, oval, nval)

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
