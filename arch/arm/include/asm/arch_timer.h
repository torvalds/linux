#ifndef __ASMARM_ARCH_TIMER_H
#define __ASMARM_ARCH_TIMER_H

#include <asm/errno.h>

#ifdef CONFIG_ARM_ARCH_TIMER
#define ARCH_HAS_READ_CURRENT_TIMER
int arch_timer_of_register(void);
int arch_timer_sched_clock_init(void);
#else
static inline int arch_timer_of_register(void)
{
	return -ENXIO;
}

static inline int arch_timer_sched_clock_init(void)
{
	return -ENXIO;
}
#endif

#endif
