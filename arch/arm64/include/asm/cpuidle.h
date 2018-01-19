/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_CPUIDLE_H
#define __ASM_CPUIDLE_H

#include <asm/proc-fns.h>

#ifdef CONFIG_CPU_IDLE
extern int arm_cpuidle_init(unsigned int cpu);
extern int arm_cpuidle_suspend(int index);
#else
static inline int arm_cpuidle_init(unsigned int cpu)
{
	return -EOPNOTSUPP;
}

static inline int arm_cpuidle_suspend(int index)
{
	return -EOPNOTSUPP;
}
#endif
#endif
