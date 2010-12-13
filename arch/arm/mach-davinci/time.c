/*
 * DaVinci timer subsystem
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <mach/cputype.h>
#include <mach/time.h>
#include "clock.h"

static struct clock_event_device clockevent_davinci;
static unsigned int davinci_clock_tick_rate;

/*
 * This driver configures the 2 64-bit count-up timers as 4 independent
 * 32-bit count-up timers used as follows:
 */

enum {
	TID_CLOCKEVENT,
	TID_CLOCKSOURCE,
};

/* Timer register offsets */
#define PID12			0x0
#define TIM12			0x10
#define TIM34			0x14
#define PRD12			0x18
#define PRD34			0x1c
#define TCR			0x20
#define TGCR			0x24
#define WDTCR			0x28

/* Offsets of the 8 compare registers */
#define	CMP12_0			0x60
#define	CMP12_1			0x64
#define	CMP12_2			0x68
#define	CMP12_3			0x6c
#define	CMP12_4			0x70
#define	CMP12_5			0x74
#define	CMP12_6			0x78
#define	CMP12_7			0x7c

/* Timer register bitfields */
#define TCR_ENAMODE_DISABLE          0x0
#define TCR_ENAMODE_ONESHOT          0x1
#define TCR_ENAMODE_PERIODIC         0x2
#define TCR_ENAMODE_MASK             0x3

#define TGCR_TIMMODE_SHIFT           2
#define TGCR_TIMMODE_64BIT_GP        0x0
#define TGCR_TIMMODE_32BIT_UNCHAINED 0x1
#define TGCR_TIMMODE_64BIT_WDOG      0x2
#define TGCR_TIMMODE_32BIT_CHAINED   0x3

#define TGCR_TIM12RS_SHIFT           0
#define TGCR_TIM34RS_SHIFT           1
#define TGCR_RESET                   0x0
#define TGCR_UNRESET                 0x1
#define TGCR_RESET_MASK              0x3

#define WDTCR_WDEN_SHIFT             14
#define WDTCR_WDEN_DISABLE           0x0
#define WDTCR_WDEN_ENABLE            0x1
#define WDTCR_WDKEY_SHIFT            16
#define WDTCR_WDKEY_SEQ0             0xa5c6
#define WDTCR_WDKEY_SEQ1             0xda7e

struct timer_s {
	char *name;
	unsigned int id;
	unsigned long period;
	unsigned long opts;
	unsigned long flags;
	void __iomem *base;
	unsigned long tim_off;
	unsigned long prd_off;
	unsigned long enamode_shift;
	struct irqaction irqaction;
};
static struct timer_s timers[];

/* values for 'opts' field of struct timer_s */
#define TIMER_OPTS_DISABLED		0x01
#define TIMER_OPTS_ONESHOT		0x02
#define TIMER_OPTS_PERIODIC		0x04
#define TIMER_OPTS_STATE_MASK		0x07

#define TIMER_OPTS_USE_COMPARE		0x80000000
#define USING_COMPARE(t)		((t)->opts & TIMER_OPTS_USE_COMPARE)

static char *id_to_name[] = {
	[T0_BOT]	= "timer0_0",
	[T0_TOP]	= "timer0_1",
	[T1_BOT]	= "timer1_0",
	[T1_TOP]	= "timer1_1",
};

static int timer32_config(struct timer_s *t)
{
	u32 tcr;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	if (USING_COMPARE(t)) {
		struct davinci_timer_instance *dtip =
				soc_info->timer_info->timers;
		int event_timer = ID_TO_TIMER(timers[TID_CLOCKEVENT].id);

		/*
		 * Next interrupt should be the current time reg value plus
		 * the new period (using 32-bit unsigned addition/wrapping
		 * to 0 on overflow).  This assumes that the clocksource
		 * is setup to count to 2^32-1 before wrapping around to 0.
		 */
		__raw_writel(__raw_readl(t->base + t->tim_off) + t->period,
			t->base + dtip[event_timer].cmp_off);
	} else {
		tcr = __raw_readl(t->base + TCR);

		/* disable timer */
		tcr &= ~(TCR_ENAMODE_MASK << t->enamode_shift);
		__raw_writel(tcr, t->base + TCR);

		/* reset counter to zero, set new period */
		__raw_writel(0, t->base + t->tim_off);
		__raw_writel(t->period, t->base + t->prd_off);

		/* Set enable mode */
		if (t->opts & TIMER_OPTS_ONESHOT)
			tcr |= TCR_ENAMODE_ONESHOT << t->enamode_shift;
		else if (t->opts & TIMER_OPTS_PERIODIC)
			tcr |= TCR_ENAMODE_PERIODIC << t->enamode_shift;

		__raw_writel(tcr, t->base + TCR);
	}
	return 0;
}

static inline u32 timer32_read(struct timer_s *t)
{
	return __raw_readl(t->base + t->tim_off);
}

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_davinci;

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

/* called when 32-bit counter wraps */
static irqreturn_t freerun_interrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static struct timer_s timers[] = {
	[TID_CLOCKEVENT] = {
		.name      = "clockevent",
		.opts      = TIMER_OPTS_DISABLED,
		.irqaction = {
			.flags   = IRQF_DISABLED | IRQF_TIMER,
			.handler = timer_interrupt,
		}
	},
	[TID_CLOCKSOURCE] = {
		.name       = "free-run counter",
		.period     = ~0,
		.opts       = TIMER_OPTS_PERIODIC,
		.irqaction = {
			.flags   = IRQF_DISABLED | IRQF_TIMER,
			.handler = freerun_interrupt,
		}
	},
};

static void __init timer_init(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	struct davinci_timer_instance *dtip = soc_info->timer_info->timers;
	void __iomem *base[2];
	int i;

	/* Global init of each 64-bit timer as a whole */
	for(i=0; i<2; i++) {
		u32 tgcr;

		base[i] = ioremap(dtip[i].base, SZ_4K);
		if (WARN_ON(!base[i]))
			continue;

		/* Disabled, Internal clock source */
		__raw_writel(0, base[i] + TCR);

		/* reset both timers, no pre-scaler for timer34 */
		tgcr = 0;
		__raw_writel(tgcr, base[i] + TGCR);

		/* Set both timers to unchained 32-bit */
		tgcr = TGCR_TIMMODE_32BIT_UNCHAINED << TGCR_TIMMODE_SHIFT;
		__raw_writel(tgcr, base[i] + TGCR);

		/* Unreset timers */
		tgcr |= (TGCR_UNRESET << TGCR_TIM12RS_SHIFT) |
			(TGCR_UNRESET << TGCR_TIM34RS_SHIFT);
		__raw_writel(tgcr, base[i] + TGCR);

		/* Init both counters to zero */
		__raw_writel(0, base[i] + TIM12);
		__raw_writel(0, base[i] + TIM34);
	}

	/* Init of each timer as a 32-bit timer */
	for (i=0; i< ARRAY_SIZE(timers); i++) {
		struct timer_s *t = &timers[i];
		int timer = ID_TO_TIMER(t->id);
		u32 irq;

		t->base = base[timer];
		if (!t->base)
			continue;

		if (IS_TIMER_BOT(t->id)) {
			t->enamode_shift = 6;
			t->tim_off = TIM12;
			t->prd_off = PRD12;
			irq = dtip[timer].bottom_irq;
		} else {
			t->enamode_shift = 22;
			t->tim_off = TIM34;
			t->prd_off = PRD34;
			irq = dtip[timer].top_irq;
		}

		/* Register interrupt */
		t->irqaction.name = t->name;
		t->irqaction.dev_id = (void *)t;

		if (t->irqaction.handler != NULL) {
			irq = USING_COMPARE(t) ? dtip[i].cmp_irq : irq;
			setup_irq(irq, &t->irqaction);
		}
	}
}

/*
 * clocksource
 */
static cycle_t read_cycles(struct clocksource *cs)
{
	struct timer_s *t = &timers[TID_CLOCKSOURCE];

	return (cycles_t)timer32_read(t);
}

static struct clocksource clocksource_davinci = {
	.rating		= 300,
	.read		= read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * clockevent
 */
static int davinci_set_next_event(unsigned long cycles,
				  struct clock_event_device *evt)
{
	struct timer_s *t = &timers[TID_CLOCKEVENT];

	t->period = cycles;
	timer32_config(t);
	return 0;
}

static void davinci_set_mode(enum clock_event_mode mode,
			     struct clock_event_device *evt)
{
	struct timer_s *t = &timers[TID_CLOCKEVENT];

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		t->period = davinci_clock_tick_rate / (HZ);
		t->opts &= ~TIMER_OPTS_STATE_MASK;
		t->opts |= TIMER_OPTS_PERIODIC;
		timer32_config(t);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		t->opts &= ~TIMER_OPTS_STATE_MASK;
		t->opts |= TIMER_OPTS_ONESHOT;
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		t->opts &= ~TIMER_OPTS_STATE_MASK;
		t->opts |= TIMER_OPTS_DISABLED;
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device clockevent_davinci = {
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_next_event	= davinci_set_next_event,
	.set_mode	= davinci_set_mode,
};


static void __init davinci_timer_init(void)
{
	struct clk *timer_clk;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	unsigned int clockevent_id;
	unsigned int clocksource_id;
	static char err[] __initdata = KERN_ERR
		"%s: can't register clocksource!\n";
	int i;

	clockevent_id = soc_info->timer_info->clockevent_id;
	clocksource_id = soc_info->timer_info->clocksource_id;

	timers[TID_CLOCKEVENT].id = clockevent_id;
	timers[TID_CLOCKSOURCE].id = clocksource_id;

	/*
	 * If using same timer for both clock events & clocksource,
	 * a compare register must be used to generate an event interrupt.
	 * This is equivalent to a oneshot timer only (not periodic).
	 */
	if (clockevent_id == clocksource_id) {
		struct davinci_timer_instance *dtip =
				soc_info->timer_info->timers;
		int event_timer = ID_TO_TIMER(clockevent_id);

		/* Only bottom timers can use compare regs */
		if (IS_TIMER_TOP(clockevent_id))
			pr_warning("davinci_timer_init: Invalid use"
				" of system timers.  Results unpredictable.\n");
		else if ((dtip[event_timer].cmp_off == 0)
				|| (dtip[event_timer].cmp_irq == 0))
			pr_warning("davinci_timer_init:  Invalid timer instance"
				" setup.  Results unpredictable.\n");
		else {
			timers[TID_CLOCKEVENT].opts |= TIMER_OPTS_USE_COMPARE;
			clockevent_davinci.features = CLOCK_EVT_FEAT_ONESHOT;
		}
	}

	timer_clk = clk_get(NULL, "timer0");
	BUG_ON(IS_ERR(timer_clk));
	clk_enable(timer_clk);

	/* init timer hw */
	timer_init();

	davinci_clock_tick_rate = clk_get_rate(timer_clk);

	/* setup clocksource */
	clocksource_davinci.name = id_to_name[clocksource_id];
	if (clocksource_register_hz(&clocksource_davinci,
				    davinci_clock_tick_rate))
		printk(err, clocksource_davinci.name);

	/* setup clockevent */
	clockevent_davinci.name = id_to_name[timers[TID_CLOCKEVENT].id];
	clockevent_davinci.mult = div_sc(davinci_clock_tick_rate, NSEC_PER_SEC,
					 clockevent_davinci.shift);
	clockevent_davinci.max_delta_ns =
		clockevent_delta2ns(0xfffffffe, &clockevent_davinci);
	clockevent_davinci.min_delta_ns = 50000; /* 50 usec */

	clockevent_davinci.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_davinci);

	for (i=0; i< ARRAY_SIZE(timers); i++)
		timer32_config(&timers[i]);
}

struct sys_timer davinci_timer = {
	.init   = davinci_timer_init,
};


/* reset board using watchdog timer */
void davinci_watchdog_reset(struct platform_device *pdev)
{
	u32 tgcr, wdtcr;
	void __iomem *base;
	struct clk *wd_clk;

	base = ioremap(pdev->resource[0].start, SZ_4K);
	if (WARN_ON(!base))
		return;

	wd_clk = clk_get(&pdev->dev, NULL);
	if (WARN_ON(IS_ERR(wd_clk)))
		return;
	clk_enable(wd_clk);

	/* disable, internal clock source */
	__raw_writel(0, base + TCR);

	/* reset timer, set mode to 64-bit watchdog, and unreset */
	tgcr = 0;
	__raw_writel(tgcr, base + TGCR);
	tgcr = TGCR_TIMMODE_64BIT_WDOG << TGCR_TIMMODE_SHIFT;
	tgcr |= (TGCR_UNRESET << TGCR_TIM12RS_SHIFT) |
		(TGCR_UNRESET << TGCR_TIM34RS_SHIFT);
	__raw_writel(tgcr, base + TGCR);

	/* clear counter and period regs */
	__raw_writel(0, base + TIM12);
	__raw_writel(0, base + TIM34);
	__raw_writel(0, base + PRD12);
	__raw_writel(0, base + PRD34);

	/* put watchdog in pre-active state */
	wdtcr = __raw_readl(base + WDTCR);
	wdtcr = (WDTCR_WDKEY_SEQ0 << WDTCR_WDKEY_SHIFT) |
		(WDTCR_WDEN_ENABLE << WDTCR_WDEN_SHIFT);
	__raw_writel(wdtcr, base + WDTCR);

	/* put watchdog in active state */
	wdtcr = (WDTCR_WDKEY_SEQ1 << WDTCR_WDKEY_SHIFT) |
		(WDTCR_WDEN_ENABLE << WDTCR_WDEN_SHIFT);
	__raw_writel(wdtcr, base + WDTCR);

	/* write an invalid value to the WDKEY field to trigger
	 * a watchdog reset */
	wdtcr = 0x00004000;
	__raw_writel(wdtcr, base + WDTCR);
}
