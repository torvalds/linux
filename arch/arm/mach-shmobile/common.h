#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern void shmobile_earlytimer_init(void);
extern void shmobile_init_delay(void);
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
extern void shmobile_boot_scu(void);
extern void shmobile_smp_scu_prepare_cpus(unsigned int max_cpus);
extern void shmobile_smp_scu_cpu_die(unsigned int cpu);
extern int shmobile_smp_scu_cpu_kill(unsigned int cpu);
struct clk;
extern int shmobile_clk_init(void);
extern struct platform_suspend_ops shmobile_suspend_ops;

#ifdef CONFIG_SUSPEND
int shmobile_suspend_init(void);
void shmobile_smp_apmu_suspend_init(void);
#else
static inline int shmobile_suspend_init(void) { return 0; }
static inline void shmobile_smp_apmu_suspend_init(void) { }
#endif

#ifdef CONFIG_CPU_FREQ
int shmobile_cpufreq_init(void);
#else
static inline int shmobile_cpufreq_init(void) { return 0; }
#endif

extern void __iomem *shmobile_scu_base;

static inline void __init shmobile_init_late(void)
{
	shmobile_suspend_init();
	shmobile_cpufreq_init();
}

#endif /* __ARCH_MACH_COMMON_H */
