/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SMP_H
#define __ASM_SH_SMP_H

#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <asm/smp-ops.h>

#ifdef CONFIG_SMP

#include <linux/atomic.h>
#include <asm/current.h>
#include <asm/percpu.h>

#define raw_smp_processor_id()	(current_thread_info()->cpu)

/* Map from cpu id to sequential logical cpu number. */
extern int __cpu_number_map[NR_CPUS];
#define cpu_number_map(cpu)  __cpu_number_map[cpu]

/* The reverse map from sequential logical cpu number to cpu id.  */
extern int __cpu_logical_map[NR_CPUS];
#define cpu_logical_map(cpu)  __cpu_logical_map[cpu]

enum {
	SMP_MSG_FUNCTION,
	SMP_MSG_RESCHEDULE,
	SMP_MSG_FUNCTION_SINGLE,
	SMP_MSG_TIMER,

	SMP_MSG_NR,	/* must be last */
};

DECLARE_PER_CPU(int, cpu_state);

void smp_message_recv(unsigned int msg);

void arch_send_call_function_single_ipi(int cpu);
void arch_send_call_function_ipi_mask(const struct cpumask *mask);

void native_play_dead(void);
void native_cpu_die(unsigned int cpu);
int native_cpu_disable(unsigned int cpu);

#ifdef CONFIG_HOTPLUG_CPU
void play_dead_common(void);
extern int __cpu_disable(void);

static inline void __cpu_die(unsigned int cpu)
{
	extern struct plat_smp_ops *mp_ops;     /* private */

	mp_ops->cpu_die(cpu);
}
#endif

static inline int hard_smp_processor_id(void)
{
	extern struct plat_smp_ops *mp_ops;	/* private */

	if (!mp_ops)
		return 0;	/* boot CPU */

	return mp_ops->smp_processor_id();
}

struct of_cpu_method {
	const char *method;
	struct plat_smp_ops *ops;
};

#define CPU_METHOD_OF_DECLARE(name, _method, _ops)			\
	static const struct of_cpu_method __cpu_method_of_table_##name	\
		__used __section("__cpu_method_of_table")		\
		= { .method = _method, .ops = _ops }

#else

#define hard_smp_processor_id()	(0)

#endif /* CONFIG_SMP */

#endif /* __ASM_SH_SMP_H */
