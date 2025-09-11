/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __I915_WAIT_UTIL_H__
#define __I915_WAIT_UTIL_H__

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/sched/clock.h>
#include <linux/smp.h>

/*
 * __wait_for - magic wait macro
 *
 * Macro to help avoid open coding check/wait/timeout patterns. Note that it's
 * important that we check the condition again after having timed out, since the
 * timeout could be due to preemption or similar and we've never had a chance to
 * check the condition before the timeout.
 */
#define __wait_for(OP, COND, US, Wmin, Wmax) ({ \
	const ktime_t end__ = ktime_add_ns(ktime_get_raw(), 1000ll * (US)); \
	long wait__ = (Wmin); /* recommended min for usleep is 10 us */	\
	int ret__;							\
	might_sleep();							\
	for (;;) {							\
		const bool expired__ = ktime_after(ktime_get_raw(), end__); \
		OP;							\
		/* Guarantee COND check prior to timeout */		\
		barrier();						\
		if (COND) {						\
			ret__ = 0;					\
			break;						\
		}							\
		if (expired__) {					\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		usleep_range(wait__, wait__ * 2);			\
		if (wait__ < (Wmax))					\
			wait__ <<= 1;					\
	}								\
	ret__;								\
})

#define _wait_for(COND, US, Wmin, Wmax)	__wait_for(, (COND), (US), (Wmin), \
						   (Wmax))
#define wait_for(COND, MS)		_wait_for((COND), (MS) * 1000, 10, 1000)

/*
 * If CONFIG_PREEMPT_COUNT is disabled, in_atomic() always reports false.
 * On PREEMPT_RT the context isn't becoming atomic because it is used in an
 * interrupt handler or because a spinlock_t is acquired. This leads to
 * warnings which don't occur otherwise and therefore the check is disabled.
 */
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG) && IS_ENABLED(CONFIG_PREEMPT_COUNT) && !defined(CONFIG_PREEMPT_RT)
# define _WAIT_FOR_ATOMIC_CHECK(ATOMIC) WARN_ON_ONCE((ATOMIC) && !in_atomic())
#else
# define _WAIT_FOR_ATOMIC_CHECK(ATOMIC) do { } while (0)
#endif

#define _wait_for_atomic(COND, US, ATOMIC) \
({ \
	int cpu, ret, timeout = (US) * 1000; \
	u64 base; \
	_WAIT_FOR_ATOMIC_CHECK(ATOMIC); \
	if (!(ATOMIC)) { \
		preempt_disable(); \
		cpu = smp_processor_id(); \
	} \
	base = local_clock(); \
	for (;;) { \
		u64 now = local_clock(); \
		if (!(ATOMIC)) \
			preempt_enable(); \
		/* Guarantee COND check prior to timeout */ \
		barrier(); \
		if (COND) { \
			ret = 0; \
			break; \
		} \
		if (now - base >= timeout) { \
			ret = -ETIMEDOUT; \
			break; \
		} \
		cpu_relax(); \
		if (!(ATOMIC)) { \
			preempt_disable(); \
			if (unlikely(cpu != smp_processor_id())) { \
				timeout -= now - base; \
				cpu = smp_processor_id(); \
				base = local_clock(); \
			} \
		} \
	} \
	ret; \
})

#define wait_for_us(COND, US) \
({ \
	int ret__; \
	BUILD_BUG_ON(!__builtin_constant_p(US)); \
	if ((US) > 10) \
		ret__ = _wait_for((COND), (US), 10, 10); \
	else \
		ret__ = _wait_for_atomic((COND), (US), 0); \
	ret__; \
})

#define wait_for_atomic_us(COND, US) \
({ \
	BUILD_BUG_ON(!__builtin_constant_p(US)); \
	BUILD_BUG_ON((US) > 50000); \
	_wait_for_atomic((COND), (US), 1); \
})

#define wait_for_atomic(COND, MS) wait_for_atomic_us((COND), (MS) * 1000)

#endif /* __I915_WAIT_UTIL_H__ */
