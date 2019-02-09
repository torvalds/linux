// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

#include <dt-bindings/reset/amlogic,meson-axg-audio-arb.h>

struct meson_audio_arb_data {
	struct reset_controller_dev rstc;
	void __iomem *regs;
	struct clk *clk;
	const unsigned int *reset_bits;
	spinlock_t lock;
};

#define ARB_GENERAL_BIT	31

static const unsigned int axg_audio_arb_reset_bits[] = {
	[AXG_ARB_TODDR_A]	= 0,
	[AXG_ARB_TODDR_B]	= 1,
	[AXG_ARB_TODDR_C]	= 2,
	[AXG_ARB_FRDDR_A]	= 4,
	[AXG_ARB_FRDDR_B]	= 5,
	[AXG_ARB_FRDDR_C]	= 6,
};

static int meson_audio_arb_update(struct reset_controller_dev *rcdev,
				  unsigned long id, bool assert)
{
	u32 val;
	struct meson_audio_arb_data *arb =
		container_of(rcdev, struct meson_audio_arb_data, rstc);

	spin_lock(&arb->lock);
	val = readl(arb->regs);

	if (assert)
		val &= ~BIT(arb->reset_bits[id]);
	else
		val |= BIT(arb->reset_bits[id]);

	writel(val, arb->regs);
	spin_unlock(&arb->lock);

	return 0;
}

static int meson_audio_arb_status(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	u32 val;
	struct meson_audio_arb_data *arb =
		container_of(rcdev, struct meson_audio_arb_data, rstc);

	val = readl(arb->regs);

	return !(val & BIT(arb->reset_bits[id]));
}

static int meson_audio_arb_assert(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	return meson_audio_arb_update(rcdev, id, true);
}

static int meson_audio_arb_deassert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	return meson_audio_arb_update(rcdev, id, false);
}

static const struct reset_control_ops meson_audio_arb_rstc_ops = {
	.assert = meson_audio_arb_assert,
	.deassert = meson_audio_arb_deassert,
	.status = meson_audio_arb_status,
};

static const struct of_device_id meson_audio_arb_of_match[] = {
	{ .compatible = "amlogic,meson-axg-audio-arb", },
	{}
};
MODULE_DEVICE_TABLE(of, meson_audio_arb_of_match);

static int meson_audio_arb_remove(struct platform_device *pdev)
{
	struct meson_audio_arb_data *arb = platform_get_drvdata(pdev);

	/* Disable all access */
	spin_lock(&arb->lock);
	writel(0, arb->regs);
	spin_unlock(&arb->lock);

	clk_disable_unprepare(arb->clk);

	return 0;
}

static int meson_audio_arb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_audio_arb_data *arb;
	struct resource *res;
	int ret;

	arb = devm_kzalloc(dev, sizeof(*arb), GFP_KERNEL);
	if (!arb)
		return -ENOMEM;
	platform_set_drvdata(pdev, arb);

	arb->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(arb->clk)) {
		if (PTR_ERR(arb->clk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get clock\n");
		return PTR_ERR(arb->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	arb->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(arb->regs))
		return PTR_ERR(arb->regs);

	spin_lock_init(&arb->lock);
	arb->reset_bits = axg_audio_arb_reset_bits;
	arb->rstc.nr_resets = ARRAY_SIZE(axg_audio_arb_reset_bits);
	arb->rstc.ops = &meson_audio_arb_rstc_ops;
	arb->rstc.of_node = dev->of_node;

	/*
	 * Enable general :
	 * In the initial state, all memory interfaces are disabled
	 * and the general bit is on
	 */
	ret = clk_prepare_enable(arb->clk);
	if (ret) {
		dev_err(dev, "failed to enable arb clock\n");
		return ret;
	}
	writel(BIT(ARB_GENERAL_BIT), arb->regs);

	/* Register reset controller */
	ret = devm_reset_controller_register(dev, &arb->rstc);
	if (ret) {
		dev_err(dev, "failed to register arb reset controller\n");
		meson_audio_arb_remove(pdev);
	}

	return ret;
}

static struct platform_driver meson_audio_arb_pdrv = {
	.probe = meson_audio_arb_probe,
	.remove = meson_audio_arb_remove,
	.driver = {
		.name = "meson-audio-arb-reset",
		.of_match_table = meson_audio_arb_of_match,
	},
};
module_platform_driver(meson_audio_arb_pdrv);

MODULE_DESCRIPTION("Amlogic A113 Audio Memory Arbiter");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
