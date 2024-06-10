// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek display pulse-width-modulation controller driver.
 * Copyright (c) 2015 MediaTek Inc.
 * Author: YH Huang <yh.huang@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define DISP_PWM_EN		0x00

#define PWM_CLKDIV_SHIFT	16
#define PWM_CLKDIV_MAX		0x3ff
#define PWM_CLKDIV_MASK		(PWM_CLKDIV_MAX << PWM_CLKDIV_SHIFT)

#define PWM_PERIOD_BIT_WIDTH	12
#define PWM_PERIOD_MASK		((1 << PWM_PERIOD_BIT_WIDTH) - 1)

#define PWM_HIGH_WIDTH_SHIFT	16
#define PWM_HIGH_WIDTH_MASK	(0x1fff << PWM_HIGH_WIDTH_SHIFT)

struct mtk_pwm_data {
	u32 enable_mask;
	unsigned int con0;
	u32 con0_sel;
	unsigned int con1;

	bool has_commit;
	unsigned int commit;
	unsigned int commit_mask;

	unsigned int bls_debug;
	u32 bls_debug_mask;
};

struct mtk_disp_pwm {
	const struct mtk_pwm_data *data;
	struct clk *clk_main;
	struct clk *clk_mm;
	void __iomem *base;
	bool enabled;
};

static inline struct mtk_disp_pwm *to_mtk_disp_pwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static void mtk_disp_pwm_update_bits(struct mtk_disp_pwm *mdp, u32 offset,
				     u32 mask, u32 data)
{
	void __iomem *address = mdp->base + offset;
	u32 value;

	value = readl(address);
	value &= ~mask;
	value |= data;
	writel(value, address);
}

static int mtk_disp_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct mtk_disp_pwm *mdp = to_mtk_disp_pwm(chip);
	u32 clk_div, period, high_width, value;
	u64 div, rate;
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled && mdp->enabled) {
		mtk_disp_pwm_update_bits(mdp, DISP_PWM_EN,
					 mdp->data->enable_mask, 0x0);
		clk_disable_unprepare(mdp->clk_mm);
		clk_disable_unprepare(mdp->clk_main);

		mdp->enabled = false;
		return 0;
	}

	if (!mdp->enabled) {
		err = clk_prepare_enable(mdp->clk_main);
		if (err < 0) {
			dev_err(pwmchip_parent(chip), "Can't enable mdp->clk_main: %pe\n",
				ERR_PTR(err));
			return err;
		}

		err = clk_prepare_enable(mdp->clk_mm);
		if (err < 0) {
			dev_err(pwmchip_parent(chip), "Can't enable mdp->clk_mm: %pe\n",
				ERR_PTR(err));
			clk_disable_unprepare(mdp->clk_main);
			return err;
		}
	}

	/*
	 * Find period, high_width and clk_div to suit duty_ns and period_ns.
	 * Calculate proper div value to keep period value in the bound.
	 *
	 * period_ns = 10^9 * (clk_div + 1) * (period + 1) / PWM_CLK_RATE
	 * duty_ns = 10^9 * (clk_div + 1) * high_width / PWM_CLK_RATE
	 *
	 * period = (PWM_CLK_RATE * period_ns) / (10^9 * (clk_div + 1)) - 1
	 * high_width = (PWM_CLK_RATE * duty_ns) / (10^9 * (clk_div + 1))
	 */
	rate = clk_get_rate(mdp->clk_main);
	clk_div = mul_u64_u64_div_u64(state->period, rate, NSEC_PER_SEC) >>
			  PWM_PERIOD_BIT_WIDTH;
	if (clk_div > PWM_CLKDIV_MAX) {
		if (!mdp->enabled) {
			clk_disable_unprepare(mdp->clk_mm);
			clk_disable_unprepare(mdp->clk_main);
		}
		return -EINVAL;
	}

	div = NSEC_PER_SEC * (clk_div + 1);
	period = mul_u64_u64_div_u64(state->period, rate, div);
	if (period > 0)
		period--;

	high_width = mul_u64_u64_div_u64(state->duty_cycle, rate, div);
	value = period | (high_width << PWM_HIGH_WIDTH_SHIFT);

	if (mdp->data->bls_debug && !mdp->data->has_commit) {
		/*
		 * For MT2701, disable double buffer before writing register
		 * and select manual mode and use PWM_PERIOD/PWM_HIGH_WIDTH.
		 */
		mtk_disp_pwm_update_bits(mdp, mdp->data->bls_debug,
					 mdp->data->bls_debug_mask,
					 mdp->data->bls_debug_mask);
		mtk_disp_pwm_update_bits(mdp, mdp->data->con0,
					 mdp->data->con0_sel,
					 mdp->data->con0_sel);
	}

	mtk_disp_pwm_update_bits(mdp, mdp->data->con0,
				 PWM_CLKDIV_MASK,
				 clk_div << PWM_CLKDIV_SHIFT);
	mtk_disp_pwm_update_bits(mdp, mdp->data->con1,
				 PWM_PERIOD_MASK | PWM_HIGH_WIDTH_MASK,
				 value);

	if (mdp->data->has_commit) {
		mtk_disp_pwm_update_bits(mdp, mdp->data->commit,
					 mdp->data->commit_mask,
					 mdp->data->commit_mask);
		mtk_disp_pwm_update_bits(mdp, mdp->data->commit,
					 mdp->data->commit_mask,
					 0x0);
	}

	mtk_disp_pwm_update_bits(mdp, DISP_PWM_EN, mdp->data->enable_mask,
				 mdp->data->enable_mask);
	mdp->enabled = true;

	return 0;
}

static int mtk_disp_pwm_get_state(struct pwm_chip *chip,
				  struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct mtk_disp_pwm *mdp = to_mtk_disp_pwm(chip);
	u64 rate, period, high_width;
	u32 clk_div, pwm_en, con0, con1;
	int err;

	err = clk_prepare_enable(mdp->clk_main);
	if (err < 0) {
		dev_err(pwmchip_parent(chip), "Can't enable mdp->clk_main: %pe\n", ERR_PTR(err));
		return err;
	}

	err = clk_prepare_enable(mdp->clk_mm);
	if (err < 0) {
		dev_err(pwmchip_parent(chip), "Can't enable mdp->clk_mm: %pe\n", ERR_PTR(err));
		clk_disable_unprepare(mdp->clk_main);
		return err;
	}

	/*
	 * Apply DISP_PWM_DEBUG settings to choose whether to enable or disable
	 * registers double buffer and manual commit to working register before
	 * performing any read/write operation
	 */
	if (mdp->data->bls_debug)
		mtk_disp_pwm_update_bits(mdp, mdp->data->bls_debug,
					 mdp->data->bls_debug_mask,
					 mdp->data->bls_debug_mask);

	rate = clk_get_rate(mdp->clk_main);
	con0 = readl(mdp->base + mdp->data->con0);
	con1 = readl(mdp->base + mdp->data->con1);
	pwm_en = readl(mdp->base + DISP_PWM_EN);
	state->enabled = !!(pwm_en & mdp->data->enable_mask);
	clk_div = FIELD_GET(PWM_CLKDIV_MASK, con0);
	period = FIELD_GET(PWM_PERIOD_MASK, con1);
	/*
	 * period has 12 bits, clk_div 11 and NSEC_PER_SEC has 30,
	 * so period * (clk_div + 1) * NSEC_PER_SEC doesn't overflow.
	 */
	state->period = DIV64_U64_ROUND_UP(period * (clk_div + 1) * NSEC_PER_SEC, rate);
	high_width = FIELD_GET(PWM_HIGH_WIDTH_MASK, con1);
	state->duty_cycle = DIV64_U64_ROUND_UP(high_width * (clk_div + 1) * NSEC_PER_SEC,
					       rate);
	state->polarity = PWM_POLARITY_NORMAL;
	clk_disable_unprepare(mdp->clk_mm);
	clk_disable_unprepare(mdp->clk_main);

	return 0;
}

static const struct pwm_ops mtk_disp_pwm_ops = {
	.apply = mtk_disp_pwm_apply,
	.get_state = mtk_disp_pwm_get_state,
};

static int mtk_disp_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct mtk_disp_pwm *mdp;
	int ret;

	chip = devm_pwmchip_alloc(&pdev->dev, 1, sizeof(*mdp));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	mdp = to_mtk_disp_pwm(chip);

	mdp->data = of_device_get_match_data(&pdev->dev);

	mdp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdp->base))
		return PTR_ERR(mdp->base);

	mdp->clk_main = devm_clk_get(&pdev->dev, "main");
	if (IS_ERR(mdp->clk_main))
		return dev_err_probe(&pdev->dev, PTR_ERR(mdp->clk_main),
				     "Failed to get main clock\n");

	mdp->clk_mm = devm_clk_get(&pdev->dev, "mm");
	if (IS_ERR(mdp->clk_mm))
		return dev_err_probe(&pdev->dev, PTR_ERR(mdp->clk_mm),
				     "Failed to get mm clock\n");

	chip->ops = &mtk_disp_pwm_ops;

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "pwmchip_add() failed\n");

	return 0;
}

static const struct mtk_pwm_data mt2701_pwm_data = {
	.enable_mask = BIT(16),
	.con0 = 0xa8,
	.con0_sel = 0x2,
	.con1 = 0xac,
	.has_commit = false,
	.bls_debug = 0xb0,
	.bls_debug_mask = 0x3,
};

static const struct mtk_pwm_data mt8173_pwm_data = {
	.enable_mask = BIT(0),
	.con0 = 0x10,
	.con0_sel = 0x0,
	.con1 = 0x14,
	.has_commit = true,
	.commit = 0x8,
	.commit_mask = 0x1,
};

static const struct mtk_pwm_data mt8183_pwm_data = {
	.enable_mask = BIT(0),
	.con0 = 0x18,
	.con0_sel = 0x0,
	.con1 = 0x1c,
	.has_commit = false,
	.bls_debug = 0x80,
	.bls_debug_mask = 0x3,
};

static const struct of_device_id mtk_disp_pwm_of_match[] = {
	{ .compatible = "mediatek,mt2701-disp-pwm", .data = &mt2701_pwm_data},
	{ .compatible = "mediatek,mt6595-disp-pwm", .data = &mt8173_pwm_data},
	{ .compatible = "mediatek,mt8173-disp-pwm", .data = &mt8173_pwm_data},
	{ .compatible = "mediatek,mt8183-disp-pwm", .data = &mt8183_pwm_data},
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_disp_pwm_of_match);

static struct platform_driver mtk_disp_pwm_driver = {
	.driver = {
		.name = "mediatek-disp-pwm",
		.of_match_table = mtk_disp_pwm_of_match,
	},
	.probe = mtk_disp_pwm_probe,
};
module_platform_driver(mtk_disp_pwm_driver);

MODULE_AUTHOR("YH Huang <yh.huang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SoC display PWM driver");
MODULE_LICENSE("GPL v2");
