/*
 *    Copyright IBM Corp. 1999,2009
 *    Author(s): Denis Joseph Barrow,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifdef CONFIG_SMP

#include <asm/system.h>
#include <asm/sigp.h>

extern void machine_restart_smp(char *);
extern void machine_halt_smp(void);
extern void machine_power_off_smp(void);

#define raw_smp_processor_id()	(S390_lowcore.cpu_nr)

extern int __cpu_disable (void);
extern void __cpu_die (unsigned int cpu);
extern void cpu_die (void) __attribute__ ((noreturn));
extern int __cpu_up (unsigned int cpu);

extern struct mutex smp_cpu_state_mutex;
extern int smp_cpu_polarization[];

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

extern union save_area *zfcpdump_save_areas[NR_CPUS + 1];

#endif /* CONFIG_SMP */

#ifdef CONFIG_HOTPLUG_CPU
extern int smp_rescan_cpus(void);
#else
static inline int smp_rescan_cpus(void) { return 0; }
#endif

#endif /* __ASM_SMP_H */
