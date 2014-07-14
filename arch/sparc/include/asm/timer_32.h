/*
 * timer.h:  Definitions for the timer chips on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */


#ifndef _SPARC_TIMER_H
#define _SPARC_TIMER_H

#include <linux/clocksource.h>
#include <linux/irqreturn.h>

#include <asm-generic/percpu.h>

#include <asm/cpu_type.h>  /* For SUN4M_NCPUS */

#define SBUS_CLOCK_RATE   2000000 /* 2MHz */
#define TIMER_VALUE_SHIFT 9
#define TIMER_VALUE_MASK  0x3fffff
#define TIMER_LIMIT_BIT   (1 << 31)  /* Bit 31 in Counter-Timer register */

/* The counter timer register has the value offset by 9 bits.
 * From sun4m manual:
 * When a counter reaches the value in the corresponding limit register,
 * the Limit bit is set and the counter is set to 500 nS (i.e. 0x00000200).
 *
 * To compensate for this add one to the value.
 */
static inline unsigned int timer_value(unsigned int value)
{
	return (value + 1) << TIMER_VALUE_SHIFT;
}

extern volatile u32 __iomem *master_l10_counter;

irqreturn_t notrace timer_interrupt(int dummy, void *dev_id);

#ifdef CONFIG_SMP
DECLARE_PER_CPU(struct clock_event_device, sparc32_clockevent);
void register_percpu_ce(int cpu);
#endif

#endif /* !(_SPARC_TIMER_H) */
