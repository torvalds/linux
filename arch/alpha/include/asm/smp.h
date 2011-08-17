#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <asm/pal.h>

/* HACK: Cabrio WHAMI return value is bogus if more than 8 bits used.. :-( */

static __inline__ unsigned char
__hard_smp_processor_id(void)
{
	register unsigned char __r0 __asm__("$0");
	__asm__ __volatile__(
		"call_pal %1 #whami"
		: "=r"(__r0)
		:"i" (PAL_whami)
		: "$1", "$22", "$23", "$24", "$25");
	return __r0;
}

#ifdef CONFIG_SMP

#include <asm/irq.h>

struct cpuinfo_alpha {
	unsigned long loops_per_jiffy;
	unsigned long last_asn;
	int need_new_asn;
	int asn_lock;
	unsigned long ipi_count;
	unsigned long prof_multiplier;
	unsigned long prof_counter;
	unsigned char mcheck_expected;
	unsigned char mcheck_taken;
	unsigned char mcheck_extra;
} __attribute__((aligned(64)));

extern struct cpuinfo_alpha cpu_data[NR_CPUS];

#define hard_smp_processor_id()	__hard_smp_processor_id()
#define raw_smp_processor_id()	(current_thread_info()->cpu)

extern int smp_num_cpus;

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

#else /* CONFIG_SMP */

#define hard_smp_processor_id()		0
#define smp_call_function_on_cpu(func,info,wait,cpu)    ({ 0; })

#endif /* CONFIG_SMP */

#define NO_PROC_ID	(-1)

#endif
