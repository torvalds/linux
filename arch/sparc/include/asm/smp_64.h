/* smp.h: Sparc64 specific SMP stuff.
 *
 * Copyright (C) 1996, 2008 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_SMP_H
#define _SPARC64_SMP_H

#include <linux/threads.h>
#include <asm/asi.h>
#include <asm/starfire.h>
#include <asm/spitfire.h>

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>
#include <linux/cache.h>

#endif /* !(__ASSEMBLY__) */

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

/*
 *	Private routines/data
 */

#include <linux/bitops.h>
#include <linux/atomic.h>
#include <asm/percpu.h>

DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
extern cpumask_t cpu_core_map[NR_CPUS];

void arch_send_call_function_single_ipi(int cpu);
void arch_send_call_function_ipi_mask(const struct cpumask *mask);

/*
 *	General functions that each host system must provide.
 */

int hard_smp_processor_id(void);
#define raw_smp_processor_id() (current_thread_info()->cpu)

void smp_fill_in_cpu_possible_map(void);
void smp_fill_in_sib_core_maps(void);
void cpu_play_dead(void);

void smp_fetch_global_regs(void);
void smp_fetch_global_pmu(void);

struct seq_file;
void smp_bogo(struct seq_file *);
void smp_info(struct seq_file *);

void smp_callin(void);
void cpu_panic(void);
void smp_synchronize_tick_client(void);
void smp_capture(void);
void smp_release(void);

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void);
void __cpu_die(unsigned int cpu);
#endif

#endif /* !(__ASSEMBLY__) */

#else

#define hard_smp_processor_id()		0
#define smp_fill_in_sib_core_maps() do { } while (0)
#define smp_fetch_global_regs() do { } while (0)
#define smp_fetch_global_pmu() do { } while (0)

#endif /* !(CONFIG_SMP) */

#endif /* !(_SPARC64_SMP_H) */
