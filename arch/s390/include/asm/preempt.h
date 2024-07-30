/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

#include <asm/current.h>
#include <linux/thread_info.h>
#include <asm/atomic_ops.h>

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

/* We use the MSB mostly because its available */
#define PREEMPT_NEED_RESCHED	0x80000000
#define PREEMPT_ENABLED	(0 + PREEMPT_NEED_RESCHED)

static __always_inline int preempt_count(void)
{
	return READ_ONCE(get_lowcore()->preempt_count) & ~PREEMPT_NEED_RESCHED;
}

static __always_inline void preempt_count_set(int pc)
{
	int old, new;

	do {
		old = READ_ONCE(get_lowcore()->preempt_count);
		new = (old & PREEMPT_NEED_RESCHED) |
			(pc & ~PREEMPT_NEED_RESCHED);
	} while (__atomic_cmpxchg(&get_lowcore()->preempt_count,
				  old, new) != old);
}

static __always_inline void set_preempt_need_resched(void)
{
	__atomic_and(~PREEMPT_NEED_RESCHED, &get_lowcore()->preempt_count);
}

static __always_inline void clear_preempt_need_resched(void)
{
	__atomic_or(PREEMPT_NEED_RESCHED, &get_lowcore()->preempt_count);
}

static __always_inline bool test_preempt_need_resched(void)
{
	return !(READ_ONCE(get_lowcore()->preempt_count) & PREEMPT_NEED_RESCHED);
}

static __always_inline void __preempt_count_add(int val)
{
	/*
	 * With some obscure config options and CONFIG_PROFILE_ALL_BRANCHES
	 * enabled, gcc 12 fails to handle __builtin_constant_p().
	 */
	if (!IS_ENABLED(CONFIG_PROFILE_ALL_BRANCHES)) {
		if (__builtin_constant_p(val) && (val >= -128) && (val <= 127)) {
			__atomic_add_const(val, &get_lowcore()->preempt_count);
			return;
		}
	}
	__atomic_add(val, &get_lowcore()->preempt_count);
}

static __always_inline void __preempt_count_sub(int val)
{
	__preempt_count_add(-val);
}

static __always_inline bool __preempt_count_dec_and_test(void)
{
	return __atomic_add(-1, &get_lowcore()->preempt_count) == 1;
}

static __always_inline bool should_resched(int preempt_offset)
{
	return unlikely(READ_ONCE(get_lowcore()->preempt_count) ==
			preempt_offset);
}

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define PREEMPT_ENABLED	(0)

static __always_inline int preempt_count(void)
{
	return READ_ONCE(get_lowcore()->preempt_count);
}

static __always_inline void preempt_count_set(int pc)
{
	get_lowcore()->preempt_count = pc;
}

static __always_inline void set_preempt_need_resched(void)
{
}

static __always_inline void clear_preempt_need_resched(void)
{
}

static __always_inline bool test_preempt_need_resched(void)
{
	return false;
}

static __always_inline void __preempt_count_add(int val)
{
	get_lowcore()->preempt_count += val;
}

static __always_inline void __preempt_count_sub(int val)
{
	get_lowcore()->preempt_count -= val;
}

static __always_inline bool __preempt_count_dec_and_test(void)
{
	return !--get_lowcore()->preempt_count && tif_need_resched();
}

static __always_inline bool should_resched(int preempt_offset)
{
	return unlikely(preempt_count() == preempt_offset &&
			tif_need_resched());
}

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define init_task_preempt_count(p)	do { } while (0)
/* Deferred to CPU bringup time */
#define init_idle_preempt_count(p, cpu)	do { } while (0)

#ifdef CONFIG_PREEMPTION
extern void preempt_schedule(void);
#define __preempt_schedule() preempt_schedule()
extern void preempt_schedule_notrace(void);
#define __preempt_schedule_notrace() preempt_schedule_notrace()
#endif /* CONFIG_PREEMPTION */

#endif /* __ASM_PREEMPT_H */
