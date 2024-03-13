// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MediaTek SoCs CPUX General Purpose Timer handling
 *
 * Based on timer-mediatek.c:
 * Copyright (C) 2014 Matthias Brugger <matthias.bgg@gmail.com>
 *
 * Copyright (C) 2022 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>
#include "timer-of.h"

#define TIMER_SYNC_TICKS        3

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

static struct timer_of to = {
	/*
	 * There are per-cpu interrupts for the CPUX General Purpose Timer
	 * but since this timer feeds the AArch64 System Timer we can rely
	 * on the CPU timer PPIs as well, so we don't declare TIMER_OF_IRQ.
	 */
	.flags = TIMER_OF_BASE | TIMER_OF_CLOCK,

	.clkevt = {
		.name = "mtk-cpuxgpt",
		.cpumask = cpu_possible_mask,
		.rating = 10,
		.set_state_shutdown = mtk_cpux_clkevt_shutdown,
		.tick_resume = mtk_cpux_clkevt_resume,
	},
};

static int __init mtk_cpux_init(struct device_node *node)
{
	u32 freq, val;
	int ret;

	/* If this fails, bad things are about to happen... */
	ret = timer_of_init(node, &to);
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
	freq = timer_of_rate(&to);
	if (freq > 13000000)
		WARN(1, "Requested unsupported timer frequency %u\n", freq);

	/* Clock input is 26MHz, set DIV2 to achieve 13MHz clock */
	val = mtk_cpux_readl(CPUX_IDX_GLOBAL_CTRL, &to);
	val &= ~CPUX_CLK_DIV_MASK;
	val |= CPUX_CLK_DIV2;
	mtk_cpux_writel(val, CPUX_IDX_GLOBAL_CTRL, &to);

	/* Enable all CPUXGPT timers */
	val = mtk_cpux_readl(CPUX_IDX_GLOBAL_CTRL, &to);
	mtk_cpux_writel(val | CPUX_ENABLE, CPUX_IDX_GLOBAL_CTRL, &to);

	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					TIMER_SYNC_TICKS, 0xffffffff);

	return 0;
}
TIMER_OF_DECLARE(mtk_mt6795, "mediatek,mt6795-systimer", mtk_cpux_init);
