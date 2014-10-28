#ifndef __AR2315_H
#define __AR2315_H

#ifdef CONFIG_SOC_AR2315

void ar2315_plat_time_init(void);
void ar2315_plat_mem_setup(void);

#else

static inline void ar2315_plat_time_init(void) {}
static inline void ar2315_plat_mem_setup(void) {}

#endif

#endif	/* __AR2315_H */
