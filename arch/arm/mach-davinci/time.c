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
#include <linux/sched_clock.h>

#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include <mach/cputype.h>
#include <mach/hardware.h>
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
			.flags   = IRQF_TIMER,
			.handler = timer_interrupt,
		}
	},
	[TID_CLOCKSOURCE] = {
		.name       = "free-run counter",
		.period     = ~0,
		.opts       = TIMER_OPTS_PERIODIC,
		.irqaction = {
			.flags   = IRQF_TIMER,
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
static u64 read_cycles(struct clocksource *cs)
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
 * Overwrite weak default sched_clock with something more precise
 */
static u64 notrace davinci_read_sched_clock(void)
{
	return timer32_read(&timers[TID_CLOCKSOURCE]);
}

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

static int davinci_shutdown(struct clock_event_device *evt)
{
	struct timer_s *t = &timers[TID_CLOCKEVENT];

	t->opts &= ~TIMER_OPTS_STATE_MASK;
	t->opts |= TIMER_OPTS_DISABLED;
	return 0;
}

static int davinci_set_oneshot(struct clock_event_device *evt)
{
	struct timer_s *t = &timers[TID_CLOCKEVENT];

	t->opts &= ~TIMER_OPTS_STATE_MASK;
	t->opts |= TIMER_OPTS_ONESHOT;
	return 0;
}

static int davinci_set_periodic(struct clock_event_device *evt)
{
	struct timer_s *t = &timers[TID_CLOCKEVENT];

	t->period = davinci_clock_tick_rate / (HZ);
	t->opts &= ~TIMER_OPTS_STATE_MASK;
	t->opts |= TIMER_OPTS_PERIODIC;
	timer32_config(t);
	return 0;
}

static struct clock_event_device clockevent_davinci = {
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= davinci_set_next_event,
	.set_state_shutdown	= davinci_shutdown,
	.set_state_periodic	= davinci_set_periodic,
	.set_state_oneshot	= davinci_set_oneshot,
};


void __init davinci_timer_init(void)
{
	struct clk *timer_clk;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	unsigned int clockevent_id;
	unsigned int clocksource_id;
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
			pr_warn("%s: Invalid use of system timers.  Results unpredictable.\n",
				__func__);
		else if ((dtip[event_timer].cmp_off == 0)
				|| (dtip[event_timer].cmp_irq == 0))
			pr_warn("%s: Invalid timer instance setup.  Results unpredictable.\n",
				__func__);
		else {
			timers[TID_CLOCKEVENT].opts |= TIMER_OPTS_USE_COMPARE;
			clockevent_davinci.features = CLOCK_EVT_FEAT_ONESHOT;
		}
	}

	timer_clk = clk_get(NULL, "timer0");
	BUG_ON(IS_ERR(timer_clk));
	clk_prepare_enable(timer_clk);

	/* init timer hw */
	timer_init();

	davinci_clock_tick_rate = clk_get_rate(timer_clk);

	/* setup clocksource */
	clocksource_davinci.name = id_to_name[clocksource_id];
	if (clocksource_register_hz(&clocksource_davinci,
				    davinci_clock_tick_rate))
		pr_err("%s: can't register clocksource!\n",
		       clocksource_davinci.name);

	sched_clock_register(davinci_read_sched_clock, 32,
			  davinci_clock_tick_rate);

	/* setup clockevent */
	clockevent_davinci.name = id_to_name[timers[TID_CLOCKEVENT].id];

	clockevent_davinci.cpumask = cpumask_of(0);
	clockevents_config_and_register(&clockevent_davinci,
					davinci_clock_tick_rate, 1, 0xfffffffe);

	for (i=0; i< ARRAY_SIZE(timers); i++)
		timer32_config(&timers[i]);
}
