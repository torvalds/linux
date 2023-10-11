// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 *  Copyright (C) 2012 Lantiq GmbH
 */

#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include <lantiq_soc.h>
#include "../clk.h"

/* the magic ID byte of the core */
#define GPTU_MAGIC	0x59
/* clock control register */
#define GPTU_CLC	0x00
/* id register */
#define GPTU_ID		0x08
/* interrupt node enable */
#define GPTU_IRNEN	0xf4
/* interrupt control register */
#define GPTU_IRCR	0xf8
/* interrupt capture register */
#define GPTU_IRNCR	0xfc
/* there are 3 identical blocks of 2 timers. calculate register offsets */
#define GPTU_SHIFT(x)	(x % 2 ? 4 : 0)
#define GPTU_BASE(x)	(((x >> 1) * 0x20) + 0x10)
/* timer control register */
#define GPTU_CON(x)	(GPTU_BASE(x) + GPTU_SHIFT(x) + 0x00)
/* timer auto reload register */
#define GPTU_RUN(x)	(GPTU_BASE(x) + GPTU_SHIFT(x) + 0x08)
/* timer manual reload register */
#define GPTU_RLD(x)	(GPTU_BASE(x) + GPTU_SHIFT(x) + 0x10)
/* timer count register */
#define GPTU_CNT(x)	(GPTU_BASE(x) + GPTU_SHIFT(x) + 0x18)

/* GPTU_CON(x) */
#define CON_CNT		BIT(2)
#define CON_EDGE_ANY	(BIT(7) | BIT(6))
#define CON_SYNC	BIT(8)
#define CON_CLK_INT	BIT(10)

/* GPTU_RUN(x) */
#define RUN_SEN		BIT(0)
#define RUN_RL		BIT(2)

/* set clock to runmode */
#define CLC_RMC		BIT(8)
/* bring core out of suspend */
#define CLC_SUSPEND	BIT(4)
/* the disable bit */
#define CLC_DISABLE	BIT(0)

#define gptu_w32(x, y)	ltq_w32((x), gptu_membase + (y))
#define gptu_r32(x)	ltq_r32(gptu_membase + (x))

enum gptu_timer {
	TIMER1A = 0,
	TIMER1B,
	TIMER2A,
	TIMER2B,
	TIMER3A,
	TIMER3B
};

static void __iomem *gptu_membase;
static struct resource irqres[6];

static irqreturn_t timer_irq_handler(int irq, void *priv)
{
	int timer = irq - irqres[0].start;
	gptu_w32(1 << timer, GPTU_IRNCR);
	return IRQ_HANDLED;
}

static void gptu_hwinit(void)
{
	gptu_w32(0x00, GPTU_IRNEN);
	gptu_w32(0xff, GPTU_IRNCR);
	gptu_w32(CLC_RMC | CLC_SUSPEND, GPTU_CLC);
}

static void gptu_hwexit(void)
{
	gptu_w32(0x00, GPTU_IRNEN);
	gptu_w32(0xff, GPTU_IRNCR);
	gptu_w32(CLC_DISABLE, GPTU_CLC);
}

static int gptu_enable(struct clk *clk)
{
	int ret = request_irq(irqres[clk->bits].start, timer_irq_handler,
		IRQF_TIMER, "gtpu", NULL);
	if (ret) {
		pr_err("gptu: failed to request irq\n");
		return ret;
	}

	gptu_w32(CON_CNT | CON_EDGE_ANY | CON_SYNC | CON_CLK_INT,
		GPTU_CON(clk->bits));
	gptu_w32(1, GPTU_RLD(clk->bits));
	gptu_w32(gptu_r32(GPTU_IRNEN) | BIT(clk->bits), GPTU_IRNEN);
	gptu_w32(RUN_SEN | RUN_RL, GPTU_RUN(clk->bits));
	return 0;
}

static void gptu_disable(struct clk *clk)
{
	gptu_w32(0, GPTU_RUN(clk->bits));
	gptu_w32(0, GPTU_CON(clk->bits));
	gptu_w32(0, GPTU_RLD(clk->bits));
	gptu_w32(gptu_r32(GPTU_IRNEN) & ~BIT(clk->bits), GPTU_IRNEN);
	free_irq(irqres[clk->bits].start, NULL);
}

static inline void clkdev_add_gptu(struct device *dev, const char *con,
							unsigned int timer)
{
	struct clk *clk = kzalloc(sizeof(struct clk), GFP_KERNEL);

	if (!clk)
		return;
	clk->cl.dev_id = dev_name(dev);
	clk->cl.con_id = con;
	clk->cl.clk = clk;
	clk->enable = gptu_enable;
	clk->disable = gptu_disable;
	clk->bits = timer;
	clkdev_add(&clk->cl);
}

static int gptu_probe(struct platform_device *pdev)
{
	struct clk *clk;

	if (of_irq_to_resource_table(pdev->dev.of_node, irqres, 6) != 6) {
		dev_err(&pdev->dev, "Failed to get IRQ list\n");
		return -EINVAL;
	}

	/* remap gptu register range */
	gptu_membase = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(gptu_membase))
		return PTR_ERR(gptu_membase);

	/* enable our clock */
	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return -ENOENT;
	}
	clk_enable(clk);

	/* power up the core */
	gptu_hwinit();

	/* the gptu has a ID register */
	if (((gptu_r32(GPTU_ID) >> 8) & 0xff) != GPTU_MAGIC) {
		dev_err(&pdev->dev, "Failed to find magic\n");
		gptu_hwexit();
		clk_disable(clk);
		clk_put(clk);
		return -ENAVAIL;
	}

	/* register the clocks */
	clkdev_add_gptu(&pdev->dev, "timer1a", TIMER1A);
	clkdev_add_gptu(&pdev->dev, "timer1b", TIMER1B);
	clkdev_add_gptu(&pdev->dev, "timer2a", TIMER2A);
	clkdev_add_gptu(&pdev->dev, "timer2b", TIMER2B);
	clkdev_add_gptu(&pdev->dev, "timer3a", TIMER3A);
	clkdev_add_gptu(&pdev->dev, "timer3b", TIMER3B);

	dev_info(&pdev->dev, "gptu: 6 timers loaded\n");

	return 0;
}

static const struct of_device_id gptu_match[] = {
	{ .compatible = "lantiq,gptu-xway" },
	{},
};

static struct platform_driver dma_driver = {
	.probe = gptu_probe,
	.driver = {
		.name = "gptu-xway",
		.of_match_table = gptu_match,
	},
};

int __init gptu_init(void)
{
	int ret = platform_driver_register(&dma_driver);

	if (ret)
		pr_info("gptu: Error registering platform driver\n");
	return ret;
}

arch_initcall(gptu_init);
