#ifndef __HIGHBANK_CORE_H
#define __HIGHBANK_CORE_H

extern void highbank_set_cpu_jump(int cpu, void *jump_addr);
extern void highbank_clocks_init(void);
extern void highbank_restart(char, const char *);
extern void __iomem *scu_base_addr;
#ifdef CONFIG_DEBUG_HIGHBANK_UART
extern void highbank_lluart_map_io(void);
#else
static inline void highbank_lluart_map_io(void) {}
#endif

#ifdef CONFIG_PM_SLEEP
extern void highbank_pm_init(void);
#else
static inline void highbank_pm_init(void) {}
#endif

extern void highbank_smc1(int fn, int arg);
extern void highbank_cpu_die(unsigned int cpu);

extern struct smp_operations highbank_smp_ops;

#endif
