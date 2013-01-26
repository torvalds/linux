#ifndef RK_MACH_DVFS_H
#define RK_MACH_DVFS_H

#include <plat/dvfs.h>

#ifdef CONFIG_DVFS
int rk_dvfs_init(void);
#else
static inline int rk_dvfs_init(void){ return 0; }
#endif

#endif

