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

#define TMU0		(0)
#define TMU1		(1)

static inline void _tmu_start(int tmu_num)
{
	ctrl_outb(ctrl_inb(TMU_012_TSTR) | (0x1<<tmu_num), TMU_012_TSTR);
}

static inline void _tmu_set_irq(int tmu_num, int enabled)
{
	register unsigned long tmu_tcr = TMU0_TCR + (0xc*tmu_num);
	ctrl_outw( (enabled ? ctrl_inw(tmu_tcr) | (1<<5) : ctrl_inw(tmu_tcr) & ~(1<<5)), tmu_tcr);
}

static inline void _tmu_stop(int tmu_num)
{
	ctrl_outb(ctrl_inb(TMU_012_TSTR) & ~(0x1<<tmu_num), TMU_012_TSTR);
}

static inline void _tmu_clear_status(int tmu_num)
{
	register unsigned long tmu_tcr = TMU0_TCR + (0xc*tmu_num);
	/* Clear UNF bit */
	ctrl_outw(ctrl_inw(tmu_tcr) & ~0x100, tmu_tcr);
}

static inline unsigned long _tmu_read(int tmu_num)
{
        return ctrl_inl(TMU0_TCNT+0xC*tmu_num);
}

static int tmu_timer_start(void)
{
	_tmu_start(TMU0);
	_tmu_start(TMU1);
	_tmu_set_irq(TMU0,1);
	return 0;
}

static int tmu_timer_stop(void)
{
	_tmu_stop(TMU0);
	_tmu_stop(TMU1);
	_tmu_clear_status(TMU0);
	return 0;
}

/*
 * also when the module_clk is scaled the TMU1
 * will show the same frequency
 */
static int tmus_are_scaled;

static cycle_t tmu_timer_read(void)
{
	return ((cycle_t)(~_tmu_read(TMU1)))<<tmus_are_scaled;
}


static unsigned long tmu_latest_interval[3];
static void tmu_timer_set_interval(int tmu_num, unsigned long interval, unsigned int reload)
{
	unsigned long tmu_tcnt = TMU0_TCNT + tmu_num*0xC;
	unsigned long tmu_tcor = TMU0_TCOR + tmu_num*0xC;

	_tmu_stop(tmu_num);

	ctrl_outl(interval, tmu_tcnt);
	tmu_latest_interval[tmu_num] = interval;

	/*
	 * TCNT reloads from TCOR on underflow, clear it if we don't
	 * intend to auto-reload
	 */
	ctrl_outl( reload ? interval : 0 , tmu_tcor);

	_tmu_start(tmu_num);
}

static int tmu_set_next_event(unsigned long cycles,
			      struct clock_event_device *evt)
{
	tmu_timer_set_interval(TMU0,cycles, evt->mode == CLOCK_EVT_MODE_PERIODIC);
	_tmu_set_irq(TMU0,1);
	return 0;
}

static void tmu_set_mode(enum clock_event_mode mode,
			 struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		ctrl_outl(tmu_latest_interval[TMU0], TMU0_TCOR);
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
	_tmu_clear_status(TMU0);
	_tmu_set_irq(TMU0,tmu0_clockevent.mode != CLOCK_EVT_MODE_ONESHOT);

	switch (tmu0_clockevent.mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_PERIODIC:
		evt->event_handler(evt);
		break;
	default:
		break;
	}

	return IRQ_HANDLED;
}

static struct irqaction tmu0_irq = {
	.name		= "periodic/oneshot timer",
	.handler	= tmu_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
};

static void __init tmu_clk_init(struct clk *clk)
{
	u8 divisor  = TMU_TCR_INIT & 0x7;
	int tmu_num = clk->name[3]-'0';
	ctrl_outw(TMU_TCR_INIT, TMU0_TCR+(tmu_num*0xC));
	clk->rate = clk_get_rate(clk->parent) / (4 << (divisor << 1));
}

static void tmu_clk_recalc(struct clk *clk)
{
	int tmu_num = clk->name[3]-'0';
	unsigned long prev_rate = clk_get_rate(clk);
	unsigned long flags;
	u8 divisor = ctrl_inw(TMU0_TCR+tmu_num*0xC) & 0x7;
	clk->rate  = clk_get_rate(clk->parent) / (4 << (divisor << 1));

	if(prev_rate==clk_get_rate(clk))
		return;

	if(tmu_num)
		return; /* No more work on TMU1 */

	local_irq_save(flags);
	tmus_are_scaled = (prev_rate > clk->rate);

	_tmu_stop(TMU0);

	tmu0_clockevent.mult = div_sc(clk->rate, NSEC_PER_SEC,
				tmu0_clockevent.shift);
	tmu0_clockevent.max_delta_ns =
			clockevent_delta2ns(-1, &tmu0_clockevent);
	tmu0_clockevent.min_delta_ns =
			clockevent_delta2ns(1, &tmu0_clockevent);

	if (tmus_are_scaled)
		tmu_latest_interval[TMU0] >>= 1;
	else
		tmu_latest_interval[TMU0] <<= 1;

	tmu_timer_set_interval(TMU0,
		tmu_latest_interval[TMU0],
		tmu0_clockevent.mode == CLOCK_EVT_MODE_PERIODIC);

	_tmu_start(TMU0);

	local_irq_restore(flags);
}

static struct clk_ops tmu_clk_ops = {
	.init		= tmu_clk_init,
	.recalc		= tmu_clk_recalc,
};

static struct clk tmu0_clk = {
	.name		= "tmu0_clk",
	.ops		= &tmu_clk_ops,
};

static struct clk tmu1_clk = {
	.name		= "tmu1_clk",
	.ops		= &tmu_clk_ops,
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
    !defined(CONFIG_CPU_SUBTYPE_SH7721) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7760) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7785) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7786) && \
    !defined(CONFIG_CPU_SUBTYPE_SHX3)
	ctrl_outb(TMU_TOCR_INIT, TMU_TOCR);
#endif

	clk_register(&tmu0_clk);
	clk_register(&tmu1_clk);
	clk_enable(&tmu0_clk);
	clk_enable(&tmu1_clk);

	frequency = clk_get_rate(&tmu0_clk);
	interval = (frequency + HZ / 2) / HZ;

	tmu_timer_set_interval(TMU0,interval, 1);
	tmu_timer_set_interval(TMU1,~0,1);

	_tmu_start(TMU1);

	clocksource_sh.rating = 200;
	clocksource_sh.mask = CLOCKSOURCE_MASK(32);
	clocksource_sh.read = tmu_timer_read;
	clocksource_sh.shift = 10;
	clocksource_sh.mult = clocksource_hz2mult(clk_get_rate(&tmu1_clk),
						  clocksource_sh.shift);
	clocksource_sh.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	clocksource_register(&clocksource_sh);

	tmu0_clockevent.mult = div_sc(frequency, NSEC_PER_SEC,
				      tmu0_clockevent.shift);
	tmu0_clockevent.max_delta_ns =
			clockevent_delta2ns(-1, &tmu0_clockevent);
	tmu0_clockevent.min_delta_ns =
			clockevent_delta2ns(1, &tmu0_clockevent);

	tmu0_clockevent.cpumask = cpumask_of(0);
	tmu0_clockevent.rating = 100;

	clockevents_register_device(&tmu0_clockevent);

	return 0;
}

static struct sys_timer_ops tmu_timer_ops = {
	.init		= tmu_timer_init,
	.start		= tmu_timer_start,
	.stop		= tmu_timer_stop,
};

struct sys_timer tmu_timer = {
	.name	= "tmu",
	.ops	= &tmu_timer_ops,
};
