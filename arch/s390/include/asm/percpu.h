#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <linux/preempt.h>
#include <asm/cmpxchg.h>

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 */
#define __my_cpu_offset S390_lowcore.percpu_offset

#ifdef CONFIG_64BIT

/*
 * For 64 bit module code, the module may be more than 4G above the
 * per cpu area, use weak definitions to force the compiler to
 * generate external references.
 */
#if defined(CONFIG_SMP) && defined(MODULE)
#define ARCH_NEEDS_WEAK_PER_CPU
#endif

/*
 * We use a compare-and-swap loop since that uses less cpu cycles than
 * disabling and enabling interrupts like the generic variant would do.
 */
#define arch_this_cpu_to_op_simple(pcp, val, op)			\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ old__, new__, prev__;				\
	pcp_op_T__ *ptr__;						\
	preempt_disable();						\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	prev__ = *ptr__;						\
	do {								\
		old__ = prev__;						\
		new__ = old__ op (val);					\
		prev__ = cmpxchg(ptr__, old__, new__);			\
	} while (prev__ != old__);					\
	preempt_enable();						\
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

#ifndef CONFIG_HAVE_MARCH_Z196_FEATURES

#define this_cpu_add_4(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_8(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_return_4(pcp, val) arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_add_return_8(pcp, val) arch_this_cpu_to_op_simple(pcp, val, +)
#define this_cpu_and_4(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, &)
#define this_cpu_and_8(pcp, val)	arch_this_cpu_to_op_simple(pcp, val, &)
#define this_cpu_or_4(pcp, val)		arch_this_cpu_to_op_simple(pcp, val, |)
#define this_cpu_or_8(pcp, val)		arch_this_cpu_to_op_simple(pcp, val, |)

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define arch_this_cpu_add(pcp, val, op1, op2, szcast)			\
{									\
	typedef typeof(pcp) pcp_op_T__; 				\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ old__, *ptr__;					\
	preempt_disable();						\
	ptr__ = raw_cpu_ptr(&(pcp)); 				\
	if (__builtin_constant_p(val__) &&				\
	    ((szcast)val__ > -129) && ((szcast)val__ < 128)) {		\
		asm volatile(						\
			op2 "   %[ptr__],%[val__]\n"			\
			: [ptr__] "+Q" (*ptr__) 			\
			: [val__] "i" ((szcast)val__)			\
			: "cc");					\
	} else {							\
		asm volatile(						\
			op1 "   %[old__],%[val__],%[ptr__]\n"		\
			: [old__] "=d" (old__), [ptr__] "+Q" (*ptr__)	\
			: [val__] "d" (val__)				\
			: "cc");					\
	}								\
	preempt_enable();						\
}

#define this_cpu_add_4(pcp, val) arch_this_cpu_add(pcp, val, "laa", "asi", int)
#define this_cpu_add_8(pcp, val) arch_this_cpu_add(pcp, val, "laag", "agsi", long)

#define arch_this_cpu_add_return(pcp, val, op)				\
({									\
	typedef typeof(pcp) pcp_op_T__; 				\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ old__, *ptr__;					\
	preempt_disable();						\
	ptr__ = raw_cpu_ptr(&(pcp));	 				\
	asm volatile(							\
		op "    %[old__],%[val__],%[ptr__]\n"			\
		: [old__] "=d" (old__), [ptr__] "+Q" (*ptr__)		\
		: [val__] "d" (val__)					\
		: "cc");						\
	preempt_enable();						\
	old__ + val__;							\
})

#define this_cpu_add_return_4(pcp, val) arch_this_cpu_add_return(pcp, val, "laa")
#define this_cpu_add_return_8(pcp, val) arch_this_cpu_add_return(pcp, val, "laag")

#define arch_this_cpu_to_op(pcp, val, op)				\
{									\
	typedef typeof(pcp) pcp_op_T__; 				\
	pcp_op_T__ val__ = (val);					\
	pcp_op_T__ old__, *ptr__;					\
	preempt_disable();						\
	ptr__ = raw_cpu_ptr(&(pcp));	 				\
	asm volatile(							\
		op "    %[old__],%[val__],%[ptr__]\n"			\
		: [old__] "=d" (old__), [ptr__] "+Q" (*ptr__)		\
		: [val__] "d" (val__)					\
		: "cc");						\
	preempt_enable();						\
}

#define this_cpu_and_4(pcp, val)	arch_this_cpu_to_op(pcp, val, "lan")
#define this_cpu_and_8(pcp, val)	arch_this_cpu_to_op(pcp, val, "lang")
#define this_cpu_or_4(pcp, val)		arch_this_cpu_to_op(pcp, val, "lao")
#define this_cpu_or_8(pcp, val)		arch_this_cpu_to_op(pcp, val, "laog")

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define arch_this_cpu_cmpxchg(pcp, oval, nval)				\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ ret__;						\
	pcp_op_T__ *ptr__;						\
	preempt_disable();						\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	ret__ = cmpxchg(ptr__, oval, nval);				\
	preempt_enable();						\
	ret__;								\
})

#define this_cpu_cmpxchg_1(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)
#define this_cpu_cmpxchg_2(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)
#define this_cpu_cmpxchg_4(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)
#define this_cpu_cmpxchg_8(pcp, oval, nval) arch_this_cpu_cmpxchg(pcp, oval, nval)

#define arch_this_cpu_xchg(pcp, nval)					\
({									\
	typeof(pcp) *ptr__;						\
	typeof(pcp) ret__;						\
	preempt_disable();						\
	ptr__ = raw_cpu_ptr(&(pcp));					\
	ret__ = xchg(ptr__, nval);					\
	preempt_enable();						\
	ret__;								\
})

#define this_cpu_xchg_1(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_2(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_4(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_8(pcp, nval) arch_this_cpu_xchg(pcp, nval)

#define arch_this_cpu_cmpxchg_double(pcp1, pcp2, o1, o2, n1, n2)	\
({									\
	typeof(pcp1) o1__ = (o1), n1__ = (n1);				\
	typeof(pcp2) o2__ = (o2), n2__ = (n2);				\
	typeof(pcp1) *p1__;						\
	typeof(pcp2) *p2__;						\
	int ret__;							\
	preempt_disable();						\
	p1__ = raw_cpu_ptr(&(pcp1));					\
	p2__ = raw_cpu_ptr(&(pcp2));					\
	ret__ = __cmpxchg_double(p1__, p2__, o1__, o2__, n1__, n2__);	\
	preempt_enable();						\
	ret__;								\
})

#define this_cpu_cmpxchg_double_4 arch_this_cpu_cmpxchg_double
#define this_cpu_cmpxchg_double_8 arch_this_cpu_cmpxchg_double

#endif /* CONFIG_64BIT */

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
