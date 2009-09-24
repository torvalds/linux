/*
 *  include/asm-s390/smp.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *               Heiko Carstens (heiko.carstens@de.ibm.com)
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>

#if defined(__KERNEL__) && defined(CONFIG_SMP) && !defined(__ASSEMBLY__)

#include <asm/lowcore.h>
#include <asm/sigp.h>
#include <asm/ptrace.h>
#include <asm/system.h>

/*
  s390 specific smp.c headers
 */
typedef struct
{
	int        intresting;
	sigp_ccode ccode; 
	__u32      status;
	__u16      cpu;
} sigp_info;

extern void machine_restart_smp(char *);
extern void machine_halt_smp(void);
extern void machine_power_off_smp(void);

#define NO_PROC_ID		0xFF		/* No processor magic marker */

/*
 *	This magic constant controls our willingness to transfer
 *	a process across CPUs. Such a transfer incurs misses on the L1
 *	cache, and on a P6 or P5 with multiple L2 caches L2 hits. My
 *	gut feeling is this will vary by board in value. For a board
 *	with separate L2 cache it probably depends also on the RSS, and
 *	for a board with shared L2 cache it ought to decay fast as other
 *	processes are run.
 */
 
#define PROC_CHANGE_PENALTY	20		/* Schedule penalty */

#define raw_smp_processor_id()	(S390_lowcore.cpu_nr)
#define cpu_logical_map(cpu)	(cpu)

extern int __cpu_disable (void);
extern void __cpu_die (unsigned int cpu);
extern void cpu_die (void) __attribute__ ((noreturn));
extern int __cpu_up (unsigned int cpu);

extern struct mutex smp_cpu_state_mutex;
extern int smp_cpu_polarization[];

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

#endif

#ifdef CONFIG_HOTPLUG_CPU
extern int smp_rescan_cpus(void);
#else
static inline int smp_rescan_cpus(void) { return 0; }
#endif

extern union save_area *zfcpdump_save_areas[NR_CPUS + 1];
#endif
