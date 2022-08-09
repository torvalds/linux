/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Denis Joseph Barrow,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <asm/sigp.h>
#include <asm/lowcore.h>
#include <asm/processor.h>

#define raw_smp_processor_id()	(S390_lowcore.cpu_nr)

extern struct mutex smp_cpu_state_mutex;
extern unsigned int smp_cpu_mt_shift;
extern unsigned int smp_cpu_mtid;
extern __vector128 __initdata boot_cpu_vector_save_area[__NUM_VXRS];
extern cpumask_t cpu_setup_mask;

extern int __cpu_up(unsigned int cpu, struct task_struct *tidle);

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

extern void smp_call_online_cpu(void (*func)(void *), void *);
extern void smp_call_ipl_cpu(void (*func)(void *), void *);
extern void smp_emergency_stop(void);

extern int smp_find_processor_id(u16 address);
extern int smp_store_status(int cpu);
extern void smp_save_dump_cpus(void);
extern void smp_yield_cpu(int cpu);
extern void smp_cpu_set_polarization(int cpu, int val);
extern int smp_cpu_get_polarization(int cpu);
extern int smp_cpu_get_cpu_address(int cpu);
extern void smp_fill_possible_mask(void);
extern void smp_detect_cpus(void);

static inline void smp_stop_cpu(void)
{
	u16 pcpu = stap();

	for (;;) {
		__pcpu_sigp(pcpu, SIGP_STOP, 0, NULL);
		cpu_relax();
	}
}

/* Return thread 0 CPU number as base CPU */
static inline int smp_get_base_cpu(int cpu)
{
	return cpu - (cpu % (smp_cpu_mtid + 1));
}

static inline void smp_cpus_done(unsigned int max_cpus)
{
}

extern int smp_rescan_cpus(void);
extern void __noreturn cpu_die(void);
extern void __cpu_die(unsigned int cpu);
extern int __cpu_disable(void);
extern void schedule_mcck_handler(void);
void notrace smp_yield_cpu(int cpu);

#endif /* __ASM_SMP_H */
