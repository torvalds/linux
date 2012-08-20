/* MN10300 clockevents
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by Mark Salter (msalter@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <asm/timex.h>
#include "internal.h"

#ifdef CONFIG_SMP
#if (CONFIG_NR_CPUS > 2) && !defined(CONFIG_GEENERIC_CLOCKEVENTS_BROADCAST)
#error "This doesn't scale well! Need per-core local timers."
#endif
#else /* CONFIG_SMP */
#define stop_jiffies_counter1()
#define reload_jiffies_counter1(x)
#define TMJC1IRQ TMJCIRQ
#endif


static int next_event(unsigned long delta,
		      struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == 0) {
		stop_jiffies_counter();
		reload_jiffies_counter(delta - 1);
	} else {
		stop_jiffies_counter1();
		reload_jiffies_counter1(delta - 1);
	}
	return 0;
}

static void set_clock_mode(enum clock_event_mode mode,
			   struct clock_event_device *evt)
{
	/* Nothing to do ...  */
}

static DEFINE_PER_CPU(struct clock_event_device, mn10300_clockevent_device);
static DEFINE_PER_CPU(struct irqaction, timer_irq);

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd;
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		stop_jiffies_counter();
	else
		stop_jiffies_counter1();

	cd = &per_cpu(mn10300_clockevent_device, cpu);
	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static void event_handler(struct clock_event_device *dev)
{
}

static inline void setup_jiffies_interrupt(int irq,
					   struct irqaction *action)
{
	u16 tmp;
	setup_irq(irq, action);
	set_intr_level(irq, NUM2GxICR_LEVEL(CONFIG_TIMER_IRQ_LEVEL));
	GxICR(irq) |= GxICR_ENABLE | GxICR_DETECT | GxICR_REQUEST;
	tmp = GxICR(irq);
}

int __init init_clockevents(void)
{
	struct clock_event_device *cd;
	struct irqaction *iact;
	unsigned int cpu = smp_processor_id();

	cd = &per_cpu(mn10300_clockevent_device, cpu);

	if (cpu == 0) {
		stop_jiffies_counter();
		cd->irq	= TMJCIRQ;
	} else {
		stop_jiffies_counter1();
		cd->irq	= TMJC1IRQ;
	}

	cd->name		= "Timestamp";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;

	/* Calculate shift/mult. We want to spawn at least 1 second */
	clockevents_calc_mult_shift(cd, MN10300_JCCLK, 1);

	/* Calculate the min / max delta */
	cd->max_delta_ns	= clockevent_delta2ns(TMJCBR_MAX, cd);
	cd->min_delta_ns	= clockevent_delta2ns(100, cd);

	cd->rating		= 200;
	cd->cpumask		= cpumask_of(smp_processor_id());
	cd->set_mode		= set_clock_mode;
	cd->event_handler	= event_handler;
	cd->set_next_event	= next_event;

	iact = &per_cpu(timer_irq, cpu);
	iact->flags = IRQF_DISABLED | IRQF_SHARED | IRQF_TIMER;
	iact->handler = timer_interrupt;

	clockevents_register_device(cd);

#if defined(CONFIG_SMP) && !defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)
	/* setup timer irq affinity so it only runs on this cpu */
	{
		struct irq_data *data;
		data = irq_get_irq_data(cd->irq);
		cpumask_copy(data->affinity, cpumask_of(cpu));
		iact->flags |= IRQF_NOBALANCING;
	}
#endif

	if (cpu == 0) {
		reload_jiffies_counter(MN10300_JC_PER_HZ - 1);
		iact->name = "CPU0 Timer";
	} else {
		reload_jiffies_counter1(MN10300_JC_PER_HZ - 1);
		iact->name = "CPU1 Timer";
	}

	setup_jiffies_interrupt(cd->irq, iact);

	return 0;
}
