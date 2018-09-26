/*
 * Definitions for measuring cputime on powerpc machines.
 *
 * Copyright (C) 2006 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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

/*
 * PPC64 uses PACA which is task independent for storing accounting data while
 * PPC32 uses struct thread_info, therefore at task switch the accounting data
 * has to be populated in the new task
 */
#ifdef CONFIG_PPC64
#define get_accounting(tsk)	(&get_paca()->accounting)
static inline void arch_vtime_task_switch(struct task_struct *tsk) { }
#else
#define get_accounting(tsk)	(&task_thread_info(tsk)->accounting)
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
	acct->startspurr = acct0->startspurr;
}
#endif

#endif /* __KERNEL__ */
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
#endif /* __POWERPC_CPUTIME_H */
