// SPDX-License-Identifier: GPL-2.0
/*
 * 64-bit Periodic Interval Timer driver
 *
 * Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Claudiu Beznea <claudiu.beznea@microchip.com>
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

#define MCHP_PIT64B_CR			0x00	/* Control Register */
#define MCHP_PIT64B_CR_START		BIT(0)
#define MCHP_PIT64B_CR_SWRST		BIT(8)

#define MCHP_PIT64B_MR			0x04	/* Mode Register */
#define MCHP_PIT64B_MR_CONT		BIT(0)
#define MCHP_PIT64B_MR_ONE_SHOT		(0)
#define MCHP_PIT64B_MR_SGCLK		BIT(3)
#define MCHP_PIT64B_MR_PRES		GENMASK(11, 8)

#define MCHP_PIT64B_LSB_PR		0x08	/* LSB Period Register */

#define MCHP_PIT64B_MSB_PR		0x0C	/* MSB Period Register */

#define MCHP_PIT64B_IER			0x10	/* Interrupt Enable Register */
#define MCHP_PIT64B_IER_PERIOD		BIT(0)

#define MCHP_PIT64B_ISR			0x1C	/* Interrupt Status Register */

#define MCHP_PIT64B_TLSBR		0x20	/* Timer LSB Register */

#define MCHP_PIT64B_TMSBR		0x24	/* Timer MSB Register */

#define MCHP_PIT64B_PRES_MAX		0x10
#define MCHP_PIT64B_LSBMASK		GENMASK_ULL(31, 0)
#define MCHP_PIT64B_PRES_TO_MODE(p)	(MCHP_PIT64B_MR_PRES & ((p) << 8))
#define MCHP_PIT64B_MODE_TO_PRES(m)	((MCHP_PIT64B_MR_PRES & (m)) >> 8)
#define MCHP_PIT64B_DEF_FREQ		5000000UL	/* 5 MHz */

#define MCHP_PIT64B_NAME		"pit64b"

/**
 * struct mchp_pit64b_timer - PIT64B timer data structure
 * @base: base address of PIT64B hardware block
 * @pclk: PIT64B's peripheral clock
 * @gclk: PIT64B's generic clock
 * @mode: precomputed value for mode register
 */
struct mchp_pit64b_timer {
	void __iomem	*base;
	struct clk	*pclk;
	struct clk	*gclk;
	u32		mode;
};

/**
 * mchp_pit64b_clkevt - PIT64B clockevent data structure
 * @timer: PIT64B timer
 * @clkevt: clockevent
 */
struct mchp_pit64b_clkevt {
	struct mchp_pit64b_timer	timer;
	struct clock_event_device	clkevt;
};

#define clkevt_to_mchp_pit64b_timer(x) \
	((struct mchp_pit64b_timer *)container_of(x,\
		struct mchp_pit64b_clkevt, clkevt))

/**
 * mchp_pit64b_clksrc - PIT64B clocksource data structure
 * @timer: PIT64B timer
 * @clksrc: clocksource
 */
struct mchp_pit64b_clksrc {
	struct mchp_pit64b_timer	timer;
	struct clocksource		clksrc;
};

#define clksrc_to_mchp_pit64b_timer(x) \
	((struct mchp_pit64b_timer *)container_of(x,\
		struct mchp_pit64b_clksrc, clksrc))

/* Base address for clocksource timer. */
static void __iomem *mchp_pit64b_cs_base;
/* Default cycles for clockevent timer. */
static u64 mchp_pit64b_ce_cycles;

static inline u64 mchp_pit64b_cnt_read(void __iomem *base)
{
	unsigned long	flags;
	u32		low, high;

	raw_local_irq_save(flags);

	/*
	 * When using a 64 bit period TLSB must be read first, followed by the
	 * read of TMSB. This sequence generates an atomic read of the 64 bit
	 * timer value whatever the lapse of time between the accesses.
	 */
	low = readl_relaxed(base + MCHP_PIT64B_TLSBR);
	high = readl_relaxed(base + MCHP_PIT64B_TMSBR);

	raw_local_irq_restore(flags);

	return (((u64)high << 32) | low);
}

static inline void mchp_pit64b_reset(struct mchp_pit64b_timer *timer,
				     u64 cycles, u32 mode, u32 irqs)
{
	u32 low, high;

	low = cycles & MCHP_PIT64B_LSBMASK;
	high = cycles >> 32;

	writel_relaxed(MCHP_PIT64B_CR_SWRST, timer->base + MCHP_PIT64B_CR);
	writel_relaxed(mode | timer->mode, timer->base + MCHP_PIT64B_MR);
	writel_relaxed(high, timer->base + MCHP_PIT64B_MSB_PR);
	writel_relaxed(low, timer->base + MCHP_PIT64B_LSB_PR);
	writel_relaxed(irqs, timer->base + MCHP_PIT64B_IER);
	writel_relaxed(MCHP_PIT64B_CR_START, timer->base + MCHP_PIT64B_CR);
}

static void mchp_pit64b_suspend(struct mchp_pit64b_timer *timer)
{
	writel_relaxed(MCHP_PIT64B_CR_SWRST, timer->base + MCHP_PIT64B_CR);
	if (timer->mode & MCHP_PIT64B_MR_SGCLK)
		clk_disable_unprepare(timer->gclk);
	clk_disable_unprepare(timer->pclk);
}

static void mchp_pit64b_resume(struct mchp_pit64b_timer *timer)
{
	clk_prepare_enable(timer->pclk);
	if (timer->mode & MCHP_PIT64B_MR_SGCLK)
		clk_prepare_enable(timer->gclk);
}

static void mchp_pit64b_clksrc_suspend(struct clocksource *cs)
{
	struct mchp_pit64b_timer *timer = clksrc_to_mchp_pit64b_timer(cs);

	mchp_pit64b_suspend(timer);
}

static void mchp_pit64b_clksrc_resume(struct clocksource *cs)
{
	struct mchp_pit64b_timer *timer = clksrc_to_mchp_pit64b_timer(cs);

	mchp_pit64b_resume(timer);
	mchp_pit64b_reset(timer, ULLONG_MAX, MCHP_PIT64B_MR_CONT, 0);
}

static u64 mchp_pit64b_clksrc_read(struct clocksource *cs)
{
	return mchp_pit64b_cnt_read(mchp_pit64b_cs_base);
}

static u64 notrace mchp_pit64b_sched_read_clk(void)
{
	return mchp_pit64b_cnt_read(mchp_pit64b_cs_base);
}

static int mchp_pit64b_clkevt_shutdown(struct clock_event_device *cedev)
{
	struct mchp_pit64b_timer *timer = clkevt_to_mchp_pit64b_timer(cedev);

	writel_relaxed(MCHP_PIT64B_CR_SWRST, timer->base + MCHP_PIT64B_CR);

	return 0;
}

static int mchp_pit64b_clkevt_set_periodic(struct clock_event_device *cedev)
{
	struct mchp_pit64b_timer *timer = clkevt_to_mchp_pit64b_timer(cedev);

	mchp_pit64b_reset(timer, mchp_pit64b_ce_cycles, MCHP_PIT64B_MR_CONT,
			  MCHP_PIT64B_IER_PERIOD);

	return 0;
}

static int mchp_pit64b_clkevt_set_next_event(unsigned long evt,
					     struct clock_event_device *cedev)
{
	struct mchp_pit64b_timer *timer = clkevt_to_mchp_pit64b_timer(cedev);

	mchp_pit64b_reset(timer, evt, MCHP_PIT64B_MR_ONE_SHOT,
			  MCHP_PIT64B_IER_PERIOD);

	return 0;
}

static void mchp_pit64b_clkevt_suspend(struct clock_event_device *cedev)
{
	struct mchp_pit64b_timer *timer = clkevt_to_mchp_pit64b_timer(cedev);

	mchp_pit64b_suspend(timer);
}

static void mchp_pit64b_clkevt_resume(struct clock_event_device *cedev)
{
	struct mchp_pit64b_timer *timer = clkevt_to_mchp_pit64b_timer(cedev);

	mchp_pit64b_resume(timer);
}

static irqreturn_t mchp_pit64b_interrupt(int irq, void *dev_id)
{
	struct mchp_pit64b_clkevt *irq_data = dev_id;

	/* Need to clear the interrupt. */
	readl_relaxed(irq_data->timer.base + MCHP_PIT64B_ISR);

	irq_data->clkevt.event_handler(&irq_data->clkevt);

	return IRQ_HANDLED;
}

static void __init mchp_pit64b_pres_compute(u32 *pres, u32 clk_rate,
					    u32 max_rate)
{
	u32 tmp;

	for (*pres = 0; *pres < MCHP_PIT64B_PRES_MAX; (*pres)++) {
		tmp = clk_rate / (*pres + 1);
		if (tmp <= max_rate)
			break;
	}

	/* Use the biggest prescaler if we didn't match one. */
	if (*pres == MCHP_PIT64B_PRES_MAX)
		*pres = MCHP_PIT64B_PRES_MAX - 1;
}

/**
 * mchp_pit64b_init_mode - prepare PIT64B mode register value to be used at
 *			   runtime; this includes prescaler and SGCLK bit
 *
 * PIT64B timer may be fed by gclk or pclk. When gclk is used its rate has to
 * be at least 3 times lower that pclk's rate. pclk rate is fixed, gclk rate
 * could be changed via clock APIs. The chosen clock (pclk or gclk) could be
 * divided by the internal PIT64B's divider.
 *
 * This function, first tries to use GCLK by requesting the desired rate from
 * PMC and then using the internal PIT64B prescaler, if any, to reach the
 * requested rate. If PCLK/GCLK < 3 (condition requested by PIT64B hardware)
 * then the function falls back on using PCLK as clock source for PIT64B timer
 * choosing the highest prescaler in case it doesn't locate one to match the
 * requested frequency.
 *
 * Below is presented the PIT64B block in relation with PMC:
 *
 *                                PIT64B
 *  PMC             +------------------------------------+
 * +----+           |   +-----+                          |
 * |    |-->gclk -->|-->|     |    +---------+  +-----+  |
 * |    |           |   | MUX |--->| Divider |->|timer|  |
 * |    |-->pclk -->|-->|     |    +---------+  +-----+  |
 * +----+           |   +-----+                          |
 *                  |      ^                             |
 *                  |     sel                            |
 *                  +------------------------------------+
 *
 * Where:
 *	- gclk rate <= pclk rate/3
 *	- gclk rate could be requested from PMC
 *	- pclk rate is fixed (cannot be requested from PMC)
 */
static int __init mchp_pit64b_init_mode(struct mchp_pit64b_timer *timer,
					unsigned long max_rate)
{
	unsigned long pclk_rate, diff = 0, best_diff = ULONG_MAX;
	long gclk_round = 0;
	u32 pres, best_pres = 0;

	pclk_rate = clk_get_rate(timer->pclk);
	if (!pclk_rate)
		return -EINVAL;

	timer->mode = 0;

	/* Try using GCLK. */
	gclk_round = clk_round_rate(timer->gclk, max_rate);
	if (gclk_round < 0)
		goto pclk;

	if (pclk_rate / gclk_round < 3)
		goto pclk;

	mchp_pit64b_pres_compute(&pres, gclk_round, max_rate);
	best_diff = abs(gclk_round / (pres + 1) - max_rate);
	best_pres = pres;

	if (!best_diff) {
		timer->mode |= MCHP_PIT64B_MR_SGCLK;
		clk_set_rate(timer->gclk, gclk_round);
		goto done;
	}

pclk:
	/* Check if requested rate could be obtained using PCLK. */
	mchp_pit64b_pres_compute(&pres, pclk_rate, max_rate);
	diff = abs(pclk_rate / (pres + 1) - max_rate);

	if (best_diff > diff) {
		/* Use PCLK. */
		best_pres = pres;
	} else {
		/* Use GCLK. */
		timer->mode |= MCHP_PIT64B_MR_SGCLK;
		clk_set_rate(timer->gclk, gclk_round);
	}

done:
	timer->mode |= MCHP_PIT64B_PRES_TO_MODE(best_pres);

	pr_info("PIT64B: using clk=%s with prescaler %u, freq=%lu [Hz]\n",
		timer->mode & MCHP_PIT64B_MR_SGCLK ? "gclk" : "pclk", best_pres,
		timer->mode & MCHP_PIT64B_MR_SGCLK ?
		gclk_round / (best_pres + 1) : pclk_rate / (best_pres + 1));

	return 0;
}

static int __init mchp_pit64b_init_clksrc(struct mchp_pit64b_timer *timer,
					  u32 clk_rate)
{
	struct mchp_pit64b_clksrc *cs;
	int ret;

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	mchp_pit64b_reset(timer, ULLONG_MAX, MCHP_PIT64B_MR_CONT, 0);

	mchp_pit64b_cs_base = timer->base;

	cs->timer.base = timer->base;
	cs->timer.pclk = timer->pclk;
	cs->timer.gclk = timer->gclk;
	cs->timer.mode = timer->mode;
	cs->clksrc.name = MCHP_PIT64B_NAME;
	cs->clksrc.mask = CLOCKSOURCE_MASK(64);
	cs->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	cs->clksrc.rating = 210;
	cs->clksrc.read = mchp_pit64b_clksrc_read;
	cs->clksrc.suspend = mchp_pit64b_clksrc_suspend;
	cs->clksrc.resume = mchp_pit64b_clksrc_resume;

	ret = clocksource_register_hz(&cs->clksrc, clk_rate);
	if (ret) {
		pr_debug("clksrc: Failed to register PIT64B clocksource!\n");

		/* Stop timer. */
		writel_relaxed(MCHP_PIT64B_CR_SWRST,
			       timer->base + MCHP_PIT64B_CR);
		kfree(cs);

		return ret;
	}

	sched_clock_register(mchp_pit64b_sched_read_clk, 64, clk_rate);

	return 0;
}

static int __init mchp_pit64b_init_clkevt(struct mchp_pit64b_timer *timer,
					  u32 clk_rate, u32 irq)
{
	struct mchp_pit64b_clkevt *ce;
	int ret;

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	mchp_pit64b_ce_cycles = DIV_ROUND_CLOSEST(clk_rate, HZ);

	ce->timer.base = timer->base;
	ce->timer.pclk = timer->pclk;
	ce->timer.gclk = timer->gclk;
	ce->timer.mode = timer->mode;
	ce->clkevt.name = MCHP_PIT64B_NAME;
	ce->clkevt.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC;
	ce->clkevt.rating = 150;
	ce->clkevt.set_state_shutdown = mchp_pit64b_clkevt_shutdown;
	ce->clkevt.set_state_periodic = mchp_pit64b_clkevt_set_periodic;
	ce->clkevt.set_next_event = mchp_pit64b_clkevt_set_next_event;
	ce->clkevt.suspend = mchp_pit64b_clkevt_suspend;
	ce->clkevt.resume = mchp_pit64b_clkevt_resume;
	ce->clkevt.cpumask = cpumask_of(0);
	ce->clkevt.irq = irq;

	ret = request_irq(irq, mchp_pit64b_interrupt, IRQF_TIMER,
			  "pit64b_tick", ce);
	if (ret) {
		pr_debug("clkevt: Failed to setup PIT64B IRQ\n");
		kfree(ce);
		return ret;
	}

	clockevents_config_and_register(&ce->clkevt, clk_rate, 1, ULONG_MAX);

	return 0;
}

static int __init mchp_pit64b_dt_init_timer(struct device_node *node,
					    bool clkevt)
{
	struct mchp_pit64b_timer timer;
	unsigned long clk_rate;
	u32 irq = 0;
	int ret;

	/* Parse DT node. */
	timer.pclk = of_clk_get_by_name(node, "pclk");
	if (IS_ERR(timer.pclk))
		return PTR_ERR(timer.pclk);

	timer.gclk = of_clk_get_by_name(node, "gclk");
	if (IS_ERR(timer.gclk))
		return PTR_ERR(timer.gclk);

	timer.base = of_iomap(node, 0);
	if (!timer.base)
		return -ENXIO;

	if (clkevt) {
		irq = irq_of_parse_and_map(node, 0);
		if (!irq) {
			ret = -ENODEV;
			goto io_unmap;
		}
	}

	/* Initialize mode (prescaler + SGCK bit). To be used at runtime. */
	ret = mchp_pit64b_init_mode(&timer, MCHP_PIT64B_DEF_FREQ);
	if (ret)
		goto irq_unmap;

	ret = clk_prepare_enable(timer.pclk);
	if (ret)
		goto irq_unmap;

	if (timer.mode & MCHP_PIT64B_MR_SGCLK) {
		ret = clk_prepare_enable(timer.gclk);
		if (ret)
			goto pclk_unprepare;

		clk_rate = clk_get_rate(timer.gclk);
	} else {
		clk_rate = clk_get_rate(timer.pclk);
	}
	clk_rate = clk_rate / (MCHP_PIT64B_MODE_TO_PRES(timer.mode) + 1);

	if (clkevt)
		ret = mchp_pit64b_init_clkevt(&timer, clk_rate, irq);
	else
		ret = mchp_pit64b_init_clksrc(&timer, clk_rate);

	if (ret)
		goto gclk_unprepare;

	return 0;

gclk_unprepare:
	if (timer.mode & MCHP_PIT64B_MR_SGCLK)
		clk_disable_unprepare(timer.gclk);
pclk_unprepare:
	clk_disable_unprepare(timer.pclk);
irq_unmap:
	irq_dispose_mapping(irq);
io_unmap:
	iounmap(timer.base);

	return ret;
}

static int __init mchp_pit64b_dt_init(struct device_node *node)
{
	static int inits;

	switch (inits++) {
	case 0:
		/* 1st request, register clockevent. */
		return mchp_pit64b_dt_init_timer(node, true);
	case 1:
		/* 2nd request, register clocksource. */
		return mchp_pit64b_dt_init_timer(node, false);
	}

	/* The rest, don't care. */
	return -EINVAL;
}

TIMER_OF_DECLARE(mchp_pit64b, "microchip,sam9x60-pit64b", mchp_pit64b_dt_init);
