#ifndef __ASM_CPUIDLE_H
#define __ASM_CPUIDLE_H

#ifdef CONFIG_CPU_IDLE
extern int cpu_init_idle(unsigned int cpu);
extern int cpu_suspend(unsigned long arg);
#else
static inline int cpu_init_idle(unsigned int cpu)
{
	return -EOPNOTSUPP;
}

static inline int cpu_suspend(unsigned long arg)
{
	return -EOPNOTSUPP;
}
#endif

#endif
