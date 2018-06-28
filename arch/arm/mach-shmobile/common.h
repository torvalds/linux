/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern void shmobile_init_delay(void);
extern void shmobile_boot_vector(void);
extern unsigned long shmobile_boot_fn;
extern unsigned long shmobile_boot_size;
extern void shmobile_boot_vector_gen2(void);
extern unsigned long shmobile_boot_fn_gen2;
extern unsigned long shmobile_boot_cpu_gen2;
extern unsigned long shmobile_boot_size_gen2;
extern void shmobile_smp_boot(void);
extern void shmobile_smp_sleep(void);
extern void shmobile_smp_hook(unsigned int cpu, unsigned long fn,
			      unsigned long arg);
extern bool shmobile_smp_cpu_can_disable(unsigned int cpu);
extern bool shmobile_smp_init_fallback_ops(void);
extern void shmobile_boot_apmu(void);
extern void shmobile_boot_scu(void);
extern void shmobile_smp_scu_prepare_cpus(phys_addr_t scu_base_phys,
					  unsigned int max_cpus);
extern void shmobile_smp_scu_cpu_die(unsigned int cpu);
extern int shmobile_smp_scu_cpu_kill(unsigned int cpu);
extern struct platform_suspend_ops shmobile_suspend_ops;

#ifdef CONFIG_SUSPEND
int shmobile_suspend_init(void);
void shmobile_smp_apmu_suspend_init(void);
#else
static inline int shmobile_suspend_init(void) { return 0; }
static inline void shmobile_smp_apmu_suspend_init(void) { }
#endif

static inline void __init shmobile_init_late(void)
{
	shmobile_suspend_init();
}

#endif /* __ARCH_MACH_COMMON_H */
