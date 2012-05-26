#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern void shmobile_earlytimer_init(void);
extern struct sys_timer shmobile_timer;
extern void shmobile_setup_delay(unsigned int max_cpu_core_mhz,
				 unsigned int mult, unsigned int div);
struct twd_local_timer;
extern void shmobile_setup_console(void);
extern void shmobile_secondary_vector(void);
extern int shmobile_platform_cpu_kill(unsigned int cpu);
struct clk;
extern int shmobile_clk_init(void);
extern void shmobile_handle_irq_intc(struct pt_regs *);
extern struct platform_suspend_ops shmobile_suspend_ops;
struct cpuidle_driver;
extern void (*shmobile_cpuidle_modes[])(void);
extern void (*shmobile_cpuidle_setup)(struct cpuidle_driver *drv);

extern void sh7367_init_irq(void);
extern void sh7367_map_io(void);
extern void sh7367_add_early_devices(void);
extern void sh7367_add_standard_devices(void);
extern void sh7367_clock_init(void);
extern void sh7367_pinmux_init(void);
extern struct clk sh7367_extalb1_clk;
extern struct clk sh7367_extal2_clk;

extern void sh7377_init_irq(void);
extern void sh7377_map_io(void);
extern void sh7377_add_early_devices(void);
extern void sh7377_add_standard_devices(void);
extern void sh7377_clock_init(void);
extern void sh7377_pinmux_init(void);
extern struct clk sh7377_extalc1_clk;
extern struct clk sh7377_extal2_clk;

extern void sh7372_init_irq(void);
extern void sh7372_map_io(void);
extern void sh7372_add_early_devices(void);
extern void sh7372_add_standard_devices(void);
extern void sh7372_clock_init(void);
extern void sh7372_pinmux_init(void);
extern void sh7372_pm_init(void);
extern void sh7372_resume_core_standby_sysc(void);
extern int sh7372_do_idle_sysc(unsigned long sleep_mode);
extern struct clk sh7372_extal1_clk;
extern struct clk sh7372_extal2_clk;

extern void sh73a0_init_irq(void);
extern void sh73a0_map_io(void);
extern void sh73a0_add_early_devices(void);
extern void sh73a0_add_standard_devices(void);
extern void sh73a0_clock_init(void);
extern void sh73a0_pinmux_init(void);
extern struct clk sh73a0_extal1_clk;
extern struct clk sh73a0_extal2_clk;
extern struct clk sh73a0_extcki_clk;
extern struct clk sh73a0_extalr_clk;

extern unsigned int sh73a0_get_core_count(void);
extern void sh73a0_secondary_init(unsigned int cpu);
extern int sh73a0_boot_secondary(unsigned int cpu);
extern void sh73a0_smp_prepare_cpus(void);

extern void r8a7740_init_irq(void);
extern void r8a7740_map_io(void);
extern void r8a7740_add_early_devices(void);
extern void r8a7740_add_standard_devices(void);
extern void r8a7740_clock_init(u8 md_ck);
extern void r8a7740_pinmux_init(void);

extern void r8a7779_init_irq(void);
extern void r8a7779_map_io(void);
extern void r8a7779_add_early_devices(void);
extern void r8a7779_add_standard_devices(void);
extern void r8a7779_clock_init(void);
extern void r8a7779_pinmux_init(void);
extern void r8a7779_pm_init(void);

extern unsigned int r8a7779_get_core_count(void);
extern int r8a7779_platform_cpu_kill(unsigned int cpu);
extern void r8a7779_secondary_init(unsigned int cpu);
extern int r8a7779_boot_secondary(unsigned int cpu);
extern void r8a7779_smp_prepare_cpus(void);
extern void r8a7779_register_twd(void);

extern void shmobile_init_late(void);

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

#endif /* __ARCH_MACH_COMMON_H */
