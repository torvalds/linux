#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern void shmobile_earlytimer_init(void);
extern void shmobile_setup_delay(unsigned int max_cpu_core_mhz,
			 unsigned int mult, unsigned int div);
struct twd_local_timer;
extern void shmobile_setup_console(void);
extern void shmobile_boot_vector(void);
extern unsigned long shmobile_boot_fn;
extern unsigned long shmobile_boot_arg;
extern unsigned long shmobile_boot_size;
extern void shmobile_smp_boot(void);
extern void shmobile_smp_sleep(void);
extern void shmobile_smp_hook(unsigned int cpu, unsigned long fn,
			      unsigned long arg);
extern int shmobile_smp_cpu_disable(unsigned int cpu);
extern void shmobile_invalidate_start(void);
extern void shmobile_boot_scu(void);
extern void shmobile_smp_scu_prepare_cpus(unsigned int max_cpus);
extern void shmobile_smp_scu_cpu_die(unsigned int cpu);
extern int shmobile_smp_scu_cpu_kill(unsigned int cpu);
extern void shmobile_smp_apmu_prepare_cpus(unsigned int max_cpus);
extern int shmobile_smp_apmu_boot_secondary(unsigned int cpu,
					    struct task_struct *idle);
extern void shmobile_smp_apmu_cpu_die(unsigned int cpu);
extern int shmobile_smp_apmu_cpu_kill(unsigned int cpu);
struct clk;
extern int shmobile_clk_init(void);
extern void shmobile_handle_irq_intc(struct pt_regs *);
extern struct platform_suspend_ops shmobile_suspend_ops;
struct cpuidle_driver;
extern void shmobile_cpuidle_set_driver(struct cpuidle_driver *drv);

#ifdef CONFIG_SUSPEND
int shmobile_suspend_init(void);
#else
static inline int shmobile_suspend_init(void) { return 0; }
#endif

#ifdef CONFIG_CPU_IDLE
int shmobile_cpuidle_init(void);
#else
static inline int shmobile_cpuidle_init(void) { return 0; }
#endif

extern void __iomem *shmobile_scu_base;

static inline void __init shmobile_init_late(void)
{
	shmobile_suspend_init();
	shmobile_cpuidle_init();
}

#endif /* __ARCH_MACH_COMMON_H */
