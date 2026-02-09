/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_PERCPU_H
#define __ASM_PERCPU_H

#include <asm/cmpxchg.h>
#include <asm/loongarch.h>

/*
 * The "address" (in fact, offset from $r21) of a per-CPU variable is close to
 * the loading address of main kernel image, but far from where the modules are
 * loaded. Tell the compiler this fact when using explicit relocs.
 */
#if defined(MODULE) && defined(CONFIG_AS_HAS_EXPLICIT_RELOCS) && defined(CONFIG_64BIT)
# if __has_attribute(model)
#  define PER_CPU_ATTRIBUTES __attribute__((model("extreme")))
# else
#  error compiler support for the model attribute is necessary when a recent assembler is used
# endif
#endif

/* Use r21 for fast access */
register unsigned long __my_cpu_offset __asm__("$r21");

static inline void set_my_cpu_offset(unsigned long off)
{
	__my_cpu_offset = off;
	csr_write(off, PERCPU_BASE_KS);
}

#define __my_cpu_offset					\
({							\
	__asm__ __volatile__("":"+r"(__my_cpu_offset));	\
	__my_cpu_offset;				\
})

#ifdef CONFIG_CPU_HAS_AMO

#define PERCPU_OP(op, asm_op, c_op)					\
static __always_inline unsigned long __percpu_##op(void *ptr,		\
			unsigned long val, int size)			\
{									\
	unsigned long ret;						\
									\
	switch (size) {							\
	case 4:								\
		__asm__ __volatile__(					\
		"am"#asm_op".w"	" %[ret], %[val], %[ptr]	\n"	\
		: [ret] "=&r" (ret), [ptr] "+ZB"(*(u32 *)ptr)		\
		: [val] "r" (val));					\
		break;							\
	case 8:								\
		__asm__ __volatile__(					\
		"am"#asm_op".d" " %[ret], %[val], %[ptr]	\n"	\
		: [ret] "=&r" (ret), [ptr] "+ZB"(*(u64 *)ptr)		\
		: [val] "r" (val));					\
		break;							\
	default:							\
		ret = 0;						\
		BUILD_BUG();						\
	}								\
									\
	return ret c_op val;						\
}

PERCPU_OP(add, add, +)
PERCPU_OP(and, and, &)
PERCPU_OP(or, or, |)
#undef PERCPU_OP

#endif

#ifdef CONFIG_64BIT

#define __pcpu_op_1(op)		op ".b "
#define __pcpu_op_2(op)		op ".h "
#define __pcpu_op_4(op)		op ".w "
#define __pcpu_op_8(op)		op ".d "

#define _percpu_read(size, _pcp)					\
({									\
	typeof(_pcp) __pcp_ret;						\
									\
	__asm__ __volatile__(						\
		__pcpu_op_##size("ldx") "%[ret], $r21, %[ptr]	\n"	\
		: [ret] "=&r"(__pcp_ret)				\
		: [ptr] "r"(&(_pcp))					\
		: "memory");						\
									\
	__pcp_ret;							\
})

#define _percpu_write(size, _pcp, _val)					\
do {									\
	__asm__ __volatile__(						\
		__pcpu_op_##size("stx") "%[val], $r21, %[ptr]	\n"	\
		:							\
		: [val] "r"(_val), [ptr] "r"(&(_pcp))			\
		: "memory");						\
} while (0)

#endif

#define __percpu_xchg __arch_xchg

/* this_cpu_cmpxchg */
#define _protect_cmpxchg_local(pcp, o, n)			\
({								\
	typeof(*raw_cpu_ptr(&(pcp))) __ret;			\
	preempt_disable_notrace();				\
	__ret = cmpxchg_local(raw_cpu_ptr(&(pcp)), o, n);	\
	preempt_enable_notrace();				\
	__ret;							\
})

#define _pcp_protect(operation, pcp, val)			\
({								\
	typeof(pcp) __retval;					\
	preempt_disable_notrace();				\
	__retval = (typeof(pcp))operation(raw_cpu_ptr(&(pcp)),	\
					  (val), sizeof(pcp));	\
	preempt_enable_notrace();				\
	__retval;						\
})

#ifdef CONFIG_CPU_HAS_AMO

#define _percpu_add(pcp, val) \
	_pcp_protect(__percpu_add, pcp, val)

#define _percpu_add_return(pcp, val) _percpu_add(pcp, val)

#define _percpu_and(pcp, val) \
	_pcp_protect(__percpu_and, pcp, val)

#define _percpu_or(pcp, val) \
	_pcp_protect(__percpu_or, pcp, val)

#define this_cpu_add_4(pcp, val) _percpu_add(pcp, val)
#define this_cpu_add_8(pcp, val) _percpu_add(pcp, val)

#define this_cpu_add_return_4(pcp, val) _percpu_add_return(pcp, val)
#define this_cpu_add_return_8(pcp, val) _percpu_add_return(pcp, val)

#define this_cpu_and_4(pcp, val) _percpu_and(pcp, val)
#define this_cpu_and_8(pcp, val) _percpu_and(pcp, val)

#define this_cpu_or_4(pcp, val) _percpu_or(pcp, val)
#define this_cpu_or_8(pcp, val) _percpu_or(pcp, val)

#endif

#ifdef CONFIG_64BIT

#define this_cpu_read_1(pcp) _percpu_read(1, pcp)
#define this_cpu_read_2(pcp) _percpu_read(2, pcp)
#define this_cpu_read_4(pcp) _percpu_read(4, pcp)
#define this_cpu_read_8(pcp) _percpu_read(8, pcp)

#define this_cpu_write_1(pcp, val) _percpu_write(1, pcp, val)
#define this_cpu_write_2(pcp, val) _percpu_write(2, pcp, val)
#define this_cpu_write_4(pcp, val) _percpu_write(4, pcp, val)
#define this_cpu_write_8(pcp, val) _percpu_write(8, pcp, val)

#endif

#define _percpu_xchg(pcp, val) ((typeof(pcp)) \
	_pcp_protect(__percpu_xchg, pcp, (unsigned long)(val)))

#define this_cpu_xchg_1(pcp, val) _percpu_xchg(pcp, val)
#define this_cpu_xchg_2(pcp, val) _percpu_xchg(pcp, val)
#define this_cpu_xchg_4(pcp, val) _percpu_xchg(pcp, val)
#define this_cpu_xchg_8(pcp, val) _percpu_xchg(pcp, val)

#define this_cpu_cmpxchg_1(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_2(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_4(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_8(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)

#include <asm-generic/percpu.h>

#endif /* __ASM_PERCPU_H */
