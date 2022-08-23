/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for measuring cputime on powerpc machines.
 *
 * Copyright (C) 2006 Paul Mackerras, IBM Corp.
 *
 * If we have CONFIG_VIRT_CPU_ACCOUNTING_NATIVE, we measure cpu time in
 * the same units as the timebase.  Otherwise we measure cpu time
 * in jiffies using the generic definitions.
 */

#ifndef __POWERPC_CPUTIME_H
#define __POWERPC_CPUTIME_H

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE

#include <linux/types.h>
#include <linux/time.h>
#include <asm/div64.h>
#include <asm/time.h>
#include <asm/param.h>
#include <asm/firmware.h>

typedef u64 __nocast cputime_t;
typedef u64 __nocast cputime64_t;

#define cmpxchg_cputime(ptr, old, new) cmpxchg(ptr, old, new)

#ifdef __KERNEL__
/*
 * Convert cputime <-> microseconds
 */
extern u64 __cputime_usec_factor;

static inline unsigned long cputime_to_usecs(const cputime_t ct)
{
	return mulhdu((__force u64) ct, __cputime_usec_factor);
}

#define cputime_to_nsecs(cputime) tb_to_ns((__force u64)cputime)

/*
 * PPC64 uses PACA which is task independent for storing accounting data while
 * PPC32 uses struct thread_info, therefore at task switch the accounting data
 * has to be populated in the new task
 */
#ifdef CONFIG_PPC64
#define get_accounting(tsk)	(&get_paca()->accounting)
#define raw_get_accounting(tsk)	(&local_paca->accounting)
static inline void arch_vtime_task_switch(struct task_struct *tsk) { }

#else
#define get_accounting(tsk)	(&task_thread_info(tsk)->accounting)
#define raw_get_accounting(tsk)	get_accounting(tsk)
/*
 * Called from the context switch with interrupts disabled, to charge all
 * accumulated times to the current process, and to prepare accounting on
 * the next process.
 */
static inline void arch_vtime_task_switch(struct task_struct *prev)
{
	struct cpu_accounting_data *acct = get_accounting(current);
	struct cpu_accounting_data *acct0 = get_accounting(prev);

	acct->starttime = acct0->starttime;
}
#endif

/*
 * account_cpu_user_entry/exit runs "unreconciled", so can't trace,
 * can't use get_paca()
 */
static notrace inline void account_cpu_user_entry(void)
{
	unsigned long tb = mftb();
	struct cpu_accounting_data *acct = raw_get_accounting(current);

	acct->utime += (tb - acct->starttime_user);
	acct->starttime = tb;
}

static notrace inline void account_cpu_user_exit(void)
{
	unsigned long tb = mftb();
	struct cpu_accounting_data *acct = raw_get_accounting(current);

	acct->stime += (tb - acct->starttime);
	acct->starttime_user = tb;
}

static notrace inline void account_stolen_time(void)
{
#ifdef CONFIG_PPC_SPLPAR
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct lppaca *lp = local_paca->lppaca_ptr;

		if (unlikely(local_paca->dtl_ridx != be64_to_cpu(lp->dtl_idx)))
			accumulate_stolen_time();
	}
#endif
}

#endif /* __KERNEL__ */
#else /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
static inline void account_cpu_user_entry(void)
{
}
static inline void account_cpu_user_exit(void)
{
}
static notrace inline void account_stolen_time(void)
{
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
#endif /* __POWERPC_CPUTIME_H */
