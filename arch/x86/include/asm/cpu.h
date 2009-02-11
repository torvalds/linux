#ifndef _ASM_X86_CPU_H
#define _ASM_X86_CPU_H

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>

#ifdef CONFIG_SMP

extern void prefill_possible_map(void);

#else /* CONFIG_SMP */

static inline void prefill_possible_map(void) {}

#define cpu_physical_id(cpu)			boot_cpu_physical_apicid
#define safe_smp_processor_id()			0
#define stack_smp_processor_id()		0

#endif /* CONFIG_SMP */

struct x86_cpu {
	struct cpu cpu;
};

#ifdef CONFIG_HOTPLUG_CPU
extern int arch_register_cpu(int num);
extern void arch_unregister_cpu(int);
#endif

DECLARE_PER_CPU(int, cpu_state);

#ifdef CONFIG_X86_HAS_BOOT_CPU_ID
extern unsigned char boot_cpu_id;
#else
#define boot_cpu_id				0
#endif

#endif /* _ASM_X86_CPU_H */
