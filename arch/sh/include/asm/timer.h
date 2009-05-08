#ifndef __ASM_SH_TIMER_H
#define __ASM_SH_TIMER_H

#include <linux/sysdev.h>
#include <linux/clocksource.h>
#include <cpu/timer.h>

struct sys_timer_ops {
	int (*init)(void);
	int (*start)(void);
	int (*stop)(void);
};

struct sys_timer {
	const char		*name;

	struct sys_device	dev;
	struct sys_timer_ops	*ops;
};

extern struct sys_timer tmu_timer;
extern struct sys_timer *sys_timer;

/* arch/sh/kernel/timers/timer.c */
struct sys_timer *get_sys_timer(void);

extern struct clocksource clocksource_sh;

#endif /* __ASM_SH_TIMER_H */
