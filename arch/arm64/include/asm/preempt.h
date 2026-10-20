/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

#include <linux/thread_info.h>

#define PREEMPT_NEED_RESCHED	BIT(32)
#define PREEMPT_ENABLED	(PREEMPT_NEED_RESCHED)

static inline int preempt_count(void)
{
	return READ_ONCE(current_thread_info()->preempt.count);
}

static inline void preempt_count_set(u64 pc)
{
	/* Preserve existing value of PREEMPT_NEED_RESCHED */
	WRITE_ONCE(current_thread_info()->preempt.count, pc);
}

#define init_task_preempt_count(p) do { \
	task_thread_info(p)->preempt_count = FORK_PREEMPT_COUNT; \
} while (0)

#define init_idle_preempt_count(p, cpu) do { \
	task_thread_info(p)->preempt_count = PREEMPT_DISABLED; \
} while (0)

static inline void set_preempt_need_resched(void)
{
	current_thread_info()->preempt.need_resched = 0;
}

static inline void clear_preempt_need_resched(void)
{
	current_thread_info()->preempt.need_resched = 1;
}

static inline bool test_preempt_need_resched(void)
{
	return !current_thread_info()->preempt.need_resched;
}

static inline void __preempt_count_add(int val)
{
	u32 pc = READ_ONCE(current_thread_info()->preempt.count);
	pc += val;
	WRITE_ONCE(current_thread_info()->preempt.count, pc);
}

static inline void __preempt_count_sub(int val)
{
	u32 pc = READ_ONCE(current_thread_info()->preempt.count);
	pc -= val;
	WRITE_ONCE(current_thread_info()->preempt.count, pc);
}

static inline int __preempt_count_add_return(int val)
{
	u32 pc = READ_ONCE(current_thread_info()->preempt.count);

	pc += val;
	WRITE_ONCE(current_thread_info()->preempt.count, pc);

	return pc;
}

static inline int __preempt_count_sub_return(int val)
{
	u32 pc = READ_ONCE(current_thread_info()->preempt.count);

	pc -= val;
	WRITE_ONCE(current_thread_info()->preempt.count, pc);

	return pc;
}

static inline bool should_resched(int preempt_offset)
{
	u64 pc = READ_ONCE(current_thread_info()->preempt_count);
	return unlikely(pc == preempt_offset);
}

static inline bool __preempt_count_dec_and_test(void)
{
	/*
	 * We must load the combined 'preempt_count' after decrementing
	 * 'preempt.count' as an interrupt could modify 'need_resched' before
	 * __preempt_count_sub() writes back to 'preempt.count'.
	 */
	__preempt_count_sub(1);
	return should_resched(0);
}

#ifdef CONFIG_PREEMPTION

void preempt_schedule(void);
void preempt_schedule_notrace(void);

#define __preempt_schedule()		preempt_schedule()
#define __preempt_schedule_notrace()	preempt_schedule_notrace()

#endif /* CONFIG_PREEMPTION */

#endif /* __ASM_PREEMPT_H */
