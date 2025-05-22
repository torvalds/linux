/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLER__
#include <linux/cpumask.h>
#include <linux/thread_info.h>

#include <asm/cpumask.h>

DECLARE_PER_CPU_CACHE_HOT(int, cpu_number);

DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_die_map);
/* cpus sharing the last level cache: */
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_l2c_shared_map);

DECLARE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_apicid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_acpiid);

struct task_struct;

struct smp_ops {
	void (*smp_prepare_boot_cpu)(void);
	void (*smp_prepare_cpus)(unsigned max_cpus);
	void (*smp_cpus_done)(unsigned max_cpus);

	void (*stop_other_cpus)(int wait);
	void (*crash_stop_other_cpus)(void);
	void (*smp_send_reschedule)(int cpu);

	void (*cleanup_dead_cpu)(unsigned cpu);
	void (*poll_sync_state)(void);
	int (*kick_ap_alive)(unsigned cpu, struct task_struct *tidle);
	int (*cpu_disable)(void);
	void (*cpu_die)(unsigned int cpu);
	void (*play_dead)(void);
	void (*stop_this_cpu)(void);

	void (*send_call_func_ipi)(const struct cpumask *mask);
	void (*send_call_func_single_ipi)(int cpu);
};

/* Globals due to paravirt */
extern void set_cpu_sibling_map(int cpu);

#ifdef CONFIG_SMP
extern struct smp_ops smp_ops;

static inline void smp_send_stop(void)
{
	smp_ops.stop_other_cpus(0);
}

static inline void stop_other_cpus(void)
{
	smp_ops.stop_other_cpus(1);
}

static inline void smp_prepare_cpus(unsigned int max_cpus)
{
	smp_ops.smp_prepare_cpus(max_cpus);
}

static inline void smp_cpus_done(unsigned int max_cpus)
{
	smp_ops.smp_cpus_done(max_cpus);
}

static inline int __cpu_disable(void)
{
	return smp_ops.cpu_disable();
}

static inline void __cpu_die(unsigned int cpu)
{
	if (smp_ops.cpu_die)
		smp_ops.cpu_die(cpu);
}

static inline void __noreturn play_dead(void)
{
	smp_ops.play_dead();
	BUG();
}

static inline void arch_smp_send_reschedule(int cpu)
{
	smp_ops.smp_send_reschedule(cpu);
}

static inline void arch_send_call_function_single_ipi(int cpu)
{
	smp_ops.send_call_func_single_ipi(cpu);
}

static inline void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	smp_ops.send_call_func_ipi(mask);
}

void cpu_disable_common(void);
void native_smp_prepare_boot_cpu(void);
void smp_prepare_cpus_common(void);
void native_smp_prepare_cpus(unsigned int max_cpus);
void native_smp_cpus_done(unsigned int max_cpus);
int common_cpu_up(unsigned int cpunum, struct task_struct *tidle);
int native_kick_ap(unsigned int cpu, struct task_struct *tidle);
int native_cpu_disable(void);
void __noreturn hlt_play_dead(void);
void native_play_dead(void);
void play_dead_common(void);
void wbinvd_on_cpu(int cpu);
void wbinvd_on_all_cpus(void);
void wbinvd_on_cpus_mask(struct cpumask *cpus);
void wbnoinvd_on_all_cpus(void);
void wbnoinvd_on_cpus_mask(struct cpumask *cpus);

void smp_kick_mwait_play_dead(void);
void __noreturn mwait_play_dead(unsigned int eax_hint);

void native_smp_send_reschedule(int cpu);
void native_send_call_func_ipi(const struct cpumask *mask);
void native_send_call_func_single_ipi(int cpu);

asmlinkage __visible void smp_reboot_interrupt(void);
__visible void smp_reschedule_interrupt(struct pt_regs *regs);
__visible void smp_call_function_interrupt(struct pt_regs *regs);
__visible void smp_call_function_single_interrupt(struct pt_regs *r);

#define cpu_physical_id(cpu)	per_cpu(x86_cpu_to_apicid, cpu)
#define cpu_acpi_id(cpu)	per_cpu(x86_cpu_to_acpiid, cpu)

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup.
 */
#define raw_smp_processor_id()  this_cpu_read(cpu_number)
#define __smp_processor_id() __this_cpu_read(cpu_number)

static inline struct cpumask *cpu_llc_shared_mask(int cpu)
{
	return per_cpu(cpu_llc_shared_map, cpu);
}

static inline struct cpumask *cpu_l2c_shared_mask(int cpu)
{
	return per_cpu(cpu_l2c_shared_map, cpu);
}

#else /* !CONFIG_SMP */
#define wbinvd_on_cpu(cpu)     wbinvd()
static inline void wbinvd_on_all_cpus(void)
{
	wbinvd();
}

static inline void wbinvd_on_cpus_mask(struct cpumask *cpus)
{
	wbinvd();
}

static inline void wbnoinvd_on_all_cpus(void)
{
	wbnoinvd();
}

static inline void wbnoinvd_on_cpus_mask(struct cpumask *cpus)
{
	wbnoinvd();
}

static inline struct cpumask *cpu_llc_shared_mask(int cpu)
{
	return (struct cpumask *)cpumask_of(0);
}

static inline void __noreturn mwait_play_dead(unsigned int eax_hint) { BUG(); }
#endif /* CONFIG_SMP */

#ifdef CONFIG_DEBUG_NMI_SELFTEST
extern void nmi_selftest(void);
#else
#define nmi_selftest() do { } while (0)
#endif

extern unsigned int smpboot_control;
extern unsigned long apic_mmio_base;

#endif /* !__ASSEMBLER__ */

/* Control bits for startup_64 */
#define STARTUP_READ_APICID	0x80000000

/* Top 8 bits are reserved for control */
#define STARTUP_PARALLEL_MASK	0xFF000000

#endif /* _ASM_X86_SMP_H */
