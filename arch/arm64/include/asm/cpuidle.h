#ifndef __ASM_CPUIDLE_H
#define __ASM_CPUIDLE_H

#include <asm/proc-fns.h>

#ifdef CONFIG_CPU_IDLE
extern int arm_cpuidle_init(unsigned int cpu);
extern int cpu_suspend(unsigned long arg);
#else
static inline int arm_cpuidle_init(unsigned int cpu)
{
	return -EOPNOTSUPP;
}

static inline int cpu_suspend(unsigned long arg)
{
	return -EOPNOTSUPP;
}
#endif
static inline int arm_cpuidle_suspend(int index)
{
	return cpu_suspend(index);
}
#endif
