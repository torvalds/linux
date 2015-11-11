/*
 * arch/score/kernel/time.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/clockchips.h>
#include <linux/interrupt.h>

#include <asm/scoreregs.h>

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evdev = dev_id;

	/* clear timer interrupt flag */
	outl(1, P_TIMER0_CPP_REG);
	evdev->event_handler(evdev);

	return IRQ_HANDLED;
}

static struct irqaction timer_irq = {
	.handler = timer_interrupt,
	.flags = IRQF_TIMER,
	.name = "timer",
};

static int score_timer_set_next_event(unsigned long delta,
		struct clock_event_device *evdev)
{
	outl((TMR_M_PERIODIC | TMR_IE_ENABLE), P_TIMER0_CTRL);
	outl(delta, P_TIMER0_PRELOAD);
	outl(inl(P_TIMER0_CTRL) | TMR_ENABLE, P_TIMER0_CTRL);

	return 0;
}

static int score_timer_set_periodic(struct clock_event_device *evt)
{
	outl((TMR_M_PERIODIC | TMR_IE_ENABLE), P_TIMER0_CTRL);
	outl(SYSTEM_CLOCK / HZ, P_TIMER0_PRELOAD);
	outl(inl(P_TIMER0_CTRL) | TMR_ENABLE, P_TIMER0_CTRL);
	return 0;
}

static struct clock_event_device score_clockevent = {
	.name			= "score_clockevent",
	.features		= CLOCK_EVT_FEAT_PERIODIC,
	.shift			= 16,
	.set_next_event		= score_timer_set_next_event,
	.set_state_periodic	= score_timer_set_periodic,
};

void __init time_init(void)
{
	timer_irq.dev_id = &score_clockevent;
	setup_irq(IRQ_TIMER , &timer_irq);

	/* setup COMPARE clockevent */
	score_clockevent.mult = div_sc(SYSTEM_CLOCK, NSEC_PER_SEC,
					score_clockevent.shift);
	score_clockevent.max_delta_ns = clockevent_delta2ns((u32)~0,
					&score_clockevent);
	score_clockevent.min_delta_ns = clockevent_delta2ns(50,
						&score_clockevent) + 1;
	score_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&score_clockevent);
}
