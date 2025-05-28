// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek Pulse Width Modulator driver
 *
 * Copyright (C) 2015 John Crispin <blogic@openwrt.org>
 * Copyright (C) 2017 Zhi Mao <zhi.mao@mediatek.com>
 *
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/types.h>

/* PWM registers and bits definitions */
#define PWMCON			0x00
#define PWMHDUR			0x04
#define PWMLDUR			0x08
#define PWMGDUR			0x0c
#define PWMWAVENUM		0x28
#define PWMDWIDTH		0x2c
#define PWM45DWIDTH_FIXUP	0x30
#define PWMTHRES		0x30
#define PWM45THRES_FIXUP	0x34
#define PWM_CK_26M_SEL		0x210

#define PWM_CLK_DIV_MAX		7

struct pwm_mediatek_of_data {
	unsigned int num_pwms;
	bool pwm45_fixup;
	bool has_ck_26m_sel;
	const unsigned int *reg_offset;
};

/**
 * struct pwm_mediatek_chip - struct representing PWM chip
 * @regs: base address of PWM chip
 * @clk_top: the top clock generator
 * @clk_main: the clock used by PWM core
 * @clk_pwms: the clock used by each PWM channel
 * @soc: pointer to chip's platform data
 */
struct pwm_mediatek_chip {
	void __iomem *regs;
	struct clk *clk_top;
	struct clk *clk_main;
	struct clk **clk_pwms;
	const struct pwm_mediatek_of_data *soc;
};

static const unsigned int mtk_pwm_reg_offset_v1[] = {
	0x0010, 0x0050, 0x0090, 0x00d0, 0x0110, 0x0150, 0x0190, 0x0220
};

static const unsigned int mtk_pwm_reg_offset_v2[] = {
	0x0080, 0x00c0, 0x0100, 0x0140, 0x0180, 0x01c0, 0x0200, 0x0240
};

static inline struct pwm_mediatek_chip *
to_pwm_mediatek_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int pwm_mediatek_clk_enable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	int ret;

	ret = clk_prepare_enable(pc->clk_top);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(pc->clk_main);
	if (ret < 0)
		goto disable_clk_top;

	ret = clk_prepare_enable(pc->clk_pwms[pwm->hwpwm]);
	if (ret < 0)
		goto disable_clk_main;

	return 0;

disable_clk_main:
	clk_disable_unprepare(pc->clk_main);
disable_clk_top:
	clk_disable_unprepare(pc->clk_top);

	return ret;
}

static void pwm_mediatek_clk_disable(struct pwm_chip *chip,
				     struct pwm_device *pwm)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);

	clk_disable_unprepare(pc->clk_pwms[pwm->hwpwm]);
	clk_disable_unprepare(pc->clk_main);
	clk_disable_unprepare(pc->clk_top);
}

static inline void pwm_mediatek_writel(struct pwm_mediatek_chip *chip,
				       unsigned int num, unsigned int offset,
				       u32 value)
{
	writel(value, chip->regs + chip->soc->reg_offset[num] + offset);
}

static int pwm_mediatek_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       int duty_ns, int period_ns)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	u32 clkdiv = 0, cnt_period, cnt_duty, reg_width = PWMDWIDTH,
	    reg_thres = PWMTHRES;
	unsigned long clk_rate;
	u64 resolution;
	int ret;

	ret = pwm_mediatek_clk_enable(chip, pwm);
	if (ret < 0)
		return ret;

	clk_rate = clk_get_rate(pc->clk_pwms[pwm->hwpwm]);
	if (!clk_rate)
		return -EINVAL;

	/* Make sure we use the bus clock and not the 26MHz clock */
	if (pc->soc->has_ck_26m_sel)
		writel(0, pc->regs + PWM_CK_26M_SEL);

	/* Using resolution in picosecond gets accuracy higher */
	resolution = (u64)NSEC_PER_SEC * 1000;
	do_div(resolution, clk_rate);

	cnt_period = DIV_ROUND_CLOSEST_ULL((u64)period_ns * 1000, resolution);
	while (cnt_period > 8191) {
		resolution *= 2;
		clkdiv++;
		cnt_period = DIV_ROUND_CLOSEST_ULL((u64)period_ns * 1000,
						   resolution);
	}

	if (clkdiv > PWM_CLK_DIV_MAX) {
		pwm_mediatek_clk_disable(chip, pwm);
		dev_err(pwmchip_parent(chip), "period of %d ns not supported\n", period_ns);
		return -EINVAL;
	}

	if (pc->soc->pwm45_fixup && pwm->hwpwm > 2) {
		/*
		 * PWM[4,5] has distinct offset for PWMDWIDTH and PWMTHRES
		 * from the other PWMs on MT7623.
		 */
		reg_width = PWM45DWIDTH_FIXUP;
		reg_thres = PWM45THRES_FIXUP;
	}

	cnt_duty = DIV_ROUND_CLOSEST_ULL((u64)duty_ns * 1000, resolution);
	pwm_mediatek_writel(pc, pwm->hwpwm, PWMCON, BIT(15) | clkdiv);
	pwm_mediatek_writel(pc, pwm->hwpwm, reg_width, cnt_period);
	pwm_mediatek_writel(pc, pwm->hwpwm, reg_thres, cnt_duty);

	pwm_mediatek_clk_disable(chip, pwm);

	return 0;
}

static int pwm_mediatek_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	u32 value;
	int ret;

	ret = pwm_mediatek_clk_enable(chip, pwm);
	if (ret < 0)
		return ret;

	value = readl(pc->regs);
	value |= BIT(pwm->hwpwm);
	writel(value, pc->regs);

	return 0;
}

static void pwm_mediatek_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	u32 value;

	value = readl(pc->regs);
	value &= ~BIT(pwm->hwpwm);
	writel(value, pc->regs);

	pwm_mediatek_clk_disable(chip, pwm);
}

static int pwm_mediatek_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled)
			pwm_mediatek_disable(chip, pwm);

		return 0;
	}

	err = pwm_mediatek_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!pwm->state.enabled)
		err = pwm_mediatek_enable(chip, pwm);

	return err;
}

static const struct pwm_ops pwm_mediatek_ops = {
	.apply = pwm_mediatek_apply,
};

static int pwm_mediatek_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct pwm_mediatek_chip *pc;
	const struct pwm_mediatek_of_data *soc;
	unsigned int i;
	int ret;

	soc = of_device_get_match_data(&pdev->dev);

	chip = devm_pwmchip_alloc(&pdev->dev, soc->num_pwms, sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_pwm_mediatek_chip(chip);

	pc->soc = soc;

	pc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->regs))
		return PTR_ERR(pc->regs);

	pc->clk_pwms = devm_kmalloc_array(&pdev->dev, soc->num_pwms,
				    sizeof(*pc->clk_pwms), GFP_KERNEL);
	if (!pc->clk_pwms)
		return -ENOMEM;

	pc->clk_top = devm_clk_get(&pdev->dev, "top");
	if (IS_ERR(pc->clk_top))
		return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk_top),
				     "Failed to get top clock\n");

	pc->clk_main = devm_clk_get(&pdev->dev, "main");
	if (IS_ERR(pc->clk_main))
		return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk_main),
				     "Failed to get main clock\n");

	for (i = 0; i < soc->num_pwms; i++) {
		char name[8];

		snprintf(name, sizeof(name), "pwm%d", i + 1);

		pc->clk_pwms[i] = devm_clk_get(&pdev->dev, name);
		if (IS_ERR(pc->clk_pwms[i]))
			return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk_pwms[i]),
					     "Failed to get %s clock\n", name);
	}

	chip->ops = &pwm_mediatek_ops;

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "pwmchip_add() failed\n");

	return 0;
}

static const struct pwm_mediatek_of_data mt2712_pwm_data = {
	.num_pwms = 8,
	.pwm45_fixup = false,
	.has_ck_26m_sel = false,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt6795_pwm_data = {
	.num_pwms = 7,
	.pwm45_fixup = false,
	.has_ck_26m_sel = false,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt7622_pwm_data = {
	.num_pwms = 6,
	.pwm45_fixup = false,
	.has_ck_26m_sel = true,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt7623_pwm_data = {
	.num_pwms = 5,
	.pwm45_fixup = true,
	.has_ck_26m_sel = false,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt7628_pwm_data = {
	.num_pwms = 4,
	.pwm45_fixup = true,
	.has_ck_26m_sel = false,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt7629_pwm_data = {
	.num_pwms = 1,
	.pwm45_fixup = false,
	.has_ck_26m_sel = false,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt7981_pwm_data = {
	.num_pwms = 3,
	.pwm45_fixup = false,
	.has_ck_26m_sel = true,
	.reg_offset = mtk_pwm_reg_offset_v2,
};

static const struct pwm_mediatek_of_data mt7986_pwm_data = {
	.num_pwms = 2,
	.pwm45_fixup = false,
	.has_ck_26m_sel = true,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt7988_pwm_data = {
	.num_pwms = 8,
	.pwm45_fixup = false,
	.has_ck_26m_sel = false,
	.reg_offset = mtk_pwm_reg_offset_v2,
};

static const struct pwm_mediatek_of_data mt8183_pwm_data = {
	.num_pwms = 4,
	.pwm45_fixup = false,
	.has_ck_26m_sel = true,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt8365_pwm_data = {
	.num_pwms = 3,
	.pwm45_fixup = false,
	.has_ck_26m_sel = true,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct pwm_mediatek_of_data mt8516_pwm_data = {
	.num_pwms = 5,
	.pwm45_fixup = false,
	.has_ck_26m_sel = true,
	.reg_offset = mtk_pwm_reg_offset_v1,
};

static const struct of_device_id pwm_mediatek_of_match[] = {
	{ .compatible = "mediatek,mt2712-pwm", .data = &mt2712_pwm_data },
	{ .compatible = "mediatek,mt6795-pwm", .data = &mt6795_pwm_data },
	{ .compatible = "mediatek,mt7622-pwm", .data = &mt7622_pwm_data },
	{ .compatible = "mediatek,mt7623-pwm", .data = &mt7623_pwm_data },
	{ .compatible = "mediatek,mt7628-pwm", .data = &mt7628_pwm_data },
	{ .compatible = "mediatek,mt7629-pwm", .data = &mt7629_pwm_data },
	{ .compatible = "mediatek,mt7981-pwm", .data = &mt7981_pwm_data },
	{ .compatible = "mediatek,mt7986-pwm", .data = &mt7986_pwm_data },
	{ .compatible = "mediatek,mt7988-pwm", .data = &mt7988_pwm_data },
	{ .compatible = "mediatek,mt8183-pwm", .data = &mt8183_pwm_data },
	{ .compatible = "mediatek,mt8365-pwm", .data = &mt8365_pwm_data },
	{ .compatible = "mediatek,mt8516-pwm", .data = &mt8516_pwm_data },
	{ },
};
MODULE_DEVICE_TABLE(of, pwm_mediatek_of_match);

static struct platform_driver pwm_mediatek_driver = {
	.driver = {
		.name = "pwm-mediatek",
		.of_match_table = pwm_mediatek_of_match,
	},
	.probe = pwm_mediatek_probe,
};
module_platform_driver(pwm_mediatek_driver);

MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("MediaTek general purpose Pulse Width Modulator driver");
MODULE_LICENSE("GPL v2");
