/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

#include <asm/current.h>
#include <linux/thread_info.h>
#include <asm/atomic_ops.h>
#include <asm/cmpxchg.h>
#include <asm/march.h>

/* We use the MSB mostly because its available */
#define PREEMPT_NEED_RESCHED	0x80000000

/*
 * We use the PREEMPT_NEED_RESCHED bit as an inverted NEED_RESCHED such
 * that a decrement hitting 0 means we can and should reschedule.
 */
#define PREEMPT_ENABLED	(0 + PREEMPT_NEED_RESCHED)

/*
 * We mask the PREEMPT_NEED_RESCHED bit so as not to confuse all current users
 * that think a non-zero value indicates we cannot preempt.
 */
static __always_inline int preempt_count(void)
{
	return READ_ONCE(get_lowcore()->preempt_count) & ~PREEMPT_NEED_RESCHED;
}

static __always_inline void preempt_count_set(int pc)
{
	int old, new;

	old = READ_ONCE(get_lowcore()->preempt_count);
	do {
		new = (old & PREEMPT_NEED_RESCHED) | (pc & ~PREEMPT_NEED_RESCHED);
	} while (!arch_try_cmpxchg(&get_lowcore()->preempt_count, &old, new));
}

/*
 * We fold the NEED_RESCHED bit into the preempt count such that
 * preempt_enable() can decrement and test for needing to reschedule with a
 * short instruction sequence.
 *
 * We invert the actual bit, so that when the decrement hits 0 we know we both
 * need to resched (the bit is cleared) and can resched (no preempt count).
 */

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

/*
 * Because we keep PREEMPT_NEED_RESCHED set when we do _not_ need to reschedule
 * a decrement which hits zero means we have no preempt_count and should
 * reschedule.
 */
static __always_inline bool __preempt_count_dec_and_test(void)
{
	return __atomic_add_const_and_test(-1, &get_lowcore()->preempt_count);
}

/*
 * Returns true when we need to resched and can (barring IRQ state).
 */
static __always_inline bool should_resched(int preempt_offset)
{
	return unlikely(READ_ONCE(get_lowcore()->preempt_count) == preempt_offset);
}

#define init_task_preempt_count(p)	do { } while (0)
/* Deferred to CPU bringup time */
#define init_idle_preempt_count(p, cpu)	do { } while (0)

#ifdef CONFIG_PREEMPTION

void preempt_schedule(void);
void preempt_schedule_notrace(void);

#ifdef CONFIG_PREEMPT_DYNAMIC

void dynamic_preempt_schedule(void);
void dynamic_preempt_schedule_notrace(void);
#define __preempt_schedule()		dynamic_preempt_schedule()
#define __preempt_schedule_notrace()	dynamic_preempt_schedule_notrace()

#else /* CONFIG_PREEMPT_DYNAMIC */

#define __preempt_schedule()		preempt_schedule()
#define __preempt_schedule_notrace()	preempt_schedule_notrace()

#endif /* CONFIG_PREEMPT_DYNAMIC */

#endif /* CONFIG_PREEMPTION */

#endif /* __ASM_PREEMPT_H */
