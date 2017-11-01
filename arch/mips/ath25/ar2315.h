/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AR2315_H
#define __AR2315_H

#ifdef CONFIG_SOC_AR2315

void ar2315_arch_init_irq(void);
void ar2315_init_devices(void);
void ar2315_plat_time_init(void);
void ar2315_plat_mem_setup(void);
void ar2315_arch_init(void);

#else

static inline void ar2315_arch_init_irq(void) {}
static inline void ar2315_init_devices(void) {}
static inline void ar2315_plat_time_init(void) {}
static inline void ar2315_plat_mem_setup(void) {}
static inline void ar2315_arch_init(void) {}

#endif

#endif	/* __AR2315_H */
