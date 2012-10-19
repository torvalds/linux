#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <linux/preempt.h>
#include <asm/cmpxchg.h>

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 */
#define __my_cpu_offset S390_lowcore.percpu_offset

/*
 * For 64 bit module code, the module may be more than 4G above the
 * per cpu area, use weak definitions to force the compiler to
 * generate external references.
 */
#if defined(CONFIG_SMP) && defined(CONFIG_64BIT) && defined(MODULE)
#define ARCH_NEEDS_WEAK_PER_CPU
#endif

#define arch_this_cpu_to_op(pcp, val, op)				\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ old__, new__, prev__;				\
	pcp_op_T__ *ptr__;						\
	preempt_disable();						\
	ptr__ = __this_cpu_ptr(&(pcp));					\
	prev__ = *ptr__;						\
	do {								\
		old__ = prev__;						\
		new__ = old__ op (val);					\
		switch (sizeof(*ptr__)) {				\
		case 8:							\
			prev__ = cmpxchg64(ptr__, old__, new__);	\
			break;						\
		default:						\
			prev__ = cmpxchg(ptr__, old__, new__);		\
		}							\
	} while (prev__ != old__);					\
	preempt_enable();						\
	new__;								\
})

#define this_cpu_add_1(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_2(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_4(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_8(pcp, val) arch_this_cpu_to_op(pcp, val, +)

#define this_cpu_add_return_1(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_return_2(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_return_4(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_return_8(pcp, val) arch_this_cpu_to_op(pcp, val, +)

#define this_cpu_and_1(pcp, val) arch_this_cpu_to_op(pcp, val, &)
#define this_cpu_and_2(pcp, val) arch_this_cpu_to_op(pcp, val, &)
#define this_cpu_and_4(pcp, val) arch_this_cpu_to_op(pcp, val, &)
#define this_cpu_and_8(pcp, val) arch_this_cpu_to_op(pcp, val, &)

#define this_cpu_or_1(pcp, val) arch_this_cpu_to_op(pcp, val, |)
#define this_cpu_or_2(pcp, val) arch_this_cpu_to_op(pcp, val, |)
#define this_cpu_or_4(pcp, val) arch_this_cpu_to_op(pcp, val, |)
#define this_cpu_or_8(pcp, val) arch_this_cpu_to_op(pcp, val, |)

#define this_cpu_xor_1(pcp, val) arch_this_cpu_to_op(pcp, val, ^)
#define this_cpu_xor_2(pcp, val) arch_this_cpu_to_op(pcp, val, ^)
#define this_cpu_xor_4(pcp, val) arch_this_cpu_to_op(pcp, val, ^)
#define this_cpu_xor_8(pcp, val) arch_this_cpu_to_op(pcp, val, ^)

#define arch_this_cpu_cmpxchg(pcp, oval, nval)				\
({									\
	typedef typeof(pcp) pcp_op_T__;					\
	pcp_op_T__ ret__;						\
	pcp_op_T__ *ptr__;						\
	preempt_disable();						\
	ptr__ = __this_cpu_ptr(&(pcp));					\
	switch (sizeof(*ptr__)) {					\
	case 8:								\
		ret__ = cmpxchg64(ptr__, oval, nval);			\
		break;							\
	default:							\
		ret__ = cmpxchg(ptr__, oval, nval);			\
	}								\
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
	ptr__ = __this_cpu_ptr(&(pcp));					\
	ret__ = xchg(ptr__, nval);					\
	preempt_enable();						\
	ret__;								\
})

#define this_cpu_xchg_1(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_2(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#define this_cpu_xchg_4(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#ifdef CONFIG_64BIT
#define this_cpu_xchg_8(pcp, nval) arch_this_cpu_xchg(pcp, nval)
#endif

#define arch_this_cpu_cmpxchg_double(pcp1, pcp2, o1, o2, n1, n2)	\
({									\
	typeof(pcp1) o1__ = (o1), n1__ = (n1);				\
	typeof(pcp2) o2__ = (o2), n2__ = (n2);				\
	typeof(pcp1) *p1__;						\
	typeof(pcp2) *p2__;						\
	int ret__;							\
	preempt_disable();						\
	p1__ = __this_cpu_ptr(&(pcp1));					\
	p2__ = __this_cpu_ptr(&(pcp2));					\
	ret__ = __cmpxchg_double(p1__, p2__, o1__, o2__, n1__, n2__);	\
	preempt_enable();						\
	ret__;								\
})

#define this_cpu_cmpxchg_double_4 arch_this_cpu_cmpxchg_double
#ifdef CONFIG_64BIT
#define this_cpu_cmpxchg_double_8 arch_this_cpu_cmpxchg_double
#endif

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
