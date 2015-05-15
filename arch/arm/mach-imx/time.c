/*
 *  linux/arch/arm/plat-mxc/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright (C) 2006-2007 Pavel Pisa (ppisa@pikron.com)
 *  Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <soc/imx/timer.h>

#include <asm/mach/time.h>

#include "common.h"
#include "hardware.h"

/*
 * There are 4 versions of the timer hardware on Freescale MXC hardware.
 *  - MX1/MXL
 *  - MX21, MX27.
 *  - MX25, MX31, MX35, MX37, MX51, MX6Q(rev1.0)
 *  - MX6DL, MX6SX, MX6Q(rev1.1+)
 */

/* defines common for all i.MX */
#define MXC_TCTL		0x00
#define MXC_TCTL_TEN		(1 << 0) /* Enable module */
#define MXC_TPRER		0x04

/* MX1, MX21, MX27 */
#define MX1_2_TCTL_CLK_PCLK1	(1 << 1)
#define MX1_2_TCTL_IRQEN	(1 << 4)
#define MX1_2_TCTL_FRR		(1 << 8)
#define MX1_2_TCMP		0x08
#define MX1_2_TCN		0x10
#define MX1_2_TSTAT		0x14

/* MX21, MX27 */
#define MX2_TSTAT_CAPT		(1 << 1)
#define MX2_TSTAT_COMP		(1 << 0)

/* MX31, MX35, MX25, MX5, MX6 */
#define V2_TCTL_WAITEN		(1 << 3) /* Wait enable mode */
#define V2_TCTL_CLK_IPG		(1 << 6)
#define V2_TCTL_CLK_PER		(2 << 6)
#define V2_TCTL_CLK_OSC_DIV8	(5 << 6)
#define V2_TCTL_FRR		(1 << 9)
#define V2_TCTL_24MEN		(1 << 10)
#define V2_TPRER_PRE24M		12
#define V2_IR			0x0c
#define V2_TSTAT		0x08
#define V2_TSTAT_OF1		(1 << 0)
#define V2_TCN			0x24
#define V2_TCMP			0x10

#define V2_TIMER_RATE_OSC_DIV8	3000000

#define timer_is_v1()	(cpu_is_mx1() || cpu_is_mx21() || cpu_is_mx27())
#define timer_is_v2()	(!timer_is_v1())

static struct clock_event_device clockevent_mxc;
static enum clock_event_mode clockevent_mode = CLOCK_EVT_MODE_UNUSED;

struct imx_timer {
	enum imx_gpt_type type;
	void __iomem *base;
	int irq;
	struct clk *clk_per;
	struct clk *clk_ipg;
};

static void __iomem *timer_base;

static inline void gpt_irq_disable(void)
{
	unsigned int tmp;

	if (timer_is_v2())
		writel_relaxed(0, timer_base + V2_IR);
	else {
		tmp = readl_relaxed(timer_base + MXC_TCTL);
		writel_relaxed(tmp & ~MX1_2_TCTL_IRQEN, timer_base + MXC_TCTL);
	}
}

static inline void gpt_irq_enable(void)
{
	if (timer_is_v2())
		writel_relaxed(1<<0, timer_base + V2_IR);
	else {
		writel_relaxed(readl_relaxed(timer_base + MXC_TCTL) | MX1_2_TCTL_IRQEN,
			timer_base + MXC_TCTL);
	}
}

static void gpt_irq_acknowledge(void)
{
	if (timer_is_v1()) {
		if (cpu_is_mx1())
			writel_relaxed(0, timer_base + MX1_2_TSTAT);
		else
			writel_relaxed(MX2_TSTAT_CAPT | MX2_TSTAT_COMP,
				timer_base + MX1_2_TSTAT);
	} else if (timer_is_v2())
		writel_relaxed(V2_TSTAT_OF1, timer_base + V2_TSTAT);
}

static void __iomem *sched_clock_reg;

static u64 notrace mxc_read_sched_clock(void)
{
	return sched_clock_reg ? readl_relaxed(sched_clock_reg) : 0;
}

static struct delay_timer imx_delay_timer;

static unsigned long imx_read_current_timer(void)
{
	return readl_relaxed(sched_clock_reg);
}

static int __init mxc_clocksource_init(struct imx_timer *imxtm)
{
	unsigned int c = clk_get_rate(imxtm->clk_per);
	void __iomem *reg = imxtm->base + (timer_is_v2() ? V2_TCN : MX1_2_TCN);

	imx_delay_timer.read_current_timer = &imx_read_current_timer;
	imx_delay_timer.freq = c;
	register_current_timer_delay(&imx_delay_timer);

	sched_clock_reg = reg;

	sched_clock_register(mxc_read_sched_clock, 32, c);
	return clocksource_mmio_init(reg, "mxc_timer1", c, 200, 32,
			clocksource_mmio_readl_up);
}

/* clock event */

static int mx1_2_set_next_event(unsigned long evt,
			      struct clock_event_device *unused)
{
	unsigned long tcmp;

	tcmp = readl_relaxed(timer_base + MX1_2_TCN) + evt;

	writel_relaxed(tcmp, timer_base + MX1_2_TCMP);

	return (int)(tcmp - readl_relaxed(timer_base + MX1_2_TCN)) < 0 ?
				-ETIME : 0;
}

static int v2_set_next_event(unsigned long evt,
			      struct clock_event_device *unused)
{
	unsigned long tcmp;

	tcmp = readl_relaxed(timer_base + V2_TCN) + evt;

	writel_relaxed(tcmp, timer_base + V2_TCMP);

	return evt < 0x7fffffff &&
		(int)(tcmp - readl_relaxed(timer_base + V2_TCN)) < 0 ?
				-ETIME : 0;
}

#ifdef DEBUG
static const char *clock_event_mode_label[] = {
	[CLOCK_EVT_MODE_PERIODIC] = "CLOCK_EVT_MODE_PERIODIC",
	[CLOCK_EVT_MODE_ONESHOT]  = "CLOCK_EVT_MODE_ONESHOT",
	[CLOCK_EVT_MODE_SHUTDOWN] = "CLOCK_EVT_MODE_SHUTDOWN",
	[CLOCK_EVT_MODE_UNUSED]   = "CLOCK_EVT_MODE_UNUSED",
	[CLOCK_EVT_MODE_RESUME]   = "CLOCK_EVT_MODE_RESUME",
};
#endif /* DEBUG */

static void mxc_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	unsigned long flags;

	/*
	 * The timer interrupt generation is disabled at least
	 * for enough time to call mxc_set_next_event()
	 */
	local_irq_save(flags);

	/* Disable interrupt in GPT module */
	gpt_irq_disable();

	if (mode != clockevent_mode) {
		/* Set event time into far-far future */
		if (timer_is_v2())
			writel_relaxed(readl_relaxed(timer_base + V2_TCN) - 3,
					timer_base + V2_TCMP);
		else
			writel_relaxed(readl_relaxed(timer_base + MX1_2_TCN) - 3,
					timer_base + MX1_2_TCMP);

		/* Clear pending interrupt */
		gpt_irq_acknowledge();
	}

#ifdef DEBUG
	printk(KERN_INFO "mxc_set_mode: changing mode from %s to %s\n",
		clock_event_mode_label[clockevent_mode],
		clock_event_mode_label[mode]);
#endif /* DEBUG */

	/* Remember timer mode */
	clockevent_mode = mode;
	local_irq_restore(flags);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		printk(KERN_ERR"mxc_set_mode: Periodic mode is not "
				"supported for i.MX\n");
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	/*
	 * Do not put overhead of interrupt enable/disable into
	 * mxc_set_next_event(), the core has about 4 minutes
	 * to call mxc_set_next_event() or shutdown clock after
	 * mode switching
	 */
		local_irq_save(flags);
		gpt_irq_enable();
		local_irq_restore(flags);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_RESUME:
		/* Left event sources disabled, no more interrupts appear */
		break;
	}
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t mxc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_mxc;
	uint32_t tstat;

	if (timer_is_v2())
		tstat = readl_relaxed(timer_base + V2_TSTAT);
	else
		tstat = readl_relaxed(timer_base + MX1_2_TSTAT);

	gpt_irq_acknowledge();

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction mxc_timer_irq = {
	.name		= "i.MX Timer Tick",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= mxc_timer_interrupt,
};

static struct clock_event_device clockevent_mxc = {
	.name		= "mxc_timer1",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= mxc_set_mode,
	.set_next_event	= mx1_2_set_next_event,
	.rating		= 200,
};

static int __init mxc_clockevent_init(struct imx_timer *imxtm)
{
	if (timer_is_v2())
		clockevent_mxc.set_next_event = v2_set_next_event;

	clockevent_mxc.cpumask = cpumask_of(0);
	clockevents_config_and_register(&clockevent_mxc,
					clk_get_rate(imxtm->clk_per),
					0xff, 0xfffffffe);

	return 0;
}

static void __init _mxc_timer_init(struct imx_timer *imxtm)
{
	uint32_t tctl_val;

	/* Temporary */
	timer_base = imxtm->base;

	if (IS_ERR(imxtm->clk_per)) {
		pr_err("i.MX timer: unable to get clk\n");
		return;
	}

	if (!IS_ERR(imxtm->clk_ipg))
		clk_prepare_enable(imxtm->clk_ipg);

	clk_prepare_enable(imxtm->clk_per);

	/*
	 * Initialise to a known state (all timers off, and timing reset)
	 */

	writel_relaxed(0, imxtm->base + MXC_TCTL);
	writel_relaxed(0, imxtm->base + MXC_TPRER); /* see datasheet note */

	if (timer_is_v2()) {
		tctl_val = V2_TCTL_FRR | V2_TCTL_WAITEN | MXC_TCTL_TEN;
		if (clk_get_rate(imxtm->clk_per) == V2_TIMER_RATE_OSC_DIV8) {
			tctl_val |= V2_TCTL_CLK_OSC_DIV8;
			if (cpu_is_imx6dl() || cpu_is_imx6sx()) {
				/* 24 / 8 = 3 MHz */
				writel_relaxed(7 << V2_TPRER_PRE24M,
					imxtm->base + MXC_TPRER);
				tctl_val |= V2_TCTL_24MEN;
			}
		} else {
			tctl_val |= V2_TCTL_CLK_PER;
		}
	} else {
		tctl_val = MX1_2_TCTL_FRR | MX1_2_TCTL_CLK_PCLK1 | MXC_TCTL_TEN;
	}

	writel_relaxed(tctl_val, imxtm->base + MXC_TCTL);

	/* init and register the timer to the framework */
	mxc_clocksource_init(imxtm);
	mxc_clockevent_init(imxtm);

	/* Make irqs happen */
	setup_irq(imxtm->irq, &mxc_timer_irq);
}

void __init mxc_timer_init(unsigned long pbase, int irq, enum imx_gpt_type type)
{
	struct imx_timer *imxtm;

	imxtm = kzalloc(sizeof(*imxtm), GFP_KERNEL);
	BUG_ON(!imxtm);

	imxtm->clk_per = clk_get_sys("imx-gpt.0", "per");
	imxtm->clk_ipg = clk_get_sys("imx-gpt.0", "ipg");

	imxtm->base = ioremap(pbase, SZ_4K);
	BUG_ON(!imxtm->base);

	imxtm->type = type;

	_mxc_timer_init(imxtm);
}

static void __init mxc_timer_init_dt(struct device_node *np,  enum imx_gpt_type type)
{
	struct imx_timer *imxtm;
	static int initialized;

	/* Support one instance only */
	if (initialized)
		return;

	imxtm = kzalloc(sizeof(*imxtm), GFP_KERNEL);
	BUG_ON(!imxtm);

	imxtm->base = of_iomap(np, 0);
	WARN_ON(!imxtm->base);
	imxtm->irq = irq_of_parse_and_map(np, 0);

	imxtm->clk_ipg = of_clk_get_by_name(np, "ipg");

	/* Try osc_per first, and fall back to per otherwise */
	imxtm->clk_per = of_clk_get_by_name(np, "osc_per");
	if (IS_ERR(imxtm->clk_per))
		imxtm->clk_per = of_clk_get_by_name(np, "per");

	imxtm->type = type;

	_mxc_timer_init(imxtm);

	initialized = 1;
}

static void __init imx1_timer_init_dt(struct device_node *np)
{
	mxc_timer_init_dt(np, GPT_TYPE_IMX1);
}

static void __init imx21_timer_init_dt(struct device_node *np)
{
	mxc_timer_init_dt(np, GPT_TYPE_IMX21);
}

static void __init imx31_timer_init_dt(struct device_node *np)
{
	enum imx_gpt_type type = GPT_TYPE_IMX31;

	/*
	 * We were using the same compatible string for i.MX6Q/D and i.MX6DL/S
	 * GPT device, while they actually have different programming model.
	 * This is a workaround to keep the existing i.MX6DL/S DTBs continue
	 * working with the new kernel.
	 */
	if (of_machine_is_compatible("fsl,imx6dl"))
		type = GPT_TYPE_IMX6DL;

	mxc_timer_init_dt(np, type);
}

static void __init imx6dl_timer_init_dt(struct device_node *np)
{
	mxc_timer_init_dt(np, GPT_TYPE_IMX6DL);
}

CLOCKSOURCE_OF_DECLARE(imx1_timer, "fsl,imx1-gpt", imx1_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx21_timer, "fsl,imx21-gpt", imx21_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx31_timer, "fsl,imx31-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx25_timer, "fsl,imx25-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx50_timer, "fsl,imx50-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx51_timer, "fsl,imx51-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx53_timer, "fsl,imx53-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6q_timer, "fsl,imx6q-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6dl_timer, "fsl,imx6dl-gpt", imx6dl_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6sl_timer, "fsl,imx6sl-gpt", imx6dl_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6sx_timer, "fsl,imx6sx-gpt", imx6dl_timer_init_dt);
