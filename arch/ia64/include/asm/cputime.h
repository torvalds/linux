/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for measuring cputime on ia64 machines.
 *
 * Based on <asm-powerpc/cputime.h>.
 *
 * Copyright (C) 2007 FUJITSU LIMITED
 * Copyright (C) 2007 Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 *
 * If we have CONFIG_VIRT_CPU_ACCOUNTING_NATIVE, we measure cpu time in nsec.
 * Otherwise we measure cpu time in jiffies using the generic definitions.
 */

#ifndef __IA64_CPUTIME_H
#define __IA64_CPUTIME_H

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
extern void arch_vtime_task_switch(struct task_struct *tsk);
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

#endif /* __IA64_CPUTIME_H */
