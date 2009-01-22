#ifndef _MACH_BF561_SMP
#define _MACH_BF561_SMP

struct task_struct;

void platform_init_cpus(void);

void platform_prepare_cpus(unsigned int max_cpus);

int platform_boot_secondary(unsigned int cpu, struct task_struct *idle);

void platform_secondary_init(unsigned int cpu);

void platform_request_ipi(int (*handler)(int, void *));

void platform_send_ipi(cpumask_t callmap);

void platform_send_ipi_cpu(unsigned int cpu);

void platform_clear_ipi(unsigned int cpu);

#endif /* !_MACH_BF561_SMP */
