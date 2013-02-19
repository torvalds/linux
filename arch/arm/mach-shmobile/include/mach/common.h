#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern void shmobile_earlytimer_init(void);
extern void shmobile_timer_init(void);
extern void shmobile_setup_delay(unsigned int max_cpu_core_mhz,
			 unsigned int mult, unsigned int div);
struct twd_local_timer;
extern void shmobile_setup_console(void);
extern void shmobile_secondary_vector(void);
extern void shmobile_secondary_vector_scu(void);
struct clk;
extern int shmobile_clk_init(void);
extern void shmobile_handle_irq_intc(struct pt_regs *);
extern struct platform_suspend_ops shmobile_suspend_ops;
struct cpuidle_driver;
struct cpuidle_device;
extern int shmobile_enter_wfi(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv, int index);
extern void shmobile_cpuidle_set_driver(struct cpuidle_driver *drv);

extern void sh7372_init_irq(void);
extern void sh7372_map_io(void);
extern void sh7372_earlytimer_init(void);
extern void sh7372_add_early_devices(void);
extern void sh7372_add_standard_devices(void);
extern void sh7372_add_early_devices_dt(void);
extern void sh7372_add_standard_devices_dt(void);
extern void sh7372_clock_init(void);
extern void sh7372_pinmux_init(void);
extern void sh7372_pm_init(void);
extern void sh7372_resume_core_standby_sysc(void);
extern int sh7372_do_idle_sysc(unsigned long sleep_mode);
extern struct clk sh7372_extal1_clk;
extern struct clk sh7372_extal2_clk;

extern void sh73a0_init_delay(void);
extern void sh73a0_init_irq(void);
extern void sh73a0_init_irq_dt(void);
extern void sh73a0_map_io(void);
extern void sh73a0_earlytimer_init(void);
extern void sh73a0_add_early_devices(void);
extern void sh73a0_add_standard_devices(void);
extern void sh73a0_add_standard_devices_dt(void);
extern void sh73a0_clock_init(void);
extern void sh73a0_pinmux_init(void);
extern void sh73a0_pm_init(void);
extern struct clk sh73a0_extal1_clk;
extern struct clk sh73a0_extal2_clk;
extern struct clk sh73a0_extcki_clk;
extern struct clk sh73a0_extalr_clk;

extern void r8a7740_init_irq(void);
extern void r8a7740_map_io(void);
extern void r8a7740_add_early_devices(void);
extern void r8a7740_add_standard_devices(void);
extern void r8a7740_clock_init(u8 md_ck);
extern void r8a7740_pinmux_init(void);
extern void r8a7740_pm_init(void);

extern void r8a7779_init_delay(void);
extern void r8a7779_init_irq(void);
extern void r8a7779_init_irq_dt(void);
extern void r8a7779_map_io(void);
extern void r8a7779_earlytimer_init(void);
extern void r8a7779_add_early_devices(void);
extern void r8a7779_add_standard_devices(void);
extern void r8a7779_add_standard_devices_dt(void);
extern void r8a7779_clock_init(void);
extern void r8a7779_pinmux_init(void);
extern void r8a7779_pm_init(void);
extern void r8a7740_meram_workaround(void);

extern void r8a7779_register_twd(void);

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

extern void shmobile_cpu_die(unsigned int cpu);
extern int shmobile_cpu_disable(unsigned int cpu);
extern int shmobile_cpu_disable_any(unsigned int cpu);

#ifdef CONFIG_HOTPLUG_CPU
extern int shmobile_cpu_is_dead(unsigned int cpu);
#else
static inline int shmobile_cpu_is_dead(unsigned int cpu) { return 1; }
#endif

extern void __iomem *shmobile_scu_base;
extern void shmobile_smp_init_cpus(unsigned int ncores);

static inline void __init shmobile_init_late(void)
{
	shmobile_suspend_init();
	shmobile_cpuidle_init();
}

#endif /* __ARCH_MACH_COMMON_H */
