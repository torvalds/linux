// SPDX-License-Identifier: GPL-2.0
/*
 * Faraday Technology FTTMR010 timer driver
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on a rewrite of arch/arm/mach-gemini/timer.c:
 * Copyright (C) 2001-2006 Storlink, Corp.
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/sched_clock.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/delay.h>

/*
 * Register definitions common for all the timer variants.
 */
#define TIMER1_COUNT		(0x00)
#define TIMER1_LOAD		(0x04)
#define TIMER1_MATCH1		(0x08)
#define TIMER1_MATCH2		(0x0c)
#define TIMER2_COUNT		(0x10)
#define TIMER2_LOAD		(0x14)
#define TIMER2_MATCH1		(0x18)
#define TIMER2_MATCH2		(0x1c)
#define TIMER3_COUNT		(0x20)
#define TIMER3_LOAD		(0x24)
#define TIMER3_MATCH1		(0x28)
#define TIMER3_MATCH2		(0x2c)
#define TIMER_CR		(0x30)

/*
 * Control register set to clear for ast2600 only.
 */
#define AST2600_TIMER_CR_CLR	(0x3c)

/*
 * Control register (TMC30) bit fields for fttmr010/gemini/moxart timers.
 */
#define TIMER_1_CR_ENABLE	BIT(0)
#define TIMER_1_CR_CLOCK	BIT(1)
#define TIMER_1_CR_INT		BIT(2)
#define TIMER_2_CR_ENABLE	BIT(3)
#define TIMER_2_CR_CLOCK	BIT(4)
#define TIMER_2_CR_INT		BIT(5)
#define TIMER_3_CR_ENABLE	BIT(6)
#define TIMER_3_CR_CLOCK	BIT(7)
#define TIMER_3_CR_INT		BIT(8)
#define TIMER_1_CR_UPDOWN	BIT(9)
#define TIMER_2_CR_UPDOWN	BIT(10)
#define TIMER_3_CR_UPDOWN	BIT(11)

/*
 * Control register (TMC30) bit fields for aspeed ast2400/ast2500 timers.
 * The aspeed timers move bits around in the control register and lacks
 * bits for setting the timer to count upwards.
 */
#define TIMER_1_CR_ASPEED_ENABLE	BIT(0)
#define TIMER_1_CR_ASPEED_CLOCK		BIT(1)
#define TIMER_1_CR_ASPEED_INT		BIT(2)
#define TIMER_2_CR_ASPEED_ENABLE	BIT(4)
#define TIMER_2_CR_ASPEED_CLOCK		BIT(5)
#define TIMER_2_CR_ASPEED_INT		BIT(6)
#define TIMER_3_CR_ASPEED_ENABLE	BIT(8)
#define TIMER_3_CR_ASPEED_CLOCK		BIT(9)
#define TIMER_3_CR_ASPEED_INT		BIT(10)

/*
 * Interrupt status/mask register definitions for fttmr010/gemini/moxart
 * timers.
 * The registers don't exist and they are not needed on aspeed timers
 * because:
 *   - aspeed timer overflow interrupt is controlled by bits in Control
 *     Register (TMC30).
 *   - aspeed timers always generate interrupt when either one of the
 *     Match registers equals to Status register.
 */
#define TIMER_INTR_STATE	(0x34)
#define TIMER_INTR_MASK		(0x38)
#define TIMER_1_INT_MATCH1	BIT(0)
#define TIMER_1_INT_MATCH2	BIT(1)
#define TIMER_1_INT_OVERFLOW	BIT(2)
#define TIMER_2_INT_MATCH1	BIT(3)
#define TIMER_2_INT_MATCH2	BIT(4)
#define TIMER_2_INT_OVERFLOW	BIT(5)
#define TIMER_3_INT_MATCH1	BIT(6)
#define TIMER_3_INT_MATCH2	BIT(7)
#define TIMER_3_INT_OVERFLOW	BIT(8)
#define TIMER_INT_ALL_MASK	0x1ff

struct fttmr010 {
	void __iomem *base;
	unsigned int tick_rate;
	bool is_aspeed;
	u32 t1_enable_val;
	struct clock_event_device clkevt;
	int (*timer_shutdown)(struct clock_event_device *evt);
#ifdef CONFIG_ARM
	struct delay_timer delay_timer;
#endif
};

/*
 * A local singleton used by sched_clock and delay timer reads, which are
 * fast and stateless
 */
static struct fttmr010 *local_fttmr;

static inline struct fttmr010 *to_fttmr010(struct clock_event_device *evt)
{
	return container_of(evt, struct fttmr010, clkevt);
}

static unsigned long fttmr010_read_current_timer_up(void)
{
	return readl(local_fttmr->base + TIMER2_COUNT);
}

static unsigned long fttmr010_read_current_timer_down(void)
{
	return ~readl(local_fttmr->base + TIMER2_COUNT);
}

static u64 notrace fttmr010_read_sched_clock_up(void)
{
	return fttmr010_read_current_timer_up();
}

static u64 notrace fttmr010_read_sched_clock_down(void)
{
	return fttmr010_read_current_timer_down();
}

static int fttmr010_timer_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	struct fttmr010 *fttmr010 = to_fttmr010(evt);
	u32 cr;

	/* Stop */
	fttmr010->timer_shutdown(evt);

	if (fttmr010->is_aspeed) {
		/*
		 * ASPEED Timer Controller will load TIMER1_LOAD register
		 * into TIMER1_COUNT register when the timer is re-enabled.
		 */
		writel(cycles, fttmr010->base + TIMER1_LOAD);
	} else {
		/* Setup the match register forward in time */
		cr = readl(fttmr010->base + TIMER1_COUNT);
		writel(cr + cycles, fttmr010->base + TIMER1_MATCH1);
	}

	/* Start */
	cr = readl(fttmr010->base + TIMER_CR);
	cr |= fttmr010->t1_enable_val;
	writel(cr, fttmr010->base + TIMER_CR);

	return 0;
}

static int ast2600_timer_shutdown(struct clock_event_device *evt)
{
	struct fttmr010 *fttmr010 = to_fttmr010(evt);

	/* Stop */
	writel(fttmr010->t1_enable_val, fttmr010->base + AST2600_TIMER_CR_CLR);

	return 0;
}

static int fttmr010_timer_shutdown(struct clock_event_device *evt)
{
	struct fttmr010 *fttmr010 = to_fttmr010(evt);
	u32 cr;

	/* Stop */
	cr = readl(fttmr010->base + TIMER_CR);
	cr &= ~fttmr010->t1_enable_val;
	writel(cr, fttmr010->base + TIMER_CR);

	return 0;
}

static int fttmr010_timer_set_oneshot(struct clock_event_device *evt)
{
	struct fttmr010 *fttmr010 = to_fttmr010(evt);
	u32 cr;

	/* Stop */
	fttmr010->timer_shutdown(evt);

	/* Setup counter start from 0 or ~0 */
	writel(0, fttmr010->base + TIMER1_COUNT);
	if (fttmr010->is_aspeed) {
		writel(~0, fttmr010->base + TIMER1_LOAD);
	} else {
		writel(0, fttmr010->base + TIMER1_LOAD);

		/* Enable interrupt */
		cr = readl(fttmr010->base + TIMER_INTR_MASK);
		cr &= ~(TIMER_1_INT_OVERFLOW | TIMER_1_INT_MATCH2);
		cr |= TIMER_1_INT_MATCH1;
		writel(cr, fttmr010->base + TIMER_INTR_MASK);
	}

	return 0;
}

static int fttmr010_timer_set_periodic(struct clock_event_device *evt)
{
	struct fttmr010 *fttmr010 = to_fttmr010(evt);
	u32 period = DIV_ROUND_CLOSEST(fttmr010->tick_rate, HZ);
	u32 cr;

	/* Stop */
	fttmr010->timer_shutdown(evt);

	/* Setup timer to fire at 1/HZ intervals. */
	if (fttmr010->is_aspeed) {
		writel(period, fttmr010->base + TIMER1_LOAD);
	} else {
		cr = 0xffffffff - (period - 1);
		writel(cr, fttmr010->base + TIMER1_COUNT);
		writel(cr, fttmr010->base + TIMER1_LOAD);

		/* Enable interrupt on overflow */
		cr = readl(fttmr010->base + TIMER_INTR_MASK);
		cr &= ~(TIMER_1_INT_MATCH1 | TIMER_1_INT_MATCH2);
		cr |= TIMER_1_INT_OVERFLOW;
		writel(cr, fttmr010->base + TIMER_INTR_MASK);
	}

	/* Start the timer */
	cr = readl(fttmr010->base + TIMER_CR);
	cr |= fttmr010->t1_enable_val;
	writel(cr, fttmr010->base + TIMER_CR);

	return 0;
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t fttmr010_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static irqreturn_t ast2600_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	struct fttmr010 *fttmr010 = to_fttmr010(evt);

	writel(0x1, fttmr010->base + TIMER_INTR_STATE);

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static int __init fttmr010_common_init(struct device_node *np,
		bool is_aspeed,
		int (*timer_shutdown)(struct clock_event_device *),
		irq_handler_t irq_handler)
{
	struct fttmr010 *fttmr010;
	int irq;
	struct clk *clk;
	int ret;
	u32 val;

	/*
	 * These implementations require a clock reference.
	 * FIXME: we currently only support clocking using PCLK
	 * and using EXTCLK is not supported in the driver.
	 */
	clk = of_clk_get_by_name(np, "PCLK");
	if (IS_ERR(clk)) {
		pr_err("could not get PCLK\n");
		return PTR_ERR(clk);
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("failed to enable PCLK\n");
		return ret;
	}

	fttmr010 = kzalloc(sizeof(*fttmr010), GFP_KERNEL);
	if (!fttmr010) {
		ret = -ENOMEM;
		goto out_disable_clock;
	}
	fttmr010->tick_rate = clk_get_rate(clk);

	fttmr010->base = of_iomap(np, 0);
	if (!fttmr010->base) {
		pr_err("Can't remap registers\n");
		ret = -ENXIO;
		goto out_free;
	}
	/* IRQ for timer 1 */
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Can't parse IRQ\n");
		ret = -EINVAL;
		goto out_unmap;
	}

	/*
	 * The Aspeed timers move bits around in the control register.
	 */
	if (is_aspeed) {
		fttmr010->t1_enable_val = TIMER_1_CR_ASPEED_ENABLE |
			TIMER_1_CR_ASPEED_INT;
		fttmr010->is_aspeed = true;
	} else {
		fttmr010->t1_enable_val = TIMER_1_CR_ENABLE | TIMER_1_CR_INT;

		/*
		 * Reset the interrupt mask and status
		 */
		writel(TIMER_INT_ALL_MASK, fttmr010->base + TIMER_INTR_MASK);
		writel(0, fttmr010->base + TIMER_INTR_STATE);
	}

	/*
	 * Enable timer 1 count up, timer 2 count up, except on Aspeed,
	 * where everything just counts down.
	 */
	if (is_aspeed)
		val = TIMER_2_CR_ASPEED_ENABLE;
	else {
		val = TIMER_2_CR_ENABLE | TIMER_1_CR_UPDOWN |
			TIMER_2_CR_UPDOWN;
	}
	writel(val, fttmr010->base + TIMER_CR);

	/*
	 * Setup free-running clocksource timer (interrupts
	 * disabled.)
	 */
	local_fttmr = fttmr010;
	writel(0, fttmr010->base + TIMER2_COUNT);
	writel(0, fttmr010->base + TIMER2_MATCH1);
	writel(0, fttmr010->base + TIMER2_MATCH2);

	if (fttmr010->is_aspeed) {
		writel(~0, fttmr010->base + TIMER2_LOAD);
		clocksource_mmio_init(fttmr010->base + TIMER2_COUNT,
				      "FTTMR010-TIMER2",
				      fttmr010->tick_rate,
				      300, 32, clocksource_mmio_readl_down);
		sched_clock_register(fttmr010_read_sched_clock_down, 32,
				     fttmr010->tick_rate);
	} else {
		writel(0, fttmr010->base + TIMER2_LOAD);
		clocksource_mmio_init(fttmr010->base + TIMER2_COUNT,
				      "FTTMR010-TIMER2",
				      fttmr010->tick_rate,
				      300, 32, clocksource_mmio_readl_up);
		sched_clock_register(fttmr010_read_sched_clock_up, 32,
				     fttmr010->tick_rate);
	}

	fttmr010->timer_shutdown = timer_shutdown;

	/*
	 * Setup clockevent timer (interrupt-driven) on timer 1.
	 */
	writel(0, fttmr010->base + TIMER1_COUNT);
	writel(0, fttmr010->base + TIMER1_LOAD);
	writel(0, fttmr010->base + TIMER1_MATCH1);
	writel(0, fttmr010->base + TIMER1_MATCH2);
	ret = request_irq(irq, irq_handler, IRQF_TIMER,
			  "FTTMR010-TIMER1", &fttmr010->clkevt);
	if (ret) {
		pr_err("FTTMR010-TIMER1 no IRQ\n");
		goto out_unmap;
	}

	fttmr010->clkevt.name = "FTTMR010-TIMER1";
	/* Reasonably fast and accurate clock event */
	fttmr010->clkevt.rating = 300;
	fttmr010->clkevt.features = CLOCK_EVT_FEAT_PERIODIC |
		CLOCK_EVT_FEAT_ONESHOT;
	fttmr010->clkevt.set_next_event = fttmr010_timer_set_next_event;
	fttmr010->clkevt.set_state_shutdown = fttmr010->timer_shutdown;
	fttmr010->clkevt.set_state_periodic = fttmr010_timer_set_periodic;
	fttmr010->clkevt.set_state_oneshot = fttmr010_timer_set_oneshot;
	fttmr010->clkevt.tick_resume = fttmr010->timer_shutdown;
	fttmr010->clkevt.cpumask = cpumask_of(0);
	fttmr010->clkevt.irq = irq;
	clockevents_config_and_register(&fttmr010->clkevt,
					fttmr010->tick_rate,
					1, 0xffffffff);

#ifdef CONFIG_ARM
	/* Also use this timer for delays */
	if (fttmr010->is_aspeed)
		fttmr010->delay_timer.read_current_timer =
			fttmr010_read_current_timer_down;
	else
		fttmr010->delay_timer.read_current_timer =
			fttmr010_read_current_timer_up;
	fttmr010->delay_timer.freq = fttmr010->tick_rate;
	register_current_timer_delay(&fttmr010->delay_timer);
#endif

	return 0;

out_unmap:
	iounmap(fttmr010->base);
out_free:
	kfree(fttmr010);
out_disable_clock:
	clk_disable_unprepare(clk);

	return ret;
}

static __init int ast2600_timer_init(struct device_node *np)
{
	return fttmr010_common_init(np, true,
			ast2600_timer_shutdown,
			ast2600_timer_interrupt);
}

static __init int aspeed_timer_init(struct device_node *np)
{
	return fttmr010_common_init(np, true,
			fttmr010_timer_shutdown,
			fttmr010_timer_interrupt);
}

static __init int fttmr010_timer_init(struct device_node *np)
{
	return fttmr010_common_init(np, false,
			fttmr010_timer_shutdown,
			fttmr010_timer_interrupt);
}

TIMER_OF_DECLARE(fttmr010, "faraday,fttmr010", fttmr010_timer_init);
TIMER_OF_DECLARE(gemini, "cortina,gemini-timer", fttmr010_timer_init);
TIMER_OF_DECLARE(moxart, "moxa,moxart-timer", fttmr010_timer_init);
TIMER_OF_DECLARE(ast2400, "aspeed,ast2400-timer", aspeed_timer_init);
TIMER_OF_DECLARE(ast2500, "aspeed,ast2500-timer", aspeed_timer_init);
TIMER_OF_DECLARE(ast2600, "aspeed,ast2600-timer", ast2600_timer_init);
