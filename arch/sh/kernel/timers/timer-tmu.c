/*
 * arch/sh/kernel/timers/timer-tmu.c - TMU Timer Support
 *
 *  Copyright (C) 2005 - 2007  Paul Mundt
 *
 * TMU handling code hacked out of arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002, 2003, 2004  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/seqlock.h>
#include <linux/clockchips.h>
#include <asm/timer.h>
#include <asm/rtc.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/clock.h>

#define TMU_TOCR_INIT	0x00
#define TMU_TCR_INIT	0x0020

static int tmu_timer_start(void)
{
	ctrl_outb(ctrl_inb(TMU_012_TSTR) | 0x3, TMU_012_TSTR);
	return 0;
}

static void tmu0_timer_set_interval(unsigned long interval, unsigned int reload)
{
	ctrl_outl(interval, TMU0_TCNT);

	/*
	 * TCNT reloads from TCOR on underflow, clear it if we don't
	 * intend to auto-reload
	 */
	if (reload)
		ctrl_outl(interval, TMU0_TCOR);
	else
		ctrl_outl(0, TMU0_TCOR);

	tmu_timer_start();
}

static int tmu_timer_stop(void)
{
	ctrl_outb(ctrl_inb(TMU_012_TSTR) & ~0x3, TMU_012_TSTR);
	return 0;
}

static cycle_t tmu_timer_read(void)
{
	return ~ctrl_inl(TMU1_TCNT);
}

static int tmu_set_next_event(unsigned long cycles,
			      struct clock_event_device *evt)
{
	tmu0_timer_set_interval(cycles, 1);
	return 0;
}

static void tmu_set_mode(enum clock_event_mode mode,
			 struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		ctrl_outl(ctrl_inl(TMU0_TCNT), TMU0_TCOR);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		ctrl_outl(0, TMU0_TCOR);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device tmu0_clockevent = {
	.name		= "tmu0",
	.shift		= 32,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= tmu_set_mode,
	.set_next_event	= tmu_set_next_event,
};

static irqreturn_t tmu_timer_interrupt(int irq, void *dummy)
{
	struct clock_event_device *evt = &tmu0_clockevent;
	unsigned long timer_status;

	/* Clear UNF bit */
	timer_status = ctrl_inw(TMU0_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU0_TCR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction tmu0_irq = {
	.name		= "periodic timer",
	.handler	= tmu_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.mask		= CPU_MASK_NONE,
};

static void tmu0_clk_init(struct clk *clk)
{
	u8 divisor = TMU_TCR_INIT & 0x7;
	ctrl_outw(TMU_TCR_INIT, TMU0_TCR);
	clk->rate = clk->parent->rate / (4 << (divisor << 1));
}

static void tmu0_clk_recalc(struct clk *clk)
{
	u8 divisor = ctrl_inw(TMU0_TCR) & 0x7;
	clk->rate = clk->parent->rate / (4 << (divisor << 1));
}

static struct clk_ops tmu0_clk_ops = {
	.init		= tmu0_clk_init,
	.recalc		= tmu0_clk_recalc,
};

static struct clk tmu0_clk = {
	.name		= "tmu0_clk",
	.ops		= &tmu0_clk_ops,
};

static void tmu1_clk_init(struct clk *clk)
{
	u8 divisor = TMU_TCR_INIT & 0x7;
	ctrl_outw(divisor, TMU1_TCR);
	clk->rate = clk->parent->rate / (4 << (divisor << 1));
}

static void tmu1_clk_recalc(struct clk *clk)
{
	u8 divisor = ctrl_inw(TMU1_TCR) & 0x7;
	clk->rate = clk->parent->rate / (4 << (divisor << 1));
}

static struct clk_ops tmu1_clk_ops = {
	.init		= tmu1_clk_init,
	.recalc		= tmu1_clk_recalc,
};

static struct clk tmu1_clk = {
	.name		= "tmu1_clk",
	.ops		= &tmu1_clk_ops,
};

static int tmu_timer_init(void)
{
	unsigned long interval;
	unsigned long frequency;

	setup_irq(CONFIG_SH_TIMER_IRQ, &tmu0_irq);

	tmu0_clk.parent = clk_get(NULL, "module_clk");
	tmu1_clk.parent = clk_get(NULL, "module_clk");

	tmu_timer_stop();

#if !defined(CONFIG_CPU_SUBTYPE_SH7720) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7760) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7785) && \
    !defined(CONFIG_CPU_SUBTYPE_SHX3)
	ctrl_outb(TMU_TOCR_INIT, TMU_TOCR);
#endif

	clk_register(&tmu0_clk);
	clk_register(&tmu1_clk);
	clk_enable(&tmu0_clk);
	clk_enable(&tmu1_clk);

	frequency = clk_get_rate(&tmu0_clk);
	interval = (frequency + HZ / 2) / HZ;

	sh_hpt_frequency = clk_get_rate(&tmu1_clk);
	ctrl_outl(~0, TMU1_TCNT);
	ctrl_outl(~0, TMU1_TCOR);

	tmu0_timer_set_interval(interval, 1);

	tmu0_clockevent.mult = div_sc(frequency, NSEC_PER_SEC,
				      tmu0_clockevent.shift);
	tmu0_clockevent.max_delta_ns =
			clockevent_delta2ns(-1, &tmu0_clockevent);
	tmu0_clockevent.min_delta_ns =
			clockevent_delta2ns(1, &tmu0_clockevent);

	tmu0_clockevent.cpumask = cpumask_of_cpu(0);

	clockevents_register_device(&tmu0_clockevent);

	return 0;
}

struct sys_timer_ops tmu_timer_ops = {
	.init		= tmu_timer_init,
	.start		= tmu_timer_start,
	.stop		= tmu_timer_stop,
	.read		= tmu_timer_read,
};

struct sys_timer tmu_timer = {
	.name	= "tmu",
	.ops	= &tmu_timer_ops,
};
