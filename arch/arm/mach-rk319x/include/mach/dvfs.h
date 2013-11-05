#ifndef RK_MACH_DVFS_H
#define RK_MACH_DVFS_H

#include <plat/dvfs.h>

#ifdef CONFIG_DVFS
int rk3188_dvfs_init(void);
void dvfs_adjust_table_lmtvolt(struct clk *clk, struct cpufreq_frequency_table *table);
#else
static inline int rk3188_dvfs_init(void){ return 0; }
static inline void dvfs_adjust_table_lmtvolt(struct clk *clk, struct cpufreq_frequency_table *table){}
#endif

#endif
