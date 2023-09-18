// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reset driver for NXP LPC18xx/43xx Reset Generation Unit (RGU).
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

/* LPC18xx RGU registers */
#define LPC18XX_RGU_CTRL0		0x100
#define LPC18XX_RGU_CTRL1		0x104
#define LPC18XX_RGU_ACTIVE_STATUS0	0x150
#define LPC18XX_RGU_ACTIVE_STATUS1	0x154

#define LPC18XX_RGU_RESETS_PER_REG	32

/* Internal reset outputs */
#define LPC18XX_RGU_CORE_RST	0
#define LPC43XX_RGU_M0SUB_RST	12
#define LPC43XX_RGU_M0APP_RST	56

struct lpc18xx_rgu_data {
	struct reset_controller_dev rcdev;
	struct notifier_block restart_nb;
	struct clk *clk_delay;
	struct clk *clk_reg;
	void __iomem *base;
	spinlock_t lock;
	u32 delay_us;
};

#define to_rgu_data(p) container_of(p, struct lpc18xx_rgu_data, rcdev)

static int lpc18xx_rgu_restart(struct notifier_block *nb, unsigned long mode,
			       void *cmd)
{
	struct lpc18xx_rgu_data *rc = container_of(nb, struct lpc18xx_rgu_data,
						   restart_nb);

	writel(BIT(LPC18XX_RGU_CORE_RST), rc->base + LPC18XX_RGU_CTRL0);
	mdelay(2000);

	pr_emerg("%s: unable to restart system\n", __func__);

	return NOTIFY_DONE;
}

/*
 * The LPC18xx RGU has mostly self-deasserting resets except for the
 * two reset lines going to the internal Cortex-M0 cores.
 *
 * To prevent the M0 core resets from accidentally getting deasserted
 * status register must be check and bits in control register set to
 * preserve the state.
 */
static int lpc18xx_rgu_setclear_reset(struct reset_controller_dev *rcdev,
				      unsigned long id, bool set)
{
	struct lpc18xx_rgu_data *rc = to_rgu_data(rcdev);
	u32 stat_offset = LPC18XX_RGU_ACTIVE_STATUS0;
	u32 ctrl_offset = LPC18XX_RGU_CTRL0;
	unsigned long flags;
	u32 stat, rst_bit;

	stat_offset += (id / LPC18XX_RGU_RESETS_PER_REG) * sizeof(u32);
	ctrl_offset += (id / LPC18XX_RGU_RESETS_PER_REG) * sizeof(u32);
	rst_bit = 1 << (id % LPC18XX_RGU_RESETS_PER_REG);

	spin_lock_irqsave(&rc->lock, flags);
	stat = ~readl(rc->base + stat_offset);
	if (set)
		writel(stat | rst_bit, rc->base + ctrl_offset);
	else
		writel(stat & ~rst_bit, rc->base + ctrl_offset);
	spin_unlock_irqrestore(&rc->lock, flags);

	return 0;
}

static int lpc18xx_rgu_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return lpc18xx_rgu_setclear_reset(rcdev, id, true);
}

static int lpc18xx_rgu_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return lpc18xx_rgu_setclear_reset(rcdev, id, false);
}

/* Only M0 cores require explicit reset deassert */
static int lpc18xx_rgu_reset(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct lpc18xx_rgu_data *rc = to_rgu_data(rcdev);

	lpc18xx_rgu_assert(rcdev, id);
	udelay(rc->delay_us);

	switch (id) {
	case LPC43XX_RGU_M0SUB_RST:
	case LPC43XX_RGU_M0APP_RST:
		lpc18xx_rgu_setclear_reset(rcdev, id, false);
	}

	return 0;
}

static int lpc18xx_rgu_status(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct lpc18xx_rgu_data *rc = to_rgu_data(rcdev);
	u32 bit, offset = LPC18XX_RGU_ACTIVE_STATUS0;

	offset += (id / LPC18XX_RGU_RESETS_PER_REG) * sizeof(u32);
	bit = 1 << (id % LPC18XX_RGU_RESETS_PER_REG);

	return !(readl(rc->base + offset) & bit);
}

static const struct reset_control_ops lpc18xx_rgu_ops = {
	.reset		= lpc18xx_rgu_reset,
	.assert		= lpc18xx_rgu_assert,
	.deassert	= lpc18xx_rgu_deassert,
	.status		= lpc18xx_rgu_status,
};

static int lpc18xx_rgu_probe(struct platform_device *pdev)
{
	struct lpc18xx_rgu_data *rc;
	u32 fcclk, firc;
	int ret;

	rc = devm_kzalloc(&pdev->dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	rc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rc->base))
		return PTR_ERR(rc->base);

	rc->clk_reg = devm_clk_get(&pdev->dev, "reg");
	if (IS_ERR(rc->clk_reg)) {
		dev_err(&pdev->dev, "reg clock not found\n");
		return PTR_ERR(rc->clk_reg);
	}

	rc->clk_delay = devm_clk_get(&pdev->dev, "delay");
	if (IS_ERR(rc->clk_delay)) {
		dev_err(&pdev->dev, "delay clock not found\n");
		return PTR_ERR(rc->clk_delay);
	}

	ret = clk_prepare_enable(rc->clk_reg);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable reg clock\n");
		return ret;
	}

	ret = clk_prepare_enable(rc->clk_delay);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable delay clock\n");
		goto dis_clk_reg;
	}

	fcclk = clk_get_rate(rc->clk_reg) / USEC_PER_SEC;
	firc = clk_get_rate(rc->clk_delay) / USEC_PER_SEC;
	if (fcclk == 0 || firc == 0)
		rc->delay_us = 2;
	else
		rc->delay_us = DIV_ROUND_UP(fcclk, firc * firc);

	spin_lock_init(&rc->lock);

	rc->rcdev.owner = THIS_MODULE;
	rc->rcdev.nr_resets = 64;
	rc->rcdev.ops = &lpc18xx_rgu_ops;
	rc->rcdev.of_node = pdev->dev.of_node;

	ret = reset_controller_register(&rc->rcdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register device\n");
		goto dis_clks;
	}

	rc->restart_nb.priority = 192,
	rc->restart_nb.notifier_call = lpc18xx_rgu_restart,
	ret = register_restart_handler(&rc->restart_nb);
	if (ret)
		dev_warn(&pdev->dev, "failed to register restart handler\n");

	return 0;

dis_clks:
	clk_disable_unprepare(rc->clk_delay);
dis_clk_reg:
	clk_disable_unprepare(rc->clk_reg);

	return ret;
}

static const struct of_device_id lpc18xx_rgu_match[] = {
	{ .compatible = "nxp,lpc1850-rgu" },
	{ }
};

static struct platform_driver lpc18xx_rgu_driver = {
	.probe	= lpc18xx_rgu_probe,
	.driver	= {
		.name			= "lpc18xx-reset",
		.of_match_table		= lpc18xx_rgu_match,
		.suppress_bind_attrs	= true,
	},
};
builtin_platform_driver(lpc18xx_rgu_driver);
