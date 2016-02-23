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

struct imx_timer {
	enum imx_gpt_type type;
	void __iomem *base;
	int irq;
	struct clk *clk_per;
	struct clk *clk_ipg;
	const struct imx_gpt_data *gpt;
	struct clock_event_device ced;
	struct irqaction act;
};

struct imx_gpt_data {
	int reg_tstat;
	int reg_tcn;
	int reg_tcmp;
	void (*gpt_setup_tctl)(struct imx_timer *imxtm);
	void (*gpt_irq_enable)(struct imx_timer *imxtm);
	void (*gpt_irq_disable)(struct imx_timer *imxtm);
	void (*gpt_irq_acknowledge)(struct imx_timer *imxtm);
	int (*set_next_event)(unsigned long evt,
			      struct clock_event_device *ced);
};

static inline struct imx_timer *to_imx_timer(struct clock_event_device *ced)
{
	return container_of(ced, struct imx_timer, ced);
}

static void imx1_gpt_irq_disable(struct imx_timer *imxtm)
{
	unsigned int tmp;

	tmp = readl_relaxed(imxtm->base + MXC_TCTL);
	writel_relaxed(tmp & ~MX1_2_TCTL_IRQEN, imxtm->base + MXC_TCTL);
}
#define imx21_gpt_irq_disable imx1_gpt_irq_disable

static void imx31_gpt_irq_disable(struct imx_timer *imxtm)
{
	writel_relaxed(0, imxtm->base + V2_IR);
}
#define imx6dl_gpt_irq_disable imx31_gpt_irq_disable

static void imx1_gpt_irq_enable(struct imx_timer *imxtm)
{
	unsigned int tmp;

	tmp = readl_relaxed(imxtm->base + MXC_TCTL);
	writel_relaxed(tmp | MX1_2_TCTL_IRQEN, imxtm->base + MXC_TCTL);
}
#define imx21_gpt_irq_enable imx1_gpt_irq_enable

static void imx31_gpt_irq_enable(struct imx_timer *imxtm)
{
	writel_relaxed(1<<0, imxtm->base + V2_IR);
}
#define imx6dl_gpt_irq_enable imx31_gpt_irq_enable

static void imx1_gpt_irq_acknowledge(struct imx_timer *imxtm)
{
	writel_relaxed(0, imxtm->base + MX1_2_TSTAT);
}

static void imx21_gpt_irq_acknowledge(struct imx_timer *imxtm)
{
	writel_relaxed(MX2_TSTAT_CAPT | MX2_TSTAT_COMP,
				imxtm->base + MX1_2_TSTAT);
}

static void imx31_gpt_irq_acknowledge(struct imx_timer *imxtm)
{
	writel_relaxed(V2_TSTAT_OF1, imxtm->base + V2_TSTAT);
}
#define imx6dl_gpt_irq_acknowledge imx31_gpt_irq_acknowledge

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
	void __iomem *reg = imxtm->base + imxtm->gpt->reg_tcn;

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
			      struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);
	unsigned long tcmp;

	tcmp = readl_relaxed(imxtm->base + MX1_2_TCN) + evt;

	writel_relaxed(tcmp, imxtm->base + MX1_2_TCMP);

	return (int)(tcmp - readl_relaxed(imxtm->base + MX1_2_TCN)) < 0 ?
				-ETIME : 0;
}

static int v2_set_next_event(unsigned long evt,
			      struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);
	unsigned long tcmp;

	tcmp = readl_relaxed(imxtm->base + V2_TCN) + evt;

	writel_relaxed(tcmp, imxtm->base + V2_TCMP);

	return evt < 0x7fffffff &&
		(int)(tcmp - readl_relaxed(imxtm->base + V2_TCN)) < 0 ?
				-ETIME : 0;
}

static int mxc_shutdown(struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);
	unsigned long flags;
	u32 tcn;

	/*
	 * The timer interrupt generation is disabled at least
	 * for enough time to call mxc_set_next_event()
	 */
	local_irq_save(flags);

	/* Disable interrupt in GPT module */
	imxtm->gpt->gpt_irq_disable(imxtm);

	tcn = readl_relaxed(imxtm->base + imxtm->gpt->reg_tcn);
	/* Set event time into far-far future */
	writel_relaxed(tcn - 3, imxtm->base + imxtm->gpt->reg_tcmp);

	/* Clear pending interrupt */
	imxtm->gpt->gpt_irq_acknowledge(imxtm);

#ifdef DEBUG
	printk(KERN_INFO "%s: changing mode\n", __func__);
#endif /* DEBUG */

	local_irq_restore(flags);

	return 0;
}

static int mxc_set_oneshot(struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);
	unsigned long flags;

	/*
	 * The timer interrupt generation is disabled at least
	 * for enough time to call mxc_set_next_event()
	 */
	local_irq_save(flags);

	/* Disable interrupt in GPT module */
	imxtm->gpt->gpt_irq_disable(imxtm);

	if (!clockevent_state_oneshot(ced)) {
		u32 tcn = readl_relaxed(imxtm->base + imxtm->gpt->reg_tcn);
		/* Set event time into far-far future */
		writel_relaxed(tcn - 3, imxtm->base + imxtm->gpt->reg_tcmp);

		/* Clear pending interrupt */
		imxtm->gpt->gpt_irq_acknowledge(imxtm);
	}

#ifdef DEBUG
	printk(KERN_INFO "%s: changing mode\n", __func__);
#endif /* DEBUG */

	/*
	 * Do not put overhead of interrupt enable/disable into
	 * mxc_set_next_event(), the core has about 4 minutes
	 * to call mxc_set_next_event() or shutdown clock after
	 * mode switching
	 */
	imxtm->gpt->gpt_irq_enable(imxtm);
	local_irq_restore(flags);

	return 0;
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t mxc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ced = dev_id;
	struct imx_timer *imxtm = to_imx_timer(ced);
	uint32_t tstat;

	tstat = readl_relaxed(imxtm->base + imxtm->gpt->reg_tstat);

	imxtm->gpt->gpt_irq_acknowledge(imxtm);

	ced->event_handler(ced);

	return IRQ_HANDLED;
}

static int __init mxc_clockevent_init(struct imx_timer *imxtm)
{
	struct clock_event_device *ced = &imxtm->ced;
	struct irqaction *act = &imxtm->act;

	ced->name = "mxc_timer1";
	ced->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_DYNIRQ;
	ced->set_state_shutdown = mxc_shutdown;
	ced->set_state_oneshot = mxc_set_oneshot;
	ced->tick_resume = mxc_shutdown;
	ced->set_next_event = imxtm->gpt->set_next_event;
	ced->rating = 200;
	ced->cpumask = cpumask_of(0);
	ced->irq = imxtm->irq;
	clockevents_config_and_register(ced, clk_get_rate(imxtm->clk_per),
					0xff, 0xfffffffe);

	act->name = "i.MX Timer Tick";
	act->flags = IRQF_TIMER | IRQF_IRQPOLL;
	act->handler = mxc_timer_interrupt;
	act->dev_id = ced;

	return setup_irq(imxtm->irq, act);
}

static void imx1_gpt_setup_tctl(struct imx_timer *imxtm)
{
	u32 tctl_val;

	tctl_val = MX1_2_TCTL_FRR | MX1_2_TCTL_CLK_PCLK1 | MXC_TCTL_TEN;
	writel_relaxed(tctl_val, imxtm->base + MXC_TCTL);
}
#define imx21_gpt_setup_tctl imx1_gpt_setup_tctl

static void imx31_gpt_setup_tctl(struct imx_timer *imxtm)
{
	u32 tctl_val;

	tctl_val = V2_TCTL_FRR | V2_TCTL_WAITEN | MXC_TCTL_TEN;
	if (clk_get_rate(imxtm->clk_per) == V2_TIMER_RATE_OSC_DIV8)
		tctl_val |= V2_TCTL_CLK_OSC_DIV8;
	else
		tctl_val |= V2_TCTL_CLK_PER;

	writel_relaxed(tctl_val, imxtm->base + MXC_TCTL);
}

static void imx6dl_gpt_setup_tctl(struct imx_timer *imxtm)
{
	u32 tctl_val;

	tctl_val = V2_TCTL_FRR | V2_TCTL_WAITEN | MXC_TCTL_TEN;
	if (clk_get_rate(imxtm->clk_per) == V2_TIMER_RATE_OSC_DIV8) {
		tctl_val |= V2_TCTL_CLK_OSC_DIV8;
		/* 24 / 8 = 3 MHz */
		writel_relaxed(7 << V2_TPRER_PRE24M, imxtm->base + MXC_TPRER);
		tctl_val |= V2_TCTL_24MEN;
	} else {
		tctl_val |= V2_TCTL_CLK_PER;
	}

	writel_relaxed(tctl_val, imxtm->base + MXC_TCTL);
}

static const struct imx_gpt_data imx1_gpt_data = {
	.reg_tstat = MX1_2_TSTAT,
	.reg_tcn = MX1_2_TCN,
	.reg_tcmp = MX1_2_TCMP,
	.gpt_irq_enable = imx1_gpt_irq_enable,
	.gpt_irq_disable = imx1_gpt_irq_disable,
	.gpt_irq_acknowledge = imx1_gpt_irq_acknowledge,
	.gpt_setup_tctl = imx1_gpt_setup_tctl,
	.set_next_event = mx1_2_set_next_event,
};

static const struct imx_gpt_data imx21_gpt_data = {
	.reg_tstat = MX1_2_TSTAT,
	.reg_tcn = MX1_2_TCN,
	.reg_tcmp = MX1_2_TCMP,
	.gpt_irq_enable = imx21_gpt_irq_enable,
	.gpt_irq_disable = imx21_gpt_irq_disable,
	.gpt_irq_acknowledge = imx21_gpt_irq_acknowledge,
	.gpt_setup_tctl = imx21_gpt_setup_tctl,
	.set_next_event = mx1_2_set_next_event,
};

static const struct imx_gpt_data imx31_gpt_data = {
	.reg_tstat = V2_TSTAT,
	.reg_tcn = V2_TCN,
	.reg_tcmp = V2_TCMP,
	.gpt_irq_enable = imx31_gpt_irq_enable,
	.gpt_irq_disable = imx31_gpt_irq_disable,
	.gpt_irq_acknowledge = imx31_gpt_irq_acknowledge,
	.gpt_setup_tctl = imx31_gpt_setup_tctl,
	.set_next_event = v2_set_next_event,
};

static const struct imx_gpt_data imx6dl_gpt_data = {
	.reg_tstat = V2_TSTAT,
	.reg_tcn = V2_TCN,
	.reg_tcmp = V2_TCMP,
	.gpt_irq_enable = imx6dl_gpt_irq_enable,
	.gpt_irq_disable = imx6dl_gpt_irq_disable,
	.gpt_irq_acknowledge = imx6dl_gpt_irq_acknowledge,
	.gpt_setup_tctl = imx6dl_gpt_setup_tctl,
	.set_next_event = v2_set_next_event,
};

static void __init _mxc_timer_init(struct imx_timer *imxtm)
{
	switch (imxtm->type) {
	case GPT_TYPE_IMX1:
		imxtm->gpt = &imx1_gpt_data;
		break;
	case GPT_TYPE_IMX21:
		imxtm->gpt = &imx21_gpt_data;
		break;
	case GPT_TYPE_IMX31:
		imxtm->gpt = &imx31_gpt_data;
		break;
	case GPT_TYPE_IMX6DL:
		imxtm->gpt = &imx6dl_gpt_data;
		break;
	default:
		BUG();
	}

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

	imxtm->gpt->gpt_setup_tctl(imxtm);

	/* init and register the timer to the framework */
	mxc_clocksource_init(imxtm);
	mxc_clockevent_init(imxtm);
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
	imxtm->irq = irq;

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
CLOCKSOURCE_OF_DECLARE(imx27_timer, "fsl,imx27-gpt", imx21_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx31_timer, "fsl,imx31-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx25_timer, "fsl,imx25-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx50_timer, "fsl,imx50-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx51_timer, "fsl,imx51-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx53_timer, "fsl,imx53-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6q_timer, "fsl,imx6q-gpt", imx31_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6dl_timer, "fsl,imx6dl-gpt", imx6dl_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6sl_timer, "fsl,imx6sl-gpt", imx6dl_timer_init_dt);
CLOCKSOURCE_OF_DECLARE(imx6sx_timer, "fsl,imx6sx-gpt", imx6dl_timer_init_dt);
