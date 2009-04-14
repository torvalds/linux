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
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/device.h>

#include <mach/hardware.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/errno.h>
#include <mach/io.h>
#include <mach/cputype.h>
#include "clock.h"

static struct clock_event_device clockevent_davinci;
static unsigned int davinci_clock_tick_rate;

#define DAVINCI_TIMER0_BASE (IO_PHYS + 0x21400)
#define DAVINCI_TIMER1_BASE (IO_PHYS + 0x21800)
#define DAVINCI_WDOG_BASE   (IO_PHYS + 0x21C00)

enum {
	T0_BOT = 0, T0_TOP, T1_BOT, T1_TOP, NUM_TIMERS,
};

#define IS_TIMER1(id)    (id & 0x2)
#define IS_TIMER0(id)    (!IS_TIMER1(id))
#define IS_TIMER_TOP(id) ((id & 0x1))
#define IS_TIMER_BOT(id) (!IS_TIMER_TOP(id))

static int timer_irqs[NUM_TIMERS] = {
	IRQ_TINT0_TINT12,
	IRQ_TINT0_TINT34,
	IRQ_TINT1_TINT12,
	IRQ_TINT1_TINT34,
};

/*
 * This driver configures the 2 64-bit count-up timers as 4 independent
 * 32-bit count-up timers used as follows:
 *
 * T0_BOT: Timer 0, bottom:  clockevent source for hrtimers
 * T0_TOP: Timer 0, top   :  clocksource for generic timekeeping
 * T1_BOT: Timer 1, bottom:  (used by DSP in TI DSPLink code)
 * T1_TOP: Timer 1, top   :  <unused>
 */
#define TID_CLOCKEVENT  T0_BOT
#define TID_CLOCKSOURCE T0_TOP

/* Timer register offsets */
#define PID12                        0x0
#define TIM12                        0x10
#define TIM34                        0x14
#define PRD12                        0x18
#define PRD34                        0x1c
#define TCR                          0x20
#define TGCR                         0x24
#define WDTCR                        0x28

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
	void __iomem *base;
	unsigned long tim_off;
	unsigned long prd_off;
	unsigned long enamode_shift;
	struct irqaction irqaction;
};
static struct timer_s timers[];

/* values for 'opts' field of struct timer_s */
#define TIMER_OPTS_DISABLED   0x00
#define TIMER_OPTS_ONESHOT    0x01
#define TIMER_OPTS_PERIODIC   0x02

static int timer32_config(struct timer_s *t)
{
	u32 tcr = __raw_readl(t->base + TCR);

	/* disable timer */
	tcr &= ~(TCR_ENAMODE_MASK << t->enamode_shift);
	__raw_writel(tcr, t->base + TCR);

	/* reset counter to zero, set new period */
	__raw_writel(0, t->base + t->tim_off);
	__raw_writel(t->period, t->base + t->prd_off);

	/* Set enable mode */
	if (t->opts & TIMER_OPTS_ONESHOT) {
		tcr |= TCR_ENAMODE_ONESHOT << t->enamode_shift;
	} else if (t->opts & TIMER_OPTS_PERIODIC) {
		tcr |= TCR_ENAMODE_PERIODIC << t->enamode_shift;
	}

	__raw_writel(tcr, t->base + TCR);
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
	u32 phys_bases[] = {DAVINCI_TIMER0_BASE, DAVINCI_TIMER1_BASE};
	int i;

	/* Global init of each 64-bit timer as a whole */
	for(i=0; i<2; i++) {
		u32 tgcr;
		void __iomem *base = IO_ADDRESS(phys_bases[i]);

		/* Disabled, Internal clock source */
		__raw_writel(0, base + TCR);

		/* reset both timers, no pre-scaler for timer34 */
		tgcr = 0;
		__raw_writel(tgcr, base + TGCR);

		/* Set both timers to unchained 32-bit */
		tgcr = TGCR_TIMMODE_32BIT_UNCHAINED << TGCR_TIMMODE_SHIFT;
		__raw_writel(tgcr, base + TGCR);

		/* Unreset timers */
		tgcr |= (TGCR_UNRESET << TGCR_TIM12RS_SHIFT) |
			(TGCR_UNRESET << TGCR_TIM34RS_SHIFT);
		__raw_writel(tgcr, base + TGCR);

		/* Init both counters to zero */
		__raw_writel(0, base + TIM12);
		__raw_writel(0, base + TIM34);
	}

	/* Init of each timer as a 32-bit timer */
	for (i=0; i< ARRAY_SIZE(timers); i++) {
		struct timer_s *t = &timers[i];
		u32 phys_base;

		if (t->name) {
			t->id = i;
			phys_base = (IS_TIMER1(t->id) ?
			       DAVINCI_TIMER1_BASE : DAVINCI_TIMER0_BASE);
			t->base = IO_ADDRESS(phys_base);

			if (IS_TIMER_BOT(t->id)) {
				t->enamode_shift = 6;
				t->tim_off = TIM12;
				t->prd_off = PRD12;
			} else {
				t->enamode_shift = 22;
				t->tim_off = TIM34;
				t->prd_off = PRD34;
			}

			/* Register interrupt */
			t->irqaction.name = t->name;
			t->irqaction.dev_id = (void *)t;
			if (t->irqaction.handler != NULL) {
				setup_irq(timer_irqs[t->id], &t->irqaction);
			}

			timer32_config(&timers[i]);
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
	.name		= "timer0_1",
	.rating		= 300,
	.read		= read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 24,
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
		t->opts = TIMER_OPTS_PERIODIC;
		timer32_config(t);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		t->opts = TIMER_OPTS_ONESHOT;
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		t->opts = TIMER_OPTS_DISABLED;
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device clockevent_davinci = {
	.name		= "timer0_0",
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_next_event	= davinci_set_next_event,
	.set_mode	= davinci_set_mode,
};


static void __init davinci_timer_init(void)
{
	struct clk *timer_clk;

	static char err[] __initdata = KERN_ERR
		"%s: can't register clocksource!\n";

	/* init timer hw */
	timer_init();

	timer_clk = clk_get(NULL, "timer0");
	BUG_ON(IS_ERR(timer_clk));
	clk_enable(timer_clk);

	davinci_clock_tick_rate = clk_get_rate(timer_clk);

	/* setup clocksource */
	clocksource_davinci.mult =
		clocksource_khz2mult(davinci_clock_tick_rate/1000,
				     clocksource_davinci.shift);
	if (clocksource_register(&clocksource_davinci))
		printk(err, clocksource_davinci.name);

	/* setup clockevent */
	clockevent_davinci.mult = div_sc(davinci_clock_tick_rate, NSEC_PER_SEC,
					 clockevent_davinci.shift);
	clockevent_davinci.max_delta_ns =
		clockevent_delta2ns(0xfffffffe, &clockevent_davinci);
	clockevent_davinci.min_delta_ns =
		clockevent_delta2ns(1, &clockevent_davinci);

	clockevent_davinci.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_davinci);
}

struct sys_timer davinci_timer = {
	.init   = davinci_timer_init,
};


/* reset board using watchdog timer */
void davinci_watchdog_reset(void) {
	u32 tgcr, wdtcr;
	void __iomem *base = IO_ADDRESS(DAVINCI_WDOG_BASE);
	struct device dev;
	struct clk *wd_clk;
	char *name = "watchdog";

	dev_set_name(&dev, name);
	wd_clk = clk_get(&dev, NULL);
	if (WARN_ON(IS_ERR(wd_clk)))
		return;
	clk_enable(wd_clk);

	/* disable, internal clock source */
	__raw_writel(0, base + TCR);

	/* reset timer, set mode to 64-bit watchdog, and unreset */
	tgcr = 0;
	__raw_writel(tgcr, base + TCR);
	tgcr = TGCR_TIMMODE_64BIT_WDOG << TGCR_TIMMODE_SHIFT;
	tgcr |= (TGCR_UNRESET << TGCR_TIM12RS_SHIFT) |
		(TGCR_UNRESET << TGCR_TIM34RS_SHIFT);
	__raw_writel(tgcr, base + TCR);

	/* clear counter and period regs */
	__raw_writel(0, base + TIM12);
	__raw_writel(0, base + TIM34);
	__raw_writel(0, base + PRD12);
	__raw_writel(0, base + PRD34);

	/* enable */
	wdtcr = __raw_readl(base + WDTCR);
	wdtcr |= WDTCR_WDEN_ENABLE << WDTCR_WDEN_SHIFT;
	__raw_writel(wdtcr, base + WDTCR);

	/* put watchdog in pre-active state */
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
