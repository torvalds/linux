/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <linux/preempt.h>
#include <asm/cmpxchg.h>
#include <asm/march.h>
#include <asm/asm.h>

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 */
#define __my_cpu_offset get_lowcore()->percpu_offset

#define arch_raw_cpu_ptr(_ptr)						\
({									\
	unsigned long tcp_ptr__;					\
									\
	tcp_ptr__ = (__force unsigned long)(_ptr);			\
	asm_inline volatile(						\
	ALTERNATIVE("ag		%[__ptr__],%[offzero](%%r0)\n",		\
		    "ag		%[__ptr__],%[offalt](%%r0)\n",		\
		    ALT_FEATURE(MFEATURE_LOWCORE))			\
	: [__ptr__] "+d" (tcp_ptr__)					\
	: [offzero] "i" (LC_PERCPU_OFFSET),				\
	  [offalt] "i" (LC_PERCPU_OFFSET + LOWCORE_ALT_ADDRESS),	\
	  "m" (((struct lowcore *)0)->percpu_offset)			\
	: "cc");							\
	(TYPEOF_UNQUAL(*(_ptr)) __force __kernel *)tcp_ptr__;		\
})

/*
 * We use a compare-and-swap loop since that uses less cpu cycles than
 * disabling and enabling interrupts like the generic variant would do.
 */
#define arch_this_cpu_to_op_simple(pcp, val, op)			\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ old__, new__, prev__;				\
	pcp_op_T__ *ptr__;						\
	preempt_disable_notrace();					\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	prev__ = READ_ONCE(*ptr__);					\
	do {								\
		old__ = prev__;						\
		new__ = old__ op (val);					\
		prev__ = cmpxchg(ptr__, old__, new__);			\
	} while (prev__ != old__);					\
	preempt_enable_notrace();					\
	new__;								\
})

#define this_cpu_add_1(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_2(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_return_1(pcp, val) arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_return_2(pcp, val) arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_and_1(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, &)
#define this_cpu_and_2(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, &)
#define this_cpu_or_1(pcp, val)		arch_this_cpu_to_op_simple(pcp, val, |)
#define this_cpu_or_2(pcp, val)		arch_this_cpu_to_op_simple(pcp, val, |)

/*
 * Macros to be used for percpu code section based on atomic instructions.
 *
 * Avoid the need to use preempt_disable() / preempt_enable() pairs and the
 * conditional preempt_schedule_notrace() function calls which come with
 * this. The idea is that this_cpu operations based on atomic instructions are
 * guarded with mviy instructions:
 *
 * - The first mviy instruction writes the register number of the percpu address
 *   variable and the even register number of a register pair (which encodes two
 *   registers: the even register for the percpu offset and the odd register for
 *   the percpu pointer) to lowcore. This also indicates that a percpu code
 *   section is executed.
 *
 * - The mviy instruction is followed by the lg instruction which loads the
 *   percpu offset into the even register of the pair and the la instruction
 *   which adds the percpu offset and the percpu address into the odd register
 *   of the pair (the percpu pointer register).
 *
 * - Afterwards the atomic percpu operation follows.
 *
 * - Then a second mviy instruction writes a zero to lowcore, which indicates
 *   the end of the percpu code section.
 *
 * - In case of an interrupt/exception/nmi the encoded register numbers which
 *   were written to lowcore are copied to the exception frame (pt_regs), and a
 *   zero is written to lowcore.
 *
 * - On return to the previous context it is checked if a percpu code section
 *   was executed (saved register value not zero), and if the process was
 *   migrated to a different cpu. The content of the percpu offset register
 *   (even register of the pair) is reloaded with the current cpu's percpu
 *   offset and the percpu pointer register (odd register of the pair) is
 *   recalculated so it points to the percpu variable of the new cpu.
 *
 * Inline assemblies making use of this typically have a code sequence like:
 *
 *   __PCPU_BEGIN(...) <- start of percpu code section
 *   atomic_op	       <- atomic op
 *   __PCPU_END(...)   <- end of percpu code section
 */

#define LC_ALT_ADDR	__stringify(LOWCORE_ALT_ADDRESS)

#define DEFINE_GR_NUM								\
	".macro _GR_NUM opd, gr\n"						\
	".set	\\opd,255\n"							\
	".irp rs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"			\
	"	.ifc \\gr,%%r\\rs\n"						\
	"		\\opd = \\rs\n"						\
	"	.endif\n"							\
	".endr\n"								\
	".if \\opd == 255\n"							\
	"	.error \"Illegal register number\"\n"				\
	".endif\n"								\
	".endm\n"

#define UNDEF_GR_NUM								\
	".purgem _GR_NUM\n"

#define PCPU_REG_PCP_SHIFT		0
#define PCPU_REG_PCP			GENMASK(3, 0)
#define PCPU_REG_OFF_SHIFT		4
#define PCPU_REG_OFF			GENMASK(7, 4)

#define __PCPU_MVIY(lcreg, imm)							\
	ALTERNATIVE("	mviy	" lcreg			"(%%r0)," imm "\n",	\
		    "	mviy	" lcreg "+" LC_ALT_ADDR "(%%r0)," imm "\n",	\
		    ALT_FEATURE(MFEATURE_LOWCORE))

#define __PCPU_LG(regoff, lcoff)						\
	ALTERNATIVE("	lg	" regoff ", " lcoff		    "(%%r0)\n",	\
		    "	lg	" regoff ", " lcoff "+" LC_ALT_ADDR "(%%r0)\n",	\
		    ALT_FEATURE(MFEATURE_LOWCORE))

#define __PCPU_LA(regptr, regoff, regpcp)					\
	"	la	" regptr ",0(" regoff "," regpcp ")\n"

#define __PCPU_CALC_REGVAL(regpcp, regoff)					\
	"(" regpcp " << " __stringify(PCPU_REG_PCP_SHIFT) ") |"			\
	"(" regoff " << " __stringify(PCPU_REG_OFF_SHIFT) ")"

#define __PCPU_CHECK_REGS(regpcp, regoff, regptr)				\
	".if " regoff " & 1\n"							\
	"	.error \"Percpu offset register must be even\"\n"		\
	".endif\n"								\
	".if (" regpcp " == 0) || (" regoff " == 0)\n"				\
	"	.error \"Percpu address and offset register must be non-zero\"\n"\
	".endif\n"								\
	".if " regptr " != " regoff " + 1\n"					\
	"	.error \"Percpu pointer and offset register must be a pair\"\n" \
	".endif\n"								\
	".if (" regpcp " == " regoff ") || (" regpcp " == " regptr ")\n"	\
	"	.error \"Percpu registers must be distinct\"\n"			\
	".endif\n"

#define __PCPU_BEGIN(lcreg, lcoff, regpcp, regoff, regptr)			\
	DEFINE_GR_NUM								\
	"_GR_NUM .Lregpcp, " regpcp "\n"					\
	"_GR_NUM .Lregoff, " regoff "\n"					\
	"_GR_NUM .Lregptr, " regptr "\n"					\
	UNDEF_GR_NUM								\
	__PCPU_CHECK_REGS(".Lregpcp", ".Lregoff", ".Lregptr")			\
	".set .Lregval, " __PCPU_CALC_REGVAL(".Lregpcp", ".Lregoff") "\n"	\
	__PCPU_MVIY(lcreg, ".Lregval")						\
	__PCPU_LG(regoff, lcoff)						\
	__PCPU_LA(regptr, regoff, regpcp)

#define __PCPU_END(lcreg)							\
	__PCPU_MVIY(lcreg, "0")

#ifndef MARCH_HAS_Z196_FEATURES

#define this_cpu_add_4(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_8(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_return_4(pcp, val) arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_return_8(pcp, val) arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_and_4(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, &)
#define this_cpu_and_8(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, &)
#define this_cpu_or_4(pcp, val)		arch_this_cpu_to_op_simple(pcp, val, |)
#define this_cpu_or_8(pcp, val)		arch_this_cpu_to_op_simple(pcp, val, |)

#else /* MARCH_HAS_Z196_FEATURES */

#define arch_this_cpu_add(pcp, val, op1, op2, szcast)				\
do {										\
	typedef typeof(pcp) pcp_op_T__;						\
	union register_pair rp__;						\
	pcp_op_T__ val__ = (val);						\
	pcp_op_T__ *ptr__;							\
										\
	ptr__ = PERCPU_PTR(&(pcp));						\
	if (__builtin_constant_p(val__) &&					\
	    ((szcast)val__ > -129) && ((szcast)val__ < 128)) {			\
		asm_inline volatile(						\
			__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
				      "%[pair__]","%N[pair__]")			\
			op2 "   0(%N[pair__]),%[val__]\n"			\
			__PCPU_END("%[lcreg]")					\
			: [pair__] "=&a" (rp__.pair), "+m" (*ptr__),		\
			  "=m" (((struct lowcore *)0)->percpu_register)		\
			: [val__] "i" ((szcast)val__), [ptr__] "a" (ptr__),	\
			  [lcreg] "i" (LC_PERCPU_REGISTER),			\
			  [lcoff] "i" (LC_PERCPU_OFFSET),			\
			  "m" (((struct lowcore *)0)->percpu_offset)		\
			: "cc");						\
	} else {								\
		asm_inline volatile(						\
			__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
				     "%[pair__]","%N[pair__]")			\
			op1 "   %[val__],%[val__],0(%N[pair__])\n"		\
			__PCPU_END("%[lcreg]")					\
			: [val__] "+&d" (val__), [pair__] "=&a" (rp__.pair),	\
			  "+m" (*ptr__),					\
			  "=m" (((struct lowcore *)0)->percpu_register)		\
			: [ptr__] "a" (ptr__),					\
			  [lcreg] "i" (LC_PERCPU_REGISTER),			\
			  [lcoff] "i" (LC_PERCPU_OFFSET),			\
			  "m" (((struct lowcore *)0)->percpu_offset)		\
			: "cc");						\
	}									\
} while (0)

#define this_cpu_add_4(pcp, val) arch_this_cpu_add(pcp, val, "laa", "asi", int)
#define this_cpu_add_8(pcp, val) arch_this_cpu_add(pcp, val, "laag", "agsi", long)

#define arch_this_cpu_add_return(pcp, val, op)				\
({									\
	typedef typeof(pcp) pcp_op_T__; 				\
	union register_pair rp__;					\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ old__, *ptr__;					\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			     "%[pair__]","%N[pair__]")			\
		op "	%[old__],%[val__],0(%N[pair__])\n"		\
		__PCPU_END("%[lcreg]")					\
		: [old__] "=&d" (old__), [pair__] "=&a" (rp__.pair),	\
		  "+m" (*ptr__),					\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [val__] "d" (val__), [ptr__] "a" (ptr__),		\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "cc");						\
	old__ + val__;							\
})

#define this_cpu_add_return_4(pcp, val) arch_this_cpu_add_return(pcp, val, "laa")
#define this_cpu_add_return_8(pcp, val) arch_this_cpu_add_return(pcp, val, "laag")

#define arch_this_cpu_to_op(pcp, val, op)				\
do {									\
	typedef typeof(pcp) pcp_op_T__; 				\
	union register_pair rp__;					\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ *ptr__;						\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			     "%[pair__]","%N[pair__]")			\
		op "    %[val__],%[val__],0(%N[pair__])\n"		\
		__PCPU_END("%[lcreg]")					\
		: [val__] "+&d" (val__), [pair__] "=&a" (rp__.pair),	\
		  "+m" (*ptr__),					\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [ptr__] "a" (ptr__),					\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "cc");						\
} while (0)

#define this_cpu_and_4(pcp, val)	arch_this_cpu_to_op(pcp, val, "lan")
#define this_cpu_and_8(pcp, val)	arch_this_cpu_to_op(pcp, val, "lang")
#define this_cpu_or_4(pcp, val)		arch_this_cpu_to_op(pcp, val, "lao")
#define this_cpu_or_8(pcp, val)		arch_this_cpu_to_op(pcp, val, "laog")

#endif /* MARCH_HAS_Z196_FEATURES */

#define arch_this_cpu_read(pcp, op)					\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	union register_pair rp__;					\
	unsigned long res__;						\
	pcp_op_T__ *ptr__;						\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			      "%[pair__]","%N[pair__]")			\
		op "	%[res__],0(%N[pair__])\n"			\
		__PCPU_END("%[lcreg]")					\
		: [res__] "=&d" (res__), [pair__] "=&a" (rp__.pair),	\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [ptr__] "a" (ptr__),					\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (*ptr__),						\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "cc");						\
	(pcp_op_T__)res__;						\
})

#define this_cpu_read_1(pcp) arch_this_cpu_read(pcp, "llgc")
#define this_cpu_read_2(pcp) arch_this_cpu_read(pcp, "llgh")
#define this_cpu_read_4(pcp) arch_this_cpu_read(pcp, "llgf")
#define this_cpu_read_8(pcp) arch_this_cpu_read(pcp, "lg")

#define arch_this_cpu_write(pcp, val, op)				\
do {									\
	typedef typeof(pcp) pcp_op_T__;					\
	union register_pair rp__;					\
	pcp_op_T__ *ptr__, val__ = (val);				\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			      "%[pair__]","%N[pair__]")			\
		op "    %[val__],0(%N[pair__])\n"			\
		__PCPU_END("%[lcreg]")					\
		: [pair__] "=&a" (rp__.pair), "=m" (*ptr__),		\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [val__] "d" (val__), [ptr__] "a" (ptr__),		\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "cc");						\
} while (0)

#define this_cpu_write_1(pcp, val) arch_this_cpu_write(pcp, val, "stc")
#define this_cpu_write_2(pcp, val) arch_this_cpu_write(pcp, val, "sth")
#define this_cpu_write_4(pcp, val) arch_this_cpu_write(pcp, val, "st")
#define this_cpu_write_8(pcp, val) arch_this_cpu_write(pcp, val, "stg")

#define arch_this_cpu_cmpxchg_simple(pcp, oval, nval)			\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ ret__;						\
	pcp_op_T__ *ptr__;						\
	preempt_disable_notrace();					\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	ret__ = cmpxchg(ptr__, oval, nval);				\
	preempt_enable_notrace();					\
	ret__;								\
})

#define arch_this_cpu_cmpxchg(pcp, oval, nval, op)			\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ old__ = (oval), new__ = (nval);			\
	union register_pair rp__;					\
	pcp_op_T__ *ptr__;						\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			     "%[pair__]","%N[pair__]")			\
		op "	%[old__],%[new__],0(%N[pair__])\n"		\
		__PCPU_END("%[lcreg]")					\
		: [old__] "+&d" (old__), [pair__] "=&a" (rp__.pair),	\
		  "+m" (*ptr__),					\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [new__] "d" (new__), [ptr__] "a" (ptr__),		\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "memory", "cc");					\
	old__;								\
})

#define this_cpu_cmpxchg_1(pcp, oval, nval) arch_this_cpu_cmpxchg_simple(pcp, oval, nval)
#define this_cpu_cmpxchg_2(pcp, oval, nval) arch_this_cpu_cmpxchg_simple(pcp, oval, nval)
#define this_cpu_cmpxchg_4(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval, "cs")
#define this_cpu_cmpxchg_8(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval, "csg")

#define this_cpu_cmpxchg64(pcp, o, n)	this_cpu_cmpxchg_8(pcp, o, n)

#define this_cpu_cmpxchg128(pcp, oval, nval)				\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	u128 old__ = (oval), new__ = (nval);				\
	union register_pair rp__;					\
	pcp_op_T__ *ptr__;						\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			     "%[pair__]","%N[pair__]")			\
		"	cdsg	%[old__],%[new__],0(%N[pair__])\n"	\
		__PCPU_END("%[lcreg]")					\
		: [old__] "+&d" (old__), [pair__] "=&a" (rp__.pair),	\
		  "+m" (*ptr__),					\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [new__] "d" (new__), [ptr__] "a" (ptr__),		\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "memory", "cc");					\
	old__;								\
})

#ifdef __HAVE_ASM_FLAG_OUTPUTS__

#define arch_this_cpu_try_cmpxchg(pcp, ovalp, nval, op)			\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	typeof(ovalp) ovalp__ = (ovalp);				\
	pcp_op_T__ old__ = *ovalp__;					\
	pcp_op_T__ new__ = (nval);					\
	union register_pair rp__;					\
	pcp_op_T__ *ptr__;						\
	int cc__;							\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			     "%[pair__]","%N[pair__]")			\
		op "	%[old__],%[new__],0(%N[pair__])\n"		\
		__PCPU_END("%[lcreg]")					\
		: [old__] "+&d" (old__), [pair__] "=&a" (rp__.pair),	\
		  "+m" (*ptr__), "=@cc" (cc__),				\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [new__] "d" (new__), [ptr__] "a" (ptr__),		\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "memory");						\
	if (unlikely(cc__))						\
		*ovalp__ = old__;					\
	likely(cc__ == 0);						\
})

#define this_cpu_try_cmpxchg_4(pcp, ovalp, nval) arch_this_cpu_try_cmpxchg(pcp, ovalp, nval, "cs")
#define this_cpu_try_cmpxchg_8(pcp, ovalp, nval) arch_this_cpu_try_cmpxchg(pcp, ovalp, nval, "csg")

#define this_cpu_try_cmpxchg64(pcp, ovalp, nval) this_cpu_try_cmpxchg_8(pcp, ovalp, nval)

#endif /* __HAVE_ASM_FLAG_OUTPUTS__ */

#define arch_this_cpu_xchg_simple(pcp, nval)				\
({									\
	typeof(pcp) *ptr__;						\
	typeof(pcp) ret__;						\
	preempt_disable_notrace();					\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	ret__ = xchg(ptr__, nval);					\
	preempt_enable_notrace();					\
	ret__;								\
})

#define arch_this_cpu_xchg(pcp, nval, ldop, csop)			\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ old__, new__ = (nval);				\
	union register_pair rp__;					\
	pcp_op_T__ *ptr__;						\
									\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		__PCPU_BEGIN("%[lcreg]","%[lcoff]","%[ptr__]",		\
			     "%[pair__]","%N[pair__]")			\
		"	" ldop "	%[old__],0(%N[pair__])\n"	\
		"0:	" csop "	%[old__],%[new__],0(%N[pair__])\n"\
		"	jnz	0b\n"					\
		__PCPU_END("%[lcreg]")					\
		: [old__] "=&d" (old__), [pair__] "=&a" (rp__.pair),	\
		  "+m" (*ptr__),					\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [new__] "d" (new__), [ptr__] "a" (ptr__),		\
		  [lcreg] "i" (LC_PERCPU_REGISTER),			\
		  [lcoff] "i" (LC_PERCPU_OFFSET),			\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "memory", "cc");					\
	old__;								\
})

#define this_cpu_xchg_1(pcp, nval) arch_this_cpu_xchg_simple(pcp, nval)
#define this_cpu_xchg_2(pcp, nval) arch_this_cpu_xchg_simple(pcp, nval)
#define this_cpu_xchg_4(pcp, nval) arch_this_cpu_xchg(pcp, nval, "l",  "cs")
#define this_cpu_xchg_8(pcp, nval) arch_this_cpu_xchg(pcp, nval, "lg", "csg")

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
