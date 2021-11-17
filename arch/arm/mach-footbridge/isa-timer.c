// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/arm/mach-footbridge/isa-timer.c
 *
 *  Copyright (C) 1998 Russell King.
 *  Copyright (C) 1998 Phil Blundell
 */
#include <linux/clockchips.h>
#include <linux/i8253.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#include <asm/irq.h>
#include <asm/mach/time.h>

#include "common.h"

static irqreturn_t pit_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;
	ce->event_handler(ce);
	return IRQ_HANDLED;
}

void __init isa_timer_init(void)
{
	clocksource_i8253_init();

	if (request_irq(i8253_clockevent.irq, pit_timer_interrupt,
			IRQF_TIMER | IRQF_IRQPOLL, "pit", &i8253_clockevent))
		pr_err("Failed to request irq %d(pit)\n", i8253_clockevent.irq);
	clockevent_i8253_init(false);
}
