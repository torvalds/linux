// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Mediatek SoCs General-Purpose Timer handling.
 *
 * Copyright (C) 2014 Matthias Brugger
 *
 * Matthias Brugger <matthias.bgg@gmail.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>
#include "timer-of.h"

#define TIMER_CLK_EVT           (1)
#define TIMER_CLK_SRC           (2)

#define TIMER_SYNC_TICKS        (3)

/* cpux mcusys wrapper */
#define CPUX_CON_REG		0x0
#define CPUX_IDX_REG		0x4

/* cpux */
#define CPUX_IDX_GLOBAL_CTRL	0x0
 #define CPUX_ENABLE		BIT(0)
 #define CPUX_CLK_DIV_MASK	GENMASK(10, 8)
 #define CPUX_CLK_DIV1		BIT(8)
 #define CPUX_CLK_DIV2		BIT(9)
 #define CPUX_CLK_DIV4		BIT(10)
#define CPUX_IDX_GLOBAL_IRQ	0x30

/* gpt */
#define GPT_IRQ_EN_REG          0x00
#define GPT_IRQ_ENABLE(val)     BIT((val) - 1)
#define GPT_IRQ_ACK_REG	        0x08
#define GPT_IRQ_ACK(val)        BIT((val) - 1)

#define GPT_CTRL_REG(val)       (0x10 * (val))
#define GPT_CTRL_OP(val)        (((val) & 0x3) << 4)
#define GPT_CTRL_OP_ONESHOT     (0)
#define GPT_CTRL_OP_REPEAT      (1)
#define GPT_CTRL_OP_FREERUN     (3)
#define GPT_CTRL_CLEAR          (2)
#define GPT_CTRL_ENABLE         (1)
#define GPT_CTRL_DISABLE        (0)

#define GPT_CLK_REG(val)        (0x04 + (0x10 * (val)))
#define GPT_CLK_SRC(val)        (((val) & 0x1) << 4)
#define GPT_CLK_SRC_SYS13M      (0)
#define GPT_CLK_SRC_RTC32K      (1)
#define GPT_CLK_DIV1            (0x0)
#define GPT_CLK_DIV2            (0x1)

#define GPT_CNT_REG(val)        (0x08 + (0x10 * (val)))
#define GPT_CMP_REG(val)        (0x0C + (0x10 * (val)))

/* system timer */
#define SYST_BASE               (0x40)

#define SYST_CON                (SYST_BASE + 0x0)
#define SYST_VAL                (SYST_BASE + 0x4)

#define SYST_CON_REG(to)        (timer_of_base(to) + SYST_CON)
#define SYST_VAL_REG(to)        (timer_of_base(to) + SYST_VAL)

/*
 * SYST_CON_EN: Clock enable. Shall be set to
 *   - Start timer countdown.
 *   - Allow timeout ticks being updated.
 *   - Allow changing interrupt status,like clear irq pending.
 *
 * SYST_CON_IRQ_EN: Set to enable interrupt.
 *
 * SYST_CON_IRQ_CLR: Set to clear interrupt.
 */
#define SYST_CON_EN              BIT(0)
#define SYST_CON_IRQ_EN          BIT(1)
#define SYST_CON_IRQ_CLR         BIT(4)

static void __iomem *gpt_sched_reg __read_mostly;

static u32 mtk_cpux_readl(u32 reg_idx, struct timer_of *to)
{
	writel(reg_idx, timer_of_base(to) + CPUX_IDX_REG);
	return readl(timer_of_base(to) + CPUX_CON_REG);
}

static void mtk_cpux_writel(u32 val, u32 reg_idx, struct timer_of *to)
{
	writel(reg_idx, timer_of_base(to) + CPUX_IDX_REG);
	writel(val, timer_of_base(to) + CPUX_CON_REG);
}

static void mtk_cpux_set_irq(struct timer_of *to, bool enable)
{
	const unsigned long *irq_mask = cpumask_bits(cpu_possible_mask);
	u32 val;

	val = mtk_cpux_readl(CPUX_IDX_GLOBAL_IRQ, to);

	if (enable)
		val |= *irq_mask;
	else
		val &= ~(*irq_mask);

	mtk_cpux_writel(val, CPUX_IDX_GLOBAL_IRQ, to);
}

static int mtk_cpux_clkevt_shutdown(struct clock_event_device *clkevt)
{
	/* Clear any irq */
	mtk_cpux_set_irq(to_timer_of(clkevt), false);

	/*
	 * Disabling CPUXGPT timer will crash the platform, especially
	 * if Trusted Firmware is using it (usually, for sleep states),
	 * so we only mask the IRQ and call it a day.
	 */
	return 0;
}

static int mtk_cpux_clkevt_resume(struct clock_event_device *clkevt)
{
	mtk_cpux_set_irq(to_timer_of(clkevt), true);
	return 0;
}

static void mtk_syst_ack_irq(struct timer_of *to)
{
	/* Clear and disable interrupt */
	writel(SYST_CON_EN, SYST_CON_REG(to));
	writel(SYST_CON_IRQ_CLR | SYST_CON_EN, SYST_CON_REG(to));
}

static irqreturn_t mtk_syst_handler(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	mtk_syst_ack_irq(to);
	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static int mtk_syst_clkevt_next_event(unsigned long ticks,
				      struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	/* Enable clock to allow timeout tick update later */
	writel(SYST_CON_EN, SYST_CON_REG(to));

	/*
	 * Write new timeout ticks. Timer shall start countdown
	 * after timeout ticks are updated.
	 */
	writel(ticks, SYST_VAL_REG(to));

	/* Enable interrupt */
	writel(SYST_CON_EN | SYST_CON_IRQ_EN, SYST_CON_REG(to));

	return 0;
}

static int mtk_syst_clkevt_shutdown(struct clock_event_device *clkevt)
{
	/* Clear any irq */
	mtk_syst_ack_irq(to_timer_of(clkevt));

	/* Disable timer */
	writel(0, SYST_CON_REG(to_timer_of(clkevt)));

	return 0;
}

static int mtk_syst_clkevt_resume(struct clock_event_device *clkevt)
{
	return mtk_syst_clkevt_shutdown(clkevt);
}

static int mtk_syst_clkevt_oneshot(struct clock_event_device *clkevt)
{
	return 0;
}

static u64 notrace mtk_gpt_read_sched_clock(void)
{
	return readl_relaxed(gpt_sched_reg);
}

static void mtk_gpt_clkevt_time_stop(struct timer_of *to, u8 timer)
{
	u32 val;

	val = readl(timer_of_base(to) + GPT_CTRL_REG(timer));
	writel(val & ~GPT_CTRL_ENABLE, timer_of_base(to) +
	       GPT_CTRL_REG(timer));
}

static void mtk_gpt_clkevt_time_setup(struct timer_of *to,
				      unsigned long delay, u8 timer)
{
	writel(delay, timer_of_base(to) + GPT_CMP_REG(timer));
}

static void mtk_gpt_clkevt_time_start(struct timer_of *to,
				      bool periodic, u8 timer)
{
	u32 val;

	/* Acknowledge interrupt */
	writel(GPT_IRQ_ACK(timer), timer_of_base(to) + GPT_IRQ_ACK_REG);

	val = readl(timer_of_base(to) + GPT_CTRL_REG(timer));

	/* Clear 2 bit timer operation mode field */
	val &= ~GPT_CTRL_OP(0x3);

	if (periodic)
		val |= GPT_CTRL_OP(GPT_CTRL_OP_REPEAT);
	else
		val |= GPT_CTRL_OP(GPT_CTRL_OP_ONESHOT);

	writel(val | GPT_CTRL_ENABLE | GPT_CTRL_CLEAR,
	       timer_of_base(to) + GPT_CTRL_REG(timer));
}

static int mtk_gpt_clkevt_shutdown(struct clock_event_device *clk)
{
	mtk_gpt_clkevt_time_stop(to_timer_of(clk), TIMER_CLK_EVT);

	return 0;
}

static int mtk_gpt_clkevt_set_periodic(struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	mtk_gpt_clkevt_time_stop(to, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_setup(to, to->of_clk.period, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_start(to, true, TIMER_CLK_EVT);

	return 0;
}

static int mtk_gpt_clkevt_next_event(unsigned long event,
				     struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	mtk_gpt_clkevt_time_stop(to, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_setup(to, event, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_start(to, false, TIMER_CLK_EVT);

	return 0;
}

static irqreturn_t mtk_gpt_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	/* Acknowledge timer0 irq */
	writel(GPT_IRQ_ACK(TIMER_CLK_EVT), timer_of_base(to) + GPT_IRQ_ACK_REG);
	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static void
__init mtk_gpt_setup(struct timer_of *to, u8 timer, u8 option)
{
	writel(GPT_CTRL_CLEAR | GPT_CTRL_DISABLE,
	       timer_of_base(to) + GPT_CTRL_REG(timer));

	writel(GPT_CLK_SRC(GPT_CLK_SRC_SYS13M) | GPT_CLK_DIV1,
	       timer_of_base(to) + GPT_CLK_REG(timer));

	writel(0x0, timer_of_base(to) + GPT_CMP_REG(timer));

	writel(GPT_CTRL_OP(option) | GPT_CTRL_ENABLE,
	       timer_of_base(to) + GPT_CTRL_REG(timer));
}

static void mtk_gpt_enable_irq(struct timer_of *to, u8 timer)
{
	u32 val;

	/* Disable all interrupts */
	writel(0x0, timer_of_base(to) + GPT_IRQ_EN_REG);

	/* Acknowledge all spurious pending interrupts */
	writel(0x3f, timer_of_base(to) + GPT_IRQ_ACK_REG);

	val = readl(timer_of_base(to) + GPT_IRQ_EN_REG);
	writel(val | GPT_IRQ_ENABLE(timer),
	       timer_of_base(to) + GPT_IRQ_EN_REG);
}

static void mtk_gpt_resume(struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	mtk_gpt_enable_irq(to, TIMER_CLK_EVT);
}

static void mtk_gpt_suspend(struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	/* Disable all interrupts */
	writel(0x0, timer_of_base(to) + GPT_IRQ_EN_REG);

	/*
	 * This is called with interrupts disabled,
	 * so we need to ack any interrupt that is pending
	 * or for example ATF will prevent a suspend from completing.
	 */
	writel(0x3f, timer_of_base(to) + GPT_IRQ_ACK_REG);
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,

	.clkevt = {
		.name = "mtk-clkevt",
		.rating = 300,
		.cpumask = cpu_possible_mask,
	},

	.of_irq = {
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
	},
};

static int __init mtk_cpux_init(struct device_node *node)
{
	static struct timer_of to_cpux;
	u32 freq, val;
	int ret;

	/*
	 * There are per-cpu interrupts for the CPUX General Purpose Timer
	 * but since this timer feeds the AArch64 System Timer we can rely
	 * on the CPU timer PPIs as well, so we don't declare TIMER_OF_IRQ.
	 */
	to_cpux.flags = TIMER_OF_BASE | TIMER_OF_CLOCK;
	to_cpux.clkevt.name = "mtk-cpuxgpt";
	to_cpux.clkevt.rating = 10;
	to_cpux.clkevt.cpumask = cpu_possible_mask;
	to_cpux.clkevt.set_state_shutdown = mtk_cpux_clkevt_shutdown;
	to_cpux.clkevt.tick_resume = mtk_cpux_clkevt_resume;

	/* If this fails, bad things are about to happen... */
	ret = timer_of_init(node, &to_cpux);
	if (ret) {
		WARN(1, "Cannot start CPUX timers.\n");
		return ret;
	}

	/*
	 * Check if we're given a clock with the right frequency for this
	 * timer, otherwise warn but keep going with the setup anyway, as
	 * that makes it possible to still boot the kernel, even though
	 * it may not work correctly (random lockups, etc).
	 * The reason behind this is that having an early UART may not be
	 * possible for everyone and this gives a chance to retrieve kmsg
	 * for eventual debugging even on consumer devices.
	 */
	freq = timer_of_rate(&to_cpux);
	if (freq > 13000000)
		WARN(1, "Requested unsupported timer frequency %u\n", freq);

	/* Clock input is 26MHz, set DIV2 to achieve 13MHz clock */
	val = mtk_cpux_readl(CPUX_IDX_GLOBAL_CTRL, &to_cpux);
	val &= ~CPUX_CLK_DIV_MASK;
	val |= CPUX_CLK_DIV2;
	mtk_cpux_writel(val, CPUX_IDX_GLOBAL_CTRL, &to_cpux);

	/* Enable all CPUXGPT timers */
	val = mtk_cpux_readl(CPUX_IDX_GLOBAL_CTRL, &to_cpux);
	mtk_cpux_writel(val | CPUX_ENABLE, CPUX_IDX_GLOBAL_CTRL, &to_cpux);

	clockevents_config_and_register(&to_cpux.clkevt, timer_of_rate(&to_cpux),
					TIMER_SYNC_TICKS, 0xffffffff);

	return 0;
}

static int __init mtk_syst_init(struct device_node *node)
{
	int ret;

	to.clkevt.features = CLOCK_EVT_FEAT_DYNIRQ | CLOCK_EVT_FEAT_ONESHOT;
	to.clkevt.set_state_shutdown = mtk_syst_clkevt_shutdown;
	to.clkevt.set_state_oneshot = mtk_syst_clkevt_oneshot;
	to.clkevt.tick_resume = mtk_syst_clkevt_resume;
	to.clkevt.set_next_event = mtk_syst_clkevt_next_event;
	to.of_irq.handler = mtk_syst_handler;

	ret = timer_of_init(node, &to);
	if (ret)
		return ret;

	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					TIMER_SYNC_TICKS, 0xffffffff);

	return 0;
}

static int __init mtk_gpt_init(struct device_node *node)
{
	int ret;

	to.clkevt.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	to.clkevt.set_state_shutdown = mtk_gpt_clkevt_shutdown;
	to.clkevt.set_state_periodic = mtk_gpt_clkevt_set_periodic;
	to.clkevt.set_state_oneshot = mtk_gpt_clkevt_shutdown;
	to.clkevt.tick_resume = mtk_gpt_clkevt_shutdown;
	to.clkevt.set_next_event = mtk_gpt_clkevt_next_event;
	to.clkevt.suspend = mtk_gpt_suspend;
	to.clkevt.resume = mtk_gpt_resume;
	to.of_irq.handler = mtk_gpt_interrupt;

	ret = timer_of_init(node, &to);
	if (ret)
		return ret;

	/* Configure clock source */
	mtk_gpt_setup(&to, TIMER_CLK_SRC, GPT_CTRL_OP_FREERUN);
	clocksource_mmio_init(timer_of_base(&to) + GPT_CNT_REG(TIMER_CLK_SRC),
			      node->name, timer_of_rate(&to), 300, 32,
			      clocksource_mmio_readl_up);
	gpt_sched_reg = timer_of_base(&to) + GPT_CNT_REG(TIMER_CLK_SRC);
	sched_clock_register(mtk_gpt_read_sched_clock, 32, timer_of_rate(&to));

	/* Configure clock event */
	mtk_gpt_setup(&to, TIMER_CLK_EVT, GPT_CTRL_OP_REPEAT);
	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					TIMER_SYNC_TICKS, 0xffffffff);

	mtk_gpt_enable_irq(&to, TIMER_CLK_EVT);

	return 0;
}
TIMER_OF_DECLARE(mtk_mt6577, "mediatek,mt6577-timer", mtk_gpt_init);
TIMER_OF_DECLARE(mtk_mt6765, "mediatek,mt6765-timer", mtk_syst_init);
TIMER_OF_DECLARE(mtk_mt6795, "mediatek,mt6795-systimer", mtk_cpux_init);
