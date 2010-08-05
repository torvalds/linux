/*
 * Copyright 2007-2009 Analog Devices Inc.
 *                          Philippe Gerum <rpm@xenomai.org>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BLACKFIN_SMP_H
#define __ASM_BLACKFIN_SMP_H

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/cache.h>
#include <asm/blackfin.h>
#include <mach/smp.h>

#define raw_smp_processor_id()  blackfin_core_id()

extern void bfin_relocate_coreb_l1_mem(void);

#if defined(CONFIG_SMP) && defined(CONFIG_ICACHE_FLUSH_L1)
asmlinkage void blackfin_icache_flush_range_l1(unsigned long *ptr);
extern unsigned long blackfin_iflush_l1_entry[NR_CPUS];
#endif

struct corelock_slot {
	int lock;
};
extern struct corelock_slot corelock;

#ifdef __ARCH_SYNC_CORE_ICACHE
extern unsigned long icache_invld_count[NR_CPUS];
#endif
#ifdef __ARCH_SYNC_CORE_DCACHE
extern unsigned long dcache_invld_count[NR_CPUS];
#endif

void smp_icache_flush_range_others(unsigned long start,
				   unsigned long end);
#ifdef CONFIG_HOTPLUG_CPU
void coreb_die(void);
void cpu_die(void);
void platform_cpu_die(void);
int __cpu_disable(void);
int __cpu_die(unsigned int cpu);
#endif

#endif /* !__ASM_BLACKFIN_SMP_H */
