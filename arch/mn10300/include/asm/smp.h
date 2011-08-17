/* MN10300 SMP support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Modified by Matsushita Electric Industrial Co., Ltd.
 * Modifications:
 *  13-Nov-2006 MEI Define IPI-IRQ number and add inline/macro function
 *                  for SMP support.
 *  22-Jan-2007 MEI Add the define related to SMP_BOOT_IRQ.
 *  23-Feb-2007 MEI Add the define related to SMP icahce invalidate.
 *  23-Jun-2008 MEI Delete INTC_IPI.
 *  22-Jul-2008 MEI Add smp_nmi_call_function and related defines.
 *  04-Aug-2008 MEI Delete USE_DOIRQ_CACHE_IPI.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SMP_H
#define _ASM_SMP_H

#ifndef __ASSEMBLY__
#include <linux/threads.h>
#include <linux/cpumask.h>
#endif

#ifdef CONFIG_SMP
#include <proc/smp-regs.h>

#define RESCHEDULE_IPI		63
#define CALL_FUNC_SINGLE_IPI	192
#define LOCAL_TIMER_IPI		193
#define FLUSH_CACHE_IPI		194
#define CALL_FUNCTION_NMI_IPI	195
#define DEBUGGER_NMI_IPI	196

#define SMP_BOOT_IRQ		195

#define RESCHEDULE_GxICR_LV	GxICR_LEVEL_6
#define CALL_FUNCTION_GxICR_LV	GxICR_LEVEL_4
#define LOCAL_TIMER_GxICR_LV	GxICR_LEVEL_4
#define FLUSH_CACHE_GxICR_LV	GxICR_LEVEL_0
#define SMP_BOOT_GxICR_LV	GxICR_LEVEL_0
#define DEBUGGER_GxICR_LV	CONFIG_DEBUGGER_IRQ_LEVEL

#define TIME_OUT_COUNT_BOOT_IPI	100
#define DELAY_TIME_BOOT_IPI	75000


#ifndef __ASSEMBLY__

/**
 * raw_smp_processor_id - Determine the raw CPU ID of the CPU running it
 *
 * What we really want to do is to use the CPUID hardware CPU register to get
 * this information, but accesses to that aren't cached, and run at system bus
 * speed, not CPU speed.  A copy of this value is, however, stored in the
 * thread_info struct, and that can be cached.
 *
 * An alternate way of dealing with this could be to use the EPSW.S bits to
 * cache this information for systems with up to four CPUs.
 */
#define arch_smp_processor_id()	(CPUID)
#if 0
#define raw_smp_processor_id()	(arch_smp_processor_id())
#else
#define raw_smp_processor_id()	(current_thread_info()->cpu)
#endif

static inline int cpu_logical_map(int cpu)
{
	return cpu;
}

static inline int cpu_number_map(int cpu)
{
	return cpu;
}


extern cpumask_t cpu_boot_map;

extern void smp_init_cpus(void);
extern void smp_cache_interrupt(void);
extern void send_IPI_allbutself(int irq);
extern int smp_nmi_call_function(smp_call_func_t func, void *info, int wait);

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

#ifdef CONFIG_HOTPLUG_CPU
extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
#endif /* CONFIG_HOTPLUG_CPU */

#endif /* __ASSEMBLY__ */
#else /* CONFIG_SMP */
#ifndef __ASSEMBLY__

static inline void smp_init_cpus(void) {}

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_SMP */

#endif /* _ASM_SMP_H */
