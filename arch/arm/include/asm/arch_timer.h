#ifndef __ASMARM_ARCH_TIMER_H
#define __ASMARM_ARCH_TIMER_H

#include <linux/ioport.h>

struct arch_timer {
	struct resource	res[2];
};

#ifdef CONFIG_ARM_ARCH_TIMER
int arch_timer_register(struct arch_timer *);
int arch_timer_sched_clock_init(void);
#else
static inline int arch_timer_register(struct arch_timer *at)
{
	return -ENXIO;
}

static inline int arch_timer_sched_clock_init(void)
{
	return -ENXIO;
}
#endif

#endif
