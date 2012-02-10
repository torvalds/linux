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
#if defined(CONFIG_SMP) && defined(__s390x__) && defined(MODULE)
#define ARCH_NEEDS_WEAK_PER_CPU
#endif

#define arch_this_cpu_to_op(pcp, val, op)				\
do {									\
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
} while (0)

#define this_cpu_add_1(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_2(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_4(pcp, val) arch_this_cpu_to_op(pcp, val, +)
#define this_cpu_add_8(pcp, val) arch_this_cpu_to_op(pcp, val, +)

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

#define arch_this_cpu_cmpxchg(pcp, oval, nval)			\
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

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
