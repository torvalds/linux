/*
 * Definitions for measuring cputime on ia64 machines.
 *
 * Based on <asm-powerpc/cputime.h>.
 *
 * Copyright (C) 2007 FUJITSU LIMITED
 * Copyright (C) 2007 Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * If we have CONFIG_VIRT_CPU_ACCOUNTING_NATIVE, we measure cpu time in nsec.
 * Otherwise we measure cpu time in jiffies using the generic definitions.
 */

#ifndef __IA64_CPUTIME_H
#define __IA64_CPUTIME_H

#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
# include <asm-generic/cputime.h>
#else
# include <asm/processor.h>
# include <asm-generic/cputime_nsecs.h>
extern void arch_vtime_task_switch(struct task_struct *tsk);
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

#endif /* __IA64_CPUTIME_H */
