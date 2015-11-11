#ifndef __AR5312_H
#define __AR5312_H

#ifdef CONFIG_SOC_AR5312

void ar5312_arch_init_irq(void);
void ar5312_init_devices(void);
void ar5312_plat_time_init(void);
void ar5312_plat_mem_setup(void);
void ar5312_arch_init(void);

#else

static inline void ar5312_arch_init_irq(void) {}
static inline void ar5312_init_devices(void) {}
static inline void ar5312_plat_time_init(void) {}
static inline void ar5312_plat_mem_setup(void) {}
static inline void ar5312_arch_init(void) {}

#endif

#endif	/* __AR5312_H */
