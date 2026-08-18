/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <linux/preempt.h>
#include <asm/cmpxchg.h>
#include <asm/march.h>

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 */
#define __my_cpu_offset get_lowcore()->percpu_offset

#define arch_raw_cpu_ptr(_ptr)						\
({									\
	unsigned long lc_percpu, tcp_ptr__;				\
									\
	tcp_ptr__ = (__force unsigned long)(_ptr);			\
	lc_percpu = offsetof(struct lowcore, percpu_offset);		\
	asm_inline volatile(						\
	ALTERNATIVE("ag		%[__ptr__],%[offzero](%%r0)\n",		\
		    "ag		%[__ptr__],%[offalt](%%r0)\n",		\
		    ALT_FEATURE(MFEATURE_LOWCORE))			\
	: [__ptr__] "+d" (tcp_ptr__)					\
	: [offzero] "i" (lc_percpu),					\
	  [offalt] "i" (lc_percpu + LOWCORE_ALT_ADDRESS),		\
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
 * Avoid the need to use preempt_disable() / preempt_disable() pairs and the
 * conditional preempt_schedule_notrace() function calls which come with
 * this. The idea is that this_cpu operations based on atomic instructions are
 * guarded with mviy instructions:
 *
 * - The first mviy instruction writes the register number, which contains the
 *   percpu address variable to lowcore. This also indicates that a percpu
 *   code section is executed.
 *
 * - The first mviy instruction following the mviy instruction must be the ag
 *   instruction which adds the percpu offset to the percpu address register.
 *
 * - Afterwards the atomic percpu operation follows.
 *
 * - Then a second mviy instruction writes a zero to lowcore, which indicates
 *   the end of the percpu code section.
 *
 * - In case of an interrupt/exception/nmi the register number which was
 *   written to lowcore is copied to the exception frame (pt_regs), and a zero
 *   is written to lowcore.
 *
 * - On return to the previous context it is checked if a percpu code section
 *   was executed (saved register number not zero), and if the process was
 *   migrated to a different cpu. If the percpu offset was already added to
 *   the percpu address register (instruction address does _not_ point to the
 *   ag instruction) the content of the percpu address register is adjusted so
 *   it points to percpu variable of the new cpu.
 *
 * Inline assemblies making use of this typically have a code sequence like:
 *
 *   MVIY_PERCPU(...) <- start of percpu code section
 *   AG_ALT(...)      <- add percpu offset; must be the second instruction
 *   atomic_op	      <- atomic op
 *   MVIY_ALT(...)    <- end of percpu code section
 */

#define MVIY_PERCPU(disp, dispalt, reg)						\
	".macro GEN_MVIY disp reg\n"						\
	".irp	rs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"			\
	"	.ifc \\reg,%%r\\rs\n"						\
	"	mviy	\\disp(%%r0),\\rs\n"					\
	"	.endif\n"							\
	".endr\n"								\
	".endm\n"								\
	ALTERNATIVE("GEN_MVIY " __stringify(disp)    " " __stringify(reg) "\n",	\
		    "GEN_MVIY " __stringify(dispalt) " " __stringify(reg) "\n",	\
		    ALT_FEATURE(MFEATURE_LOWCORE))				\
	".purgem GEN_MVIY\n"

#define MVIY_ALT(disp, dispalt)							\
	ALTERNATIVE("	mviy	" disp	  "(%%r0),0\n",				\
		    "	mviy	" dispalt "(%%r0),0\n",				\
		    ALT_FEATURE(MFEATURE_LOWCORE))

#define AG_ALT(disp, dispalt, reg)						\
	ALTERNATIVE("	ag	" reg ", " disp	   "(%%r0)\n",			\
		    "	ag	" reg ", " dispalt "(%%r0)\n",			\
		    ALT_FEATURE(MFEATURE_LOWCORE))

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
	unsigned long lc_pcpr, lc_pcpo;						\
	typedef typeof(pcp) pcp_op_T__;						\
	pcp_op_T__ val__ = (val);						\
	pcp_op_T__ old__, *ptr__;						\
										\
	lc_pcpr = offsetof(struct lowcore, percpu_register);			\
	lc_pcpo = offsetof(struct lowcore, percpu_offset);			\
	ptr__ = PERCPU_PTR(&(pcp));						\
	if (__builtin_constant_p(val__) &&					\
	    ((szcast)val__ > -129) && ((szcast)val__ < 128)) {			\
		asm volatile(							\
			MVIY_PERCPU("%[disppcpr]", "%[dispaltpcpr]", "%[ptr__]")\
			AG_ALT("%[disppcpo]", "%[dispaltpcpo]", "%[ptr__]")	\
			op2 "   0(%[ptr__]),%[val__]\n"				\
			MVIY_ALT("%[disppcpr]", "%[dispaltpcpr]")		\
			: [ptr__] "+&a" (ptr__), "+m" (*ptr__),			\
			  "=m" (((struct lowcore *)0)->percpu_register)		\
			: [val__] "i" ((szcast)val__),				\
			  [disppcpr] "i" (lc_pcpr),				\
			  [disppcpo] "i" (lc_pcpo),				\
			  [dispaltpcpr] "i" (lc_pcpr + LOWCORE_ALT_ADDRESS),	\
			  [dispaltpcpo] "i" (lc_pcpo + LOWCORE_ALT_ADDRESS),	\
			  "m" (((struct lowcore *)0)->percpu_offset)		\
			: "cc");						\
	} else {								\
		asm volatile(							\
			MVIY_PERCPU("%[disppcpr]", "%[dispaltpcpr]", "%[ptr__]")\
			AG_ALT("%[disppcpo]", "%[dispaltpcpo]", "%[ptr__]")	\
			op1 "   %[old__],%[val__],0(%[ptr__])\n"		\
			MVIY_ALT("%[disppcpr]", "%[dispaltpcpr]")		\
			: [old__] "=&d" (old__),				\
			  [ptr__] "+&a" (ptr__),  "+m" (*ptr__),		\
			  "=m" (((struct lowcore *)0)->percpu_register)		\
			: [val__] "d" (val__),					\
			  [disppcpr] "i" (lc_pcpr),				\
			  [disppcpo] "i" (lc_pcpo),				\
			  [dispaltpcpr] "i" (lc_pcpr + LOWCORE_ALT_ADDRESS),	\
			  [dispaltpcpo] "i" (lc_pcpo + LOWCORE_ALT_ADDRESS),	\
			  "m" (((struct lowcore *)0)->percpu_offset)		\
			: "cc");						\
	}									\
} while (0)

#define this_cpu_add_4(pcp, val) arch_this_cpu_add(pcp, val, "laa", "asi", int)
#define this_cpu_add_8(pcp, val) arch_this_cpu_add(pcp, val, "laag", "agsi", long)

#define arch_this_cpu_add_return(pcp, val, op)				\
({									\
	unsigned long lc_pcpr, lc_pcpo;					\
	typedef typeof(pcp) pcp_op_T__; 				\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ old__, *ptr__;					\
									\
	lc_pcpr = offsetof(struct lowcore, percpu_register);		\
	lc_pcpo = offsetof(struct lowcore, percpu_offset);		\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		MVIY_PERCPU("%[disppcpr]", "%[dispaltpcpr]", "%[ptr__]")\
		AG_ALT("%[disppcpo]", "%[dispaltpcpo]", "%[ptr__]")	\
		op "	%[old__],%[val__],0(%[ptr__])\n"		\
		MVIY_ALT("%[disppcpr]", "%[dispaltpcpr]")		\
		: [old__] "=&d" (old__),				\
		  [ptr__] "+&a" (ptr__), "+m" (*ptr__),			\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [val__] "d" (val__),					\
		  [disppcpr] "i" (lc_pcpr),				\
		  [disppcpo] "i" (lc_pcpo),				\
		  [dispaltpcpr] "i" (lc_pcpr + LOWCORE_ALT_ADDRESS),	\
		  [dispaltpcpo] "i" (lc_pcpo + LOWCORE_ALT_ADDRESS),	\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "cc");						\
	old__ + val__;							\
})

#define this_cpu_add_return_4(pcp, val) arch_this_cpu_add_return(pcp, val, "laa")
#define this_cpu_add_return_8(pcp, val) arch_this_cpu_add_return(pcp, val, "laag")

#define arch_this_cpu_to_op(pcp, val, op)				\
do {									\
	unsigned long lc_pcpr, lc_pcpo;					\
	typedef typeof(pcp) pcp_op_T__; 				\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ old__, *ptr__;					\
									\
	lc_pcpr = offsetof(struct lowcore, percpu_register);		\
	lc_pcpo = offsetof(struct lowcore, percpu_offset);		\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		MVIY_PERCPU("%[disppcpr]", "%[dispaltpcpr]", "%[ptr__]")\
		AG_ALT("%[disppcpo]", "%[dispaltpcpo]", "%[ptr__]")	\
		op "    %[old__],%[val__],0(%[ptr__])\n"		\
		MVIY_ALT("%[disppcpr]", "%[dispaltpcpr]")		\
		: [old__] "=&d" (old__),				\
		  [ptr__] "+&a" (ptr__), "+m" (*ptr__),			\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [val__] "d" (val__),					\
		  [disppcpr] "i" (lc_pcpr),				\
		  [disppcpo] "i" (lc_pcpo),				\
		  [dispaltpcpr] "i" (lc_pcpr + LOWCORE_ALT_ADDRESS),	\
		  [dispaltpcpo] "i" (lc_pcpo + LOWCORE_ALT_ADDRESS),	\
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
	unsigned long lc_pcpr, lc_pcpo, res__;				\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ *ptr__;						\
									\
	lc_pcpr = offsetof(struct lowcore, percpu_register);		\
	lc_pcpo = offsetof(struct lowcore, percpu_offset);		\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		MVIY_PERCPU("%[disppcpr]", "%[dispaltpcpr]", "%[ptr__]")\
		AG_ALT("%[disppcpo]", "%[dispaltpcpo]", "%[ptr__]")	\
		op "	%[res__],0(%[ptr__])\n"				\
		MVIY_ALT("%[disppcpr]", "%[dispaltpcpr]")		\
		: [res__] "=&d" (res__), [ptr__] "+&a" (ptr__),		\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [disppcpr] "i" (lc_pcpr),				\
		  [disppcpo] "i" (lc_pcpo),				\
		  [dispaltpcpr] "i" (lc_pcpr + LOWCORE_ALT_ADDRESS),	\
		  [dispaltpcpo] "i" (lc_pcpo + LOWCORE_ALT_ADDRESS),	\
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
	unsigned long lc_pcpr, lc_pcpo;					\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ *ptr__, val__ = (val);				\
									\
	lc_pcpr = offsetof(struct lowcore, percpu_register);		\
	lc_pcpo = offsetof(struct lowcore, percpu_offset);		\
	ptr__ = PERCPU_PTR(&(pcp));					\
	asm_inline volatile(						\
		MVIY_PERCPU("%[disppcpr]", "%[dispaltpcpr]", "%[ptr__]")\
		AG_ALT("%[disppcpo]", "%[dispaltpcpo]", "%[ptr__]")	\
		op "    %[val__],0(%[ptr__])\n"				\
		MVIY_ALT("%[disppcpr]", "%[dispaltpcpr]")		\
		: [ptr__] "+&a" (ptr__), "=m" (*ptr__),			\
		  "=m" (((struct lowcore *)0)->percpu_register)		\
		: [val__] "d" (val__),					\
		  [disppcpr] "i" (lc_pcpr),				\
		  [disppcpo] "i" (lc_pcpo),				\
		  [dispaltpcpr] "i" (lc_pcpr + LOWCORE_ALT_ADDRESS),	\
		  [dispaltpcpo] "i" (lc_pcpo + LOWCORE_ALT_ADDRESS),	\
		  "m" (((struct lowcore *)0)->percpu_offset)		\
		: "cc");						\
} while (0)

#define this_cpu_write_1(pcp, val) arch_this_cpu_write(pcp, val, "stc")
#define this_cpu_write_2(pcp, val) arch_this_cpu_write(pcp, val, "sth")
#define this_cpu_write_4(pcp, val) arch_this_cpu_write(pcp, val, "st")
#define this_cpu_write_8(pcp, val) arch_this_cpu_write(pcp, val, "stg")

#define arch_this_cpu_cmpxchg(pcp, oval, nval)				\
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

#define this_cpu_cmpxchg_1(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)
#define this_cpu_cmpxchg_2(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)
#define this_cpu_cmpxchg_4(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)
#define this_cpu_cmpxchg_8(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)

#define this_cpu_cmpxchg64(pcp, o, n)	this_cpu_cmpxchg_8(pcp, o, n)

#define this_cpu_cmpxchg128(pcp, oval, nval)				\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	u128 old__, new__, ret__;					\
	pcp_op_T__ *ptr__;						\
	old__ = oval;							\
	new__ = nval;							\
	preempt_disable_notrace();					\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	ret__ = cmpxchg128((void *)ptr__, old__, new__);		\
	preempt_enable_notrace();					\
	ret__;								\
})

#define arch_this_cpu_xchg(pcp, nval)					\
({									\
	typeof(pcp) *ptr__;						\
	typeof(pcp) ret__;						\
	preempt_disable_notrace();					\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	ret__ = xchg(ptr__, nval);					\
	preempt_enable_notrace();					\
	ret__;								\
})

#define this_cpu_xchg_1(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_2(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_4(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_8(pcp, nval) arch_this_cpu_xchg(pcp, nval)

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
