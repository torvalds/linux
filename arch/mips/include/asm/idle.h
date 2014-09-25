#ifndef __ASM_IDLE_H
#define __ASM_IDLE_H

#include <linux/cpuidle.h>
#include <linux/linkage.h>

extern void (*cpu_wait)(void);
extern void r4k_wait(void);
extern asmlinkage void __r4k_wait(void);
extern void r4k_wait_irqoff(void);
extern void __pastwait(void);

static inline int using_rollback_handler(void)
{
	return cpu_wait == r4k_wait;
}

static inline int address_is_in_r4k_wait_irqoff(unsigned long addr)
{
	return addr >= (unsigned long)r4k_wait_irqoff &&
	       addr < (unsigned long)__pastwait;
}

extern int mips_cpuidle_wait_enter(struct cpuidle_device *dev,
				   struct cpuidle_driver *drv, int index);

#define MIPS_CPUIDLE_WAIT_STATE {\
	.enter			= mips_cpuidle_wait_enter,\
	.exit_latency		= 1,\
	.target_residency	= 1,\
	.power_usage		= UINT_MAX,\
	.flags			= CPUIDLE_FLAG_TIME_VALID,\
	.name			= "wait",\
	.desc			= "MIPS wait",\
}

#endif /* __ASM_IDLE_H  */
