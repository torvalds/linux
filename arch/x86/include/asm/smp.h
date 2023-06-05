/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>

#include <asm/cpumask.h>
#include <asm/current.h>
#include <asm/thread_info.h>

extern int smp_num_siblings;
extern unsigned int num_processors;

DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_die_map);
/* cpus sharing the last level cache: */
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_l2c_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(u16, cpu_llc_id);
DECLARE_PER_CPU_READ_MOSTLY(u16, cpu_l2c_id);

DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_cpu_to_apicid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_acpiid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_bios_cpu_apicid);
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86_32)
DECLARE_EARLY_PER_CPU_READ_MOSTLY(int, x86_cpu_to_logical_apicid);
#endif

struct task_struct;

struct smp_ops {
	void (*smp_prepare_boot_cpu)(void);
	void (*smp_prepare_cpus)(unsigned max_cpus);
	void (*smp_cpus_done)(unsigned max_cpus);

	void (*stop_other_cpus)(int wait);
	void (*crash_stop_other_cpus)(void);
	void (*smp_send_reschedule)(int cpu);

	int (*cpu_up)(unsigned cpu, struct task_struct *tidle);
	int (*cpu_disable)(void);
	void (*cpu_die)(unsigned int cpu);
	void (*play_dead)(void);

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

static inline void smp_prepare_boot_cpu(void)
{
	smp_ops.smp_prepare_boot_cpu();
}

static inline void smp_prepare_cpus(unsigned int max_cpus)
{
	smp_ops.smp_prepare_cpus(max_cpus);
}

static inline void smp_cpus_done(unsigned int max_cpus)
{
	smp_ops.smp_cpus_done(max_cpus);
}

static inline int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	return smp_ops.cpu_up(cpu, tidle);
}

static inline int __cpu_disable(void)
{
	return smp_ops.cpu_disable();
}

static inline void __cpu_die(unsigned int cpu)
{
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
void calculate_max_logical_packages(void);
void native_smp_cpus_done(unsigned int max_cpus);
int common_cpu_up(unsigned int cpunum, struct task_struct *tidle);
int native_cpu_up(unsigned int cpunum, struct task_struct *tidle);
int native_cpu_disable(void);
int common_cpu_die(unsigned int cpu);
void native_cpu_die(unsigned int cpu);
void __noreturn hlt_play_dead(void);
void native_play_dead(void);
void play_dead_common(void);
void wbinvd_on_cpu(int cpu);
int wbinvd_on_all_cpus(void);
void cond_wakeup_cpu0(void);

void native_smp_send_reschedule(int cpu);
void native_send_call_func_ipi(const struct cpumask *mask);
void native_send_call_func_single_ipi(int cpu);
void x86_idle_thread_init(unsigned int cpu, struct task_struct *idle);

void smp_store_boot_cpu_info(void);
void smp_store_cpu_info(int id);

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
#define raw_smp_processor_id()  this_cpu_read(pcpu_hot.cpu_number)
#define __smp_processor_id() __this_cpu_read(pcpu_hot.cpu_number)

#ifdef CONFIG_X86_32
extern int safe_smp_processor_id(void);
#else
# define safe_smp_processor_id()	smp_processor_id()
#endif

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
static inline int wbinvd_on_all_cpus(void)
{
	wbinvd();
	return 0;
}

static inline struct cpumask *cpu_llc_shared_mask(int cpu)
{
	return (struct cpumask *)cpumask_of(0);
}
#endif /* CONFIG_SMP */

extern unsigned disabled_cpus;

#ifdef CONFIG_X86_LOCAL_APIC
extern int hard_smp_processor_id(void);

#else /* CONFIG_X86_LOCAL_APIC */
#define hard_smp_processor_id()	0
#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_DEBUG_NMI_SELFTEST
extern void nmi_selftest(void);
#else
#define nmi_selftest() do { } while (0)
#endif

extern unsigned int smpboot_control;

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_X86_SMP_H */
