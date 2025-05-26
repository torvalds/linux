/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RQSPINLOCK_H
#define _ASM_RQSPINLOCK_H

#include <asm/barrier.h>

/*
 * Hardcode res_smp_cond_load_acquire implementations for arm64 to a custom
 * version based on [0]. In rqspinlock code, our conditional expression involves
 * checking the value _and_ additionally a timeout. However, on arm64, the
 * WFE-based implementation may never spin again if no stores occur to the
 * locked byte in the lock word. As such, we may be stuck forever if
 * event-stream based unblocking is not available on the platform for WFE spin
 * loops (arch_timer_evtstrm_available).
 *
 * Once support for smp_cond_load_acquire_timewait [0] lands, we can drop this
 * copy-paste.
 *
 * While we rely on the implementation to amortize the cost of sampling
 * cond_expr for us, it will not happen when event stream support is
 * unavailable, time_expr check is amortized. This is not the common case, and
 * it would be difficult to fit our logic in the time_expr_ns >= time_limit_ns
 * comparison, hence just let it be. In case of event-stream, the loop is woken
 * up at microsecond granularity.
 *
 * [0]: https://lore.kernel.org/lkml/20250203214911.898276-1-ankur.a.arora@oracle.com
 */

#ifndef smp_cond_load_acquire_timewait

#define smp_cond_time_check_count	200

#define __smp_cond_load_relaxed_spinwait(ptr, cond_expr, time_expr_ns,	\
					 time_limit_ns) ({		\
	typeof(ptr) __PTR = (ptr);					\
	__unqual_scalar_typeof(*ptr) VAL;				\
	unsigned int __count = 0;					\
	for (;;) {							\
		VAL = READ_ONCE(*__PTR);				\
		if (cond_expr)						\
			break;						\
		cpu_relax();						\
		if (__count++ < smp_cond_time_check_count)		\
			continue;					\
		if ((time_expr_ns) >= (time_limit_ns))			\
			break;						\
		__count = 0;						\
	}								\
	(typeof(*ptr))VAL;						\
})

#define __smp_cond_load_acquire_timewait(ptr, cond_expr,		\
					 time_expr_ns, time_limit_ns)	\
({									\
	typeof(ptr) __PTR = (ptr);					\
	__unqual_scalar_typeof(*ptr) VAL;				\
	for (;;) {							\
		VAL = smp_load_acquire(__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, VAL);				\
		if ((time_expr_ns) >= (time_limit_ns))			\
			break;						\
	}								\
	(typeof(*ptr))VAL;						\
})

#define smp_cond_load_acquire_timewait(ptr, cond_expr,			\
				      time_expr_ns, time_limit_ns)	\
({									\
	__unqual_scalar_typeof(*ptr) _val;				\
	int __wfe = arch_timer_evtstrm_available();			\
									\
	if (likely(__wfe)) {						\
		_val = __smp_cond_load_acquire_timewait(ptr, cond_expr,	\
							time_expr_ns,	\
							time_limit_ns);	\
	} else {							\
		_val = __smp_cond_load_relaxed_spinwait(ptr, cond_expr,	\
							time_expr_ns,	\
							time_limit_ns);	\
		smp_acquire__after_ctrl_dep();				\
	}								\
	(typeof(*ptr))_val;						\
})

#endif

#define res_smp_cond_load_acquire(v, c) smp_cond_load_acquire_timewait(v, c, 0, 1)

#include <asm-generic/rqspinlock.h>

#endif /* _ASM_RQSPINLOCK_H */
