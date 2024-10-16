/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PERCPU_H
#define _ASM_X86_PERCPU_H

#ifdef CONFIG_X86_64
# define __percpu_seg		gs
# define __percpu_rel		(%rip)
#else
# define __percpu_seg		fs
# define __percpu_rel
#endif

#ifdef __ASSEMBLY__

#ifdef CONFIG_SMP
# define __percpu		%__percpu_seg:
#else
# define __percpu
#endif

#define PER_CPU_VAR(var)	__percpu(var)__percpu_rel

#ifdef CONFIG_X86_64_SMP
# define INIT_PER_CPU_VAR(var)  init_per_cpu__##var
#else
# define INIT_PER_CPU_VAR(var)  var
#endif

#else /* !__ASSEMBLY__: */

#include <linux/build_bug.h>
#include <linux/stringify.h>
#include <asm/asm.h>

#ifdef CONFIG_SMP

#ifdef CONFIG_CC_HAS_NAMED_AS

#ifdef __CHECKER__
# define __seg_gs		__attribute__((address_space(__seg_gs)))
# define __seg_fs		__attribute__((address_space(__seg_fs)))
#endif

#ifdef CONFIG_X86_64
# define __percpu_seg_override	__seg_gs
#else
# define __percpu_seg_override	__seg_fs
#endif

#define __percpu_prefix		""

#else /* !CONFIG_CC_HAS_NAMED_AS: */

#define __percpu_seg_override
#define __percpu_prefix		"%%"__stringify(__percpu_seg)":"

#endif /* CONFIG_CC_HAS_NAMED_AS */

#define __force_percpu_prefix	"%%"__stringify(__percpu_seg)":"
#define __my_cpu_offset		this_cpu_read(this_cpu_off)

/*
 * Compared to the generic __my_cpu_offset version, the following
 * saves one instruction and avoids clobbering a temp register.
 *
 * arch_raw_cpu_ptr should not be used in 32-bit VDSO for a 64-bit
 * kernel, because games are played with CONFIG_X86_64 there and
 * sizeof(this_cpu_off) becames 4.
 */
#ifndef BUILD_VDSO32_64
#define arch_raw_cpu_ptr(_ptr)						\
({									\
	unsigned long tcp_ptr__ = raw_cpu_read_long(this_cpu_off);	\
									\
	tcp_ptr__ += (__force unsigned long)(_ptr);			\
	(typeof(*(_ptr)) __kernel __force *)tcp_ptr__;			\
})
#else
#define arch_raw_cpu_ptr(_ptr) ({ BUILD_BUG(); (typeof(_ptr))0; })
#endif

#define PER_CPU_VAR(var)	%__percpu_seg:(var)__percpu_rel

#else /* !CONFIG_SMP: */

#define __percpu_seg_override
#define __percpu_prefix		""
#define __force_percpu_prefix	""

#define PER_CPU_VAR(var)	(var)__percpu_rel

#endif /* CONFIG_SMP */

#define __my_cpu_type(var)	typeof(var) __percpu_seg_override
#define __my_cpu_ptr(ptr)	(__my_cpu_type(*(ptr))*)(__force uintptr_t)(ptr)
#define __my_cpu_var(var)	(*__my_cpu_ptr(&(var)))
#define __percpu_arg(x)		__percpu_prefix "%" #x
#define __force_percpu_arg(x)	__force_percpu_prefix "%" #x

/*
 * Initialized pointers to per-CPU variables needed for the boot
 * processor need to use these macros to get the proper address
 * offset from __per_cpu_load on SMP.
 *
 * There also must be an entry in vmlinux_64.lds.S
 */
#define DECLARE_INIT_PER_CPU(var) \
       extern typeof(var) init_per_cpu_var(var)

#ifdef CONFIG_X86_64_SMP
# define init_per_cpu_var(var)  init_per_cpu__##var
#else
# define init_per_cpu_var(var)  var
#endif

/*
 * For arch-specific code, we can use direct single-insn ops (they
 * don't give an lvalue though).
 */

#define __pcpu_type_1		u8
#define __pcpu_type_2		u16
#define __pcpu_type_4		u32
#define __pcpu_type_8		u64

#define __pcpu_cast_1(val)	((u8)(((unsigned long) val) & 0xff))
#define __pcpu_cast_2(val)	((u16)(((unsigned long) val) & 0xffff))
#define __pcpu_cast_4(val)	((u32)(((unsigned long) val) & 0xffffffff))
#define __pcpu_cast_8(val)	((u64)(val))

#define __pcpu_op1_1(op, dst)	op "b " dst
#define __pcpu_op1_2(op, dst)	op "w " dst
#define __pcpu_op1_4(op, dst)	op "l " dst
#define __pcpu_op1_8(op, dst)	op "q " dst

#define __pcpu_op2_1(op, src, dst) op "b " src ", " dst
#define __pcpu_op2_2(op, src, dst) op "w " src ", " dst
#define __pcpu_op2_4(op, src, dst) op "l " src ", " dst
#define __pcpu_op2_8(op, src, dst) op "q " src ", " dst

#define __pcpu_reg_1(mod, x)	mod "q" (x)
#define __pcpu_reg_2(mod, x)	mod "r" (x)
#define __pcpu_reg_4(mod, x)	mod "r" (x)
#define __pcpu_reg_8(mod, x)	mod "r" (x)

#define __pcpu_reg_imm_1(x)	"qi" (x)
#define __pcpu_reg_imm_2(x)	"ri" (x)
#define __pcpu_reg_imm_4(x)	"ri" (x)
#define __pcpu_reg_imm_8(x)	"re" (x)

#ifdef CONFIG_USE_X86_SEG_SUPPORT

#define __raw_cpu_read(size, qual, pcp)					\
({									\
	*(qual __my_cpu_type(pcp) *)__my_cpu_ptr(&(pcp));		\
})

#define __raw_cpu_write(size, qual, pcp, val)				\
do {									\
	*(qual __my_cpu_type(pcp) *)__my_cpu_ptr(&(pcp)) = (val);	\
} while (0)

#define __raw_cpu_read_const(pcp)	__raw_cpu_read(, , pcp)

#else /* !CONFIG_USE_X86_SEG_SUPPORT: */

#define __raw_cpu_read(size, qual, _var)				\
({									\
	__pcpu_type_##size pfo_val__;					\
									\
	asm qual (__pcpu_op2_##size("mov", __percpu_arg([var]), "%[val]") \
	    : [val] __pcpu_reg_##size("=", pfo_val__)			\
	    : [var] "m" (__my_cpu_var(_var)));				\
									\
	(typeof(_var))(unsigned long) pfo_val__;			\
})

#define __raw_cpu_write(size, qual, _var, _val)				\
do {									\
	__pcpu_type_##size pto_val__ = __pcpu_cast_##size(_val);	\
									\
	if (0) {		                                        \
		typeof(_var) pto_tmp__;					\
		pto_tmp__ = (_val);					\
		(void)pto_tmp__;					\
	}								\
	asm qual(__pcpu_op2_##size("mov", "%[val]", __percpu_arg([var])) \
	    : [var] "=m" (__my_cpu_var(_var))				\
	    : [val] __pcpu_reg_imm_##size(pto_val__));			\
} while (0)

/*
 * The generic per-CPU infrastrucutre is not suitable for
 * reading const-qualified variables.
 */
#define __raw_cpu_read_const(pcp)	({ BUILD_BUG(); (typeof(pcp))0; })

#endif /* CONFIG_USE_X86_SEG_SUPPORT */

#define __raw_cpu_read_stable(size, _var)				\
({									\
	__pcpu_type_##size pfo_val__;					\
									\
	asm(__pcpu_op2_##size("mov", __force_percpu_arg(a[var]), "%[val]") \
	    : [val] __pcpu_reg_##size("=", pfo_val__)			\
	    : [var] "i" (&(_var)));					\
									\
	(typeof(_var))(unsigned long) pfo_val__;			\
})

#define percpu_unary_op(size, qual, op, _var)				\
({									\
	asm qual (__pcpu_op1_##size(op, __percpu_arg([var]))		\
	    : [var] "+m" (__my_cpu_var(_var)));				\
})

#define percpu_binary_op(size, qual, op, _var, _val)			\
do {									\
	__pcpu_type_##size pto_val__ = __pcpu_cast_##size(_val);	\
									\
	if (0) {		                                        \
		typeof(_var) pto_tmp__;					\
		pto_tmp__ = (_val);					\
		(void)pto_tmp__;					\
	}								\
	asm qual(__pcpu_op2_##size(op, "%[val]", __percpu_arg([var]))	\
	    : [var] "+m" (__my_cpu_var(_var))				\
	    : [val] __pcpu_reg_imm_##size(pto_val__));			\
} while (0)

/*
 * Generate a per-CPU add to memory instruction and optimize code
 * if one is added or subtracted.
 */
#define percpu_add_op(size, qual, var, val)				\
do {									\
	const int pao_ID__ =						\
		(__builtin_constant_p(val) &&				\
			((val) == 1 ||					\
			 (val) == (typeof(val))-1)) ? (int)(val) : 0;	\
									\
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
		percpu_binary_op(size, qual, "add", var, val);		\
} while (0)

/*
 * Add return operation
 */
#define percpu_add_return_op(size, qual, _var, _val)			\
({									\
	__pcpu_type_##size paro_tmp__ = __pcpu_cast_##size(_val);	\
									\
	asm qual (__pcpu_op2_##size("xadd", "%[tmp]",			\
				     __percpu_arg([var]))		\
		  : [tmp] __pcpu_reg_##size("+", paro_tmp__),		\
		    [var] "+m" (__my_cpu_var(_var))			\
		  : : "memory");					\
	(typeof(_var))(unsigned long) (paro_tmp__ + _val);		\
})

/*
 * raw_cpu_xchg() can use a load-store since
 * it is not required to be IRQ-safe.
 */
#define raw_percpu_xchg_op(_var, _nval)					\
({									\
	typeof(_var) pxo_old__ = raw_cpu_read(_var);			\
									\
	raw_cpu_write(_var, _nval);					\
									\
	pxo_old__;							\
})

/*
 * this_cpu_xchg() is implemented using CMPXCHG without a LOCK prefix.
 * XCHG is expensive due to the implied LOCK prefix. The processor
 * cannot prefetch cachelines if XCHG is used.
 */
#define this_percpu_xchg_op(_var, _nval)				\
({									\
	typeof(_var) pxo_old__ = this_cpu_read(_var);			\
									\
	do { } while (!this_cpu_try_cmpxchg(_var, &pxo_old__, _nval));	\
									\
	pxo_old__;							\
})

/*
 * CMPXCHG has no such implied lock semantics as a result it is much
 * more efficient for CPU-local operations.
 */
#define percpu_cmpxchg_op(size, qual, _var, _oval, _nval)		\
({									\
	__pcpu_type_##size pco_old__ = __pcpu_cast_##size(_oval);	\
	__pcpu_type_##size pco_new__ = __pcpu_cast_##size(_nval);	\
									\
	asm qual (__pcpu_op2_##size("cmpxchg", "%[nval]",		\
				    __percpu_arg([var]))		\
		  : [oval] "+a" (pco_old__),				\
		    [var] "+m" (__my_cpu_var(_var))			\
		  : [nval] __pcpu_reg_##size(, pco_new__)		\
		  : "memory");						\
									\
	(typeof(_var))(unsigned long) pco_old__;			\
})

#define percpu_try_cmpxchg_op(size, qual, _var, _ovalp, _nval)		\
({									\
	bool success;							\
	__pcpu_type_##size *pco_oval__ = (__pcpu_type_##size *)(_ovalp); \
	__pcpu_type_##size pco_old__ = *pco_oval__;			\
	__pcpu_type_##size pco_new__ = __pcpu_cast_##size(_nval);	\
									\
	asm qual (__pcpu_op2_##size("cmpxchg", "%[nval]",		\
				    __percpu_arg([var]))		\
		  CC_SET(z)						\
		  : CC_OUT(z) (success),				\
		    [oval] "+a" (pco_old__),				\
		    [var] "+m" (__my_cpu_var(_var))			\
		  : [nval] __pcpu_reg_##size(, pco_new__)		\
		  : "memory");						\
	if (unlikely(!success))						\
		*pco_oval__ = pco_old__;				\
									\
	likely(success);						\
})

#if defined(CONFIG_X86_32) && !defined(CONFIG_UML)

#define percpu_cmpxchg64_op(size, qual, _var, _oval, _nval)		\
({									\
	union {								\
		u64 var;						\
		struct {						\
			u32 low, high;					\
		};							\
	} old__, new__;							\
									\
	old__.var = _oval;						\
	new__.var = _nval;						\
									\
	asm qual (ALTERNATIVE("call this_cpu_cmpxchg8b_emu",		\
			      "cmpxchg8b " __percpu_arg([var]), X86_FEATURE_CX8) \
		  : [var] "+m" (__my_cpu_var(_var)),			\
		    "+a" (old__.low),					\
		    "+d" (old__.high)					\
		  : "b" (new__.low),					\
		    "c" (new__.high),					\
		    "S" (&(_var))					\
		  : "memory");						\
									\
	old__.var;							\
})

#define raw_cpu_cmpxchg64(pcp, oval, nval)		percpu_cmpxchg64_op(8,         , pcp, oval, nval)
#define this_cpu_cmpxchg64(pcp, oval, nval)		percpu_cmpxchg64_op(8, volatile, pcp, oval, nval)

#define percpu_try_cmpxchg64_op(size, qual, _var, _ovalp, _nval)	\
({									\
	bool success;							\
	u64 *_oval = (u64 *)(_ovalp);					\
	union {								\
		u64 var;						\
		struct {						\
			u32 low, high;					\
		};							\
	} old__, new__;							\
									\
	old__.var = *_oval;						\
	new__.var = _nval;						\
									\
	asm qual (ALTERNATIVE("call this_cpu_cmpxchg8b_emu",		\
			      "cmpxchg8b " __percpu_arg([var]), X86_FEATURE_CX8) \
		  CC_SET(z)						\
		  : CC_OUT(z) (success),				\
		    [var] "+m" (__my_cpu_var(_var)),			\
		    "+a" (old__.low),					\
		    "+d" (old__.high)					\
		  : "b" (new__.low),					\
		    "c" (new__.high),					\
		    "S" (&(_var))					\
		  : "memory");						\
	if (unlikely(!success))						\
		*_oval = old__.var;					\
									\
	likely(success);						\
})

#define raw_cpu_try_cmpxchg64(pcp, ovalp, nval)		percpu_try_cmpxchg64_op(8,         , pcp, ovalp, nval)
#define this_cpu_try_cmpxchg64(pcp, ovalp, nval)	percpu_try_cmpxchg64_op(8, volatile, pcp, ovalp, nval)

#endif /* defined(CONFIG_X86_32) && !defined(CONFIG_UML) */

#ifdef CONFIG_X86_64
#define raw_cpu_cmpxchg64(pcp, oval, nval)		percpu_cmpxchg_op(8,         , pcp, oval, nval);
#define this_cpu_cmpxchg64(pcp, oval, nval)		percpu_cmpxchg_op(8, volatile, pcp, oval, nval);

#define raw_cpu_try_cmpxchg64(pcp, ovalp, nval)		percpu_try_cmpxchg_op(8,         , pcp, ovalp, nval);
#define this_cpu_try_cmpxchg64(pcp, ovalp, nval)	percpu_try_cmpxchg_op(8, volatile, pcp, ovalp, nval);

#define percpu_cmpxchg128_op(size, qual, _var, _oval, _nval)		\
({									\
	union {								\
		u128 var;						\
		struct {						\
			u64 low, high;					\
		};							\
	} old__, new__;							\
									\
	old__.var = _oval;						\
	new__.var = _nval;						\
									\
	asm qual (ALTERNATIVE("call this_cpu_cmpxchg16b_emu",		\
			      "cmpxchg16b " __percpu_arg([var]), X86_FEATURE_CX16) \
		  : [var] "+m" (__my_cpu_var(_var)),			\
		    "+a" (old__.low),					\
		    "+d" (old__.high)					\
		  : "b" (new__.low),					\
		    "c" (new__.high),					\
		    "S" (&(_var))					\
		  : "memory");						\
									\
	old__.var;							\
})

#define raw_cpu_cmpxchg128(pcp, oval, nval)		percpu_cmpxchg128_op(16,         , pcp, oval, nval)
#define this_cpu_cmpxchg128(pcp, oval, nval)		percpu_cmpxchg128_op(16, volatile, pcp, oval, nval)

#define percpu_try_cmpxchg128_op(size, qual, _var, _ovalp, _nval)	\
({									\
	bool success;							\
	u128 *_oval = (u128 *)(_ovalp);					\
	union {								\
		u128 var;						\
		struct {						\
			u64 low, high;					\
		};							\
	} old__, new__;							\
									\
	old__.var = *_oval;						\
	new__.var = _nval;						\
									\
	asm qual (ALTERNATIVE("call this_cpu_cmpxchg16b_emu",		\
			      "cmpxchg16b " __percpu_arg([var]), X86_FEATURE_CX16) \
		  CC_SET(z)						\
		  : CC_OUT(z) (success),				\
		    [var] "+m" (__my_cpu_var(_var)),			\
		    "+a" (old__.low),					\
		    "+d" (old__.high)					\
		  : "b" (new__.low),					\
		    "c" (new__.high),					\
		    "S" (&(_var))					\
		  : "memory");						\
	if (unlikely(!success))						\
		*_oval = old__.var;					\
	likely(success);						\
})

#define raw_cpu_try_cmpxchg128(pcp, ovalp, nval)	percpu_try_cmpxchg128_op(16,         , pcp, ovalp, nval)
#define this_cpu_try_cmpxchg128(pcp, ovalp, nval)	percpu_try_cmpxchg128_op(16, volatile, pcp, ovalp, nval)

#endif /* CONFIG_X86_64 */

#define raw_cpu_read_1(pcp)				__raw_cpu_read(1, , pcp)
#define raw_cpu_read_2(pcp)				__raw_cpu_read(2, , pcp)
#define raw_cpu_read_4(pcp)				__raw_cpu_read(4, , pcp)
#define raw_cpu_write_1(pcp, val)			__raw_cpu_write(1, , pcp, val)
#define raw_cpu_write_2(pcp, val)			__raw_cpu_write(2, , pcp, val)
#define raw_cpu_write_4(pcp, val)			__raw_cpu_write(4, , pcp, val)

#define this_cpu_read_1(pcp)				__raw_cpu_read(1, volatile, pcp)
#define this_cpu_read_2(pcp)				__raw_cpu_read(2, volatile, pcp)
#define this_cpu_read_4(pcp)				__raw_cpu_read(4, volatile, pcp)
#define this_cpu_write_1(pcp, val)			__raw_cpu_write(1, volatile, pcp, val)
#define this_cpu_write_2(pcp, val)			__raw_cpu_write(2, volatile, pcp, val)
#define this_cpu_write_4(pcp, val)			__raw_cpu_write(4, volatile, pcp, val)

#define this_cpu_read_stable_1(pcp)			__raw_cpu_read_stable(1, pcp)
#define this_cpu_read_stable_2(pcp)			__raw_cpu_read_stable(2, pcp)
#define this_cpu_read_stable_4(pcp)			__raw_cpu_read_stable(4, pcp)

#define raw_cpu_add_1(pcp, val)				percpu_add_op(1, , (pcp), val)
#define raw_cpu_add_2(pcp, val)				percpu_add_op(2, , (pcp), val)
#define raw_cpu_add_4(pcp, val)				percpu_add_op(4, , (pcp), val)
#define raw_cpu_and_1(pcp, val)				percpu_binary_op(1, , "and", (pcp), val)
#define raw_cpu_and_2(pcp, val)				percpu_binary_op(2, , "and", (pcp), val)
#define raw_cpu_and_4(pcp, val)				percpu_binary_op(4, , "and", (pcp), val)
#define raw_cpu_or_1(pcp, val)				percpu_binary_op(1, , "or", (pcp), val)
#define raw_cpu_or_2(pcp, val)				percpu_binary_op(2, , "or", (pcp), val)
#define raw_cpu_or_4(pcp, val)				percpu_binary_op(4, , "or", (pcp), val)
#define raw_cpu_xchg_1(pcp, val)			raw_percpu_xchg_op(pcp, val)
#define raw_cpu_xchg_2(pcp, val)			raw_percpu_xchg_op(pcp, val)
#define raw_cpu_xchg_4(pcp, val)			raw_percpu_xchg_op(pcp, val)

#define this_cpu_add_1(pcp, val)			percpu_add_op(1, volatile, (pcp), val)
#define this_cpu_add_2(pcp, val)			percpu_add_op(2, volatile, (pcp), val)
#define this_cpu_add_4(pcp, val)			percpu_add_op(4, volatile, (pcp), val)
#define this_cpu_and_1(pcp, val)			percpu_binary_op(1, volatile, "and", (pcp), val)
#define this_cpu_and_2(pcp, val)			percpu_binary_op(2, volatile, "and", (pcp), val)
#define this_cpu_and_4(pcp, val)			percpu_binary_op(4, volatile, "and", (pcp), val)
#define this_cpu_or_1(pcp, val)				percpu_binary_op(1, volatile, "or", (pcp), val)
#define this_cpu_or_2(pcp, val)				percpu_binary_op(2, volatile, "or", (pcp), val)
#define this_cpu_or_4(pcp, val)				percpu_binary_op(4, volatile, "or", (pcp), val)
#define this_cpu_xchg_1(pcp, nval)			this_percpu_xchg_op(pcp, nval)
#define this_cpu_xchg_2(pcp, nval)			this_percpu_xchg_op(pcp, nval)
#define this_cpu_xchg_4(pcp, nval)			this_percpu_xchg_op(pcp, nval)

#define raw_cpu_add_return_1(pcp, val)			percpu_add_return_op(1, , pcp, val)
#define raw_cpu_add_return_2(pcp, val)			percpu_add_return_op(2, , pcp, val)
#define raw_cpu_add_return_4(pcp, val)			percpu_add_return_op(4, , pcp, val)
#define raw_cpu_cmpxchg_1(pcp, oval, nval)		percpu_cmpxchg_op(1, , pcp, oval, nval)
#define raw_cpu_cmpxchg_2(pcp, oval, nval)		percpu_cmpxchg_op(2, , pcp, oval, nval)
#define raw_cpu_cmpxchg_4(pcp, oval, nval)		percpu_cmpxchg_op(4, , pcp, oval, nval)
#define raw_cpu_try_cmpxchg_1(pcp, ovalp, nval)		percpu_try_cmpxchg_op(1, , pcp, ovalp, nval)
#define raw_cpu_try_cmpxchg_2(pcp, ovalp, nval)		percpu_try_cmpxchg_op(2, , pcp, ovalp, nval)
#define raw_cpu_try_cmpxchg_4(pcp, ovalp, nval)		percpu_try_cmpxchg_op(4, , pcp, ovalp, nval)

#define this_cpu_add_return_1(pcp, val)			percpu_add_return_op(1, volatile, pcp, val)
#define this_cpu_add_return_2(pcp, val)			percpu_add_return_op(2, volatile, pcp, val)
#define this_cpu_add_return_4(pcp, val)			percpu_add_return_op(4, volatile, pcp, val)
#define this_cpu_cmpxchg_1(pcp, oval, nval)		percpu_cmpxchg_op(1, volatile, pcp, oval, nval)
#define this_cpu_cmpxchg_2(pcp, oval, nval)		percpu_cmpxchg_op(2, volatile, pcp, oval, nval)
#define this_cpu_cmpxchg_4(pcp, oval, nval)		percpu_cmpxchg_op(4, volatile, pcp, oval, nval)
#define this_cpu_try_cmpxchg_1(pcp, ovalp, nval)	percpu_try_cmpxchg_op(1, volatile, pcp, ovalp, nval)
#define this_cpu_try_cmpxchg_2(pcp, ovalp, nval)	percpu_try_cmpxchg_op(2, volatile, pcp, ovalp, nval)
#define this_cpu_try_cmpxchg_4(pcp, ovalp, nval)	percpu_try_cmpxchg_op(4, volatile, pcp, ovalp, nval)

/*
 * Per-CPU atomic 64-bit operations are only available under 64-bit kernels.
 * 32-bit kernels must fall back to generic operations.
 */
#ifdef CONFIG_X86_64

#define raw_cpu_read_8(pcp)				__raw_cpu_read(8, , pcp)
#define raw_cpu_write_8(pcp, val)			__raw_cpu_write(8, , pcp, val)

#define this_cpu_read_8(pcp)				__raw_cpu_read(8, volatile, pcp)
#define this_cpu_write_8(pcp, val)			__raw_cpu_write(8, volatile, pcp, val)

#define this_cpu_read_stable_8(pcp)			__raw_cpu_read_stable(8, pcp)

#define raw_cpu_add_8(pcp, val)				percpu_add_op(8, , (pcp), val)
#define raw_cpu_and_8(pcp, val)				percpu_binary_op(8, , "and", (pcp), val)
#define raw_cpu_or_8(pcp, val)				percpu_binary_op(8, , "or", (pcp), val)
#define raw_cpu_add_return_8(pcp, val)			percpu_add_return_op(8, , pcp, val)
#define raw_cpu_xchg_8(pcp, nval)			raw_percpu_xchg_op(pcp, nval)
#define raw_cpu_cmpxchg_8(pcp, oval, nval)		percpu_cmpxchg_op(8, , pcp, oval, nval)
#define raw_cpu_try_cmpxchg_8(pcp, ovalp, nval)		percpu_try_cmpxchg_op(8, , pcp, ovalp, nval)

#define this_cpu_add_8(pcp, val)			percpu_add_op(8, volatile, (pcp), val)
#define this_cpu_and_8(pcp, val)			percpu_binary_op(8, volatile, "and", (pcp), val)
#define this_cpu_or_8(pcp, val)				percpu_binary_op(8, volatile, "or", (pcp), val)
#define this_cpu_add_return_8(pcp, val)			percpu_add_return_op(8, volatile, pcp, val)
#define this_cpu_xchg_8(pcp, nval)			this_percpu_xchg_op(pcp, nval)
#define this_cpu_cmpxchg_8(pcp, oval, nval)		percpu_cmpxchg_op(8, volatile, pcp, oval, nval)
#define this_cpu_try_cmpxchg_8(pcp, ovalp, nval)	percpu_try_cmpxchg_op(8, volatile, pcp, ovalp, nval)

#define raw_cpu_read_long(pcp)				raw_cpu_read_8(pcp)

#else /* !CONFIG_X86_64: */

/* There is no generic 64-bit read stable operation for 32-bit targets. */
#define this_cpu_read_stable_8(pcp)			({ BUILD_BUG(); (typeof(pcp))0; })

#define raw_cpu_read_long(pcp)				raw_cpu_read_4(pcp)

#endif /* CONFIG_X86_64 */

#define this_cpu_read_const(pcp)			__raw_cpu_read_const(pcp)

/*
 * this_cpu_read() makes the compiler load the per-CPU variable every time
 * it is accessed while this_cpu_read_stable() allows the value to be cached.
 * this_cpu_read_stable() is more efficient and can be used if its value
 * is guaranteed to be valid across CPUs.  The current users include
 * pcpu_hot.current_task and pcpu_hot.top_of_stack, both of which are
 * actually per-thread variables implemented as per-CPU variables and
 * thus stable for the duration of the respective task.
 */
#define this_cpu_read_stable(pcp)			__pcpu_size_call_return(this_cpu_read_stable_, pcp)

#define x86_this_cpu_constant_test_bit(_nr, _var)			\
({									\
	unsigned long __percpu *addr__ =				\
		(unsigned long __percpu *)&(_var) + ((_nr) / BITS_PER_LONG); \
									\
	!!((1UL << ((_nr) % BITS_PER_LONG)) & raw_cpu_read(*addr__));	\
})

#define x86_this_cpu_variable_test_bit(_nr, _var)			\
({									\
	bool oldbit;							\
									\
	asm volatile("btl %[nr], " __percpu_arg([var])			\
		     CC_SET(c)						\
		     : CC_OUT(c) (oldbit)				\
		     : [var] "m" (__my_cpu_var(_var)),			\
		       [nr] "rI" (_nr));				\
	oldbit;								\
})

#define x86_this_cpu_test_bit(_nr, _var)				\
	(__builtin_constant_p(_nr)					\
	 ? x86_this_cpu_constant_test_bit(_nr, _var)			\
	 : x86_this_cpu_variable_test_bit(_nr, _var))


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

#define EXPORT_EARLY_PER_CPU_SYMBOL(_name)				\
	EXPORT_PER_CPU_SYMBOL(_name)

#define DECLARE_EARLY_PER_CPU(_type, _name)				\
	DECLARE_PER_CPU(_type, _name);					\
	extern __typeof__(_type) *_name##_early_ptr;			\
	extern __typeof__(_type)  _name##_early_map[]

#define DECLARE_EARLY_PER_CPU_READ_MOSTLY(_type, _name)			\
	DECLARE_PER_CPU_READ_MOSTLY(_type, _name);			\
	extern __typeof__(_type) *_name##_early_ptr;			\
	extern __typeof__(_type)  _name##_early_map[]

#define	early_per_cpu_ptr(_name)			(_name##_early_ptr)
#define	early_per_cpu_map(_name, _idx)			(_name##_early_map[_idx])

#define	early_per_cpu(_name, _cpu)					\
	*(early_per_cpu_ptr(_name) ?					\
		&early_per_cpu_ptr(_name)[_cpu] :			\
		&per_cpu(_name, _cpu))

#else /* !CONFIG_SMP: */
#define	DEFINE_EARLY_PER_CPU(_type, _name, _initvalue)			\
	DEFINE_PER_CPU(_type, _name) = _initvalue

#define DEFINE_EARLY_PER_CPU_READ_MOSTLY(_type, _name, _initvalue)	\
	DEFINE_PER_CPU_READ_MOSTLY(_type, _name) = _initvalue

#define EXPORT_EARLY_PER_CPU_SYMBOL(_name)				\
	EXPORT_PER_CPU_SYMBOL(_name)

#define DECLARE_EARLY_PER_CPU(_type, _name)				\
	DECLARE_PER_CPU(_type, _name)

#define DECLARE_EARLY_PER_CPU_READ_MOSTLY(_type, _name)			\
	DECLARE_PER_CPU_READ_MOSTLY(_type, _name)

#define	early_per_cpu(_name, _cpu)			per_cpu(_name, _cpu)
#define	early_per_cpu_ptr(_name)			NULL
/* no early_per_cpu_map() */

#endif /* !CONFIG_SMP */

#endif /* _ASM_X86_PERCPU_H */
