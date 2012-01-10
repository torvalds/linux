/*
 *  arch/arm/include/asm/localtimer.h
 *
 *  Copyright (C) 2004-2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_LOCALTIMER_H
#define __ASM_ARM_LOCALTIMER_H

#include <linux/errno.h>
#include <linux/interrupt.h>

struct clock_event_device;

struct local_timer_ops {
	int  (*setup)(struct clock_event_device *);
	void (*stop)(struct clock_event_device *);
};

/*
 * Setup a per-cpu timer, whether it be a local timer or dummy broadcast
 */
void percpu_timer_setup(void);

#ifdef CONFIG_LOCAL_TIMERS

#ifdef CONFIG_HAVE_ARM_TWD

#include "smp_twd.h"

#endif

/*
 * Stop the local timer
 */
void local_timer_stop(struct clock_event_device *);

/*
 * Setup a local timer interrupt for a CPU.
 */
int local_timer_setup(struct clock_event_device *);

/*
 * Register a local timer driver
 */
int local_timer_register(struct local_timer_ops *);

#else

static inline int local_timer_setup(struct clock_event_device *evt)
{
	return -ENXIO;
}

static inline void local_timer_stop(struct clock_event_device *evt)
{
}

static inline int local_timer_register(struct local_timer_ops *ops)
{
	return -ENXIO;
}
#endif

#endif
