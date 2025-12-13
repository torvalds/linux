// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek Pulse Width Modulator driver
 *
 * Copyright (C) 2015 John Crispin <blogic@openwrt.org>
 * Copyright (C) 2017 Zhi Mao <zhi.mao@mediatek.com>
 *
 */

#include <linux/bitfield.h>
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
#define PWMCON_CLKDIV			GENMASK(2, 0)
#define PWMHDUR			0x04
#define PWMLDUR			0x08
#define PWMGDUR			0x0c
#define PWMWAVENUM		0x28
#define PWMDWIDTH		0x2c
#define PWMDWIDTH_PERIOD		GENMASK(12, 0)
#define PWM45DWIDTH_FIXUP	0x30
#define PWMTHRES		0x30
#define PWMTHRES_DUTY			GENMASK(12, 0)
#define PWM45THRES_FIXUP	0x34
#define PWM_CK_26M_SEL_V3	0x74
#define PWM_CK_26M_SEL		0x210

struct pwm_mediatek_of_data {
	unsigned int num_pwms;
	bool pwm45_fixup;
	u16 pwm_ck_26m_sel_reg;
	unsigned int chanreg_base;
	unsigned int chanreg_width;
};

/**
 * struct pwm_mediatek_chip - struct representing PWM chip
 * @regs: base address of PWM chip
 * @clk_top: the top clock generator
 * @clk_main: the clock used by PWM core
 * @soc: pointer to chip's platform data
 * @clk_pwms: the clock and clkrate used by each PWM channel
 */
struct pwm_mediatek_chip {
	void __iomem *regs;
	struct clk *clk_top;
	struct clk *clk_main;
	const struct pwm_mediatek_of_data *soc;
	struct {
		struct clk *clk;
		unsigned long rate;
	} clk_pwms[];
};

static inline struct pwm_mediatek_chip *
to_pwm_mediatek_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int pwm_mediatek_clk_enable(struct pwm_mediatek_chip *pc,
				   unsigned int hwpwm)
{
	int ret;

	ret = clk_prepare_enable(pc->clk_top);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(pc->clk_main);
	if (ret < 0)
		goto disable_clk_top;

	ret = clk_prepare_enable(pc->clk_pwms[hwpwm].clk);
	if (ret < 0)
		goto disable_clk_main;

	if (!pc->clk_pwms[hwpwm].rate) {
		pc->clk_pwms[hwpwm].rate = clk_get_rate(pc->clk_pwms[hwpwm].clk);

		/*
		 * With the clk running with not more than 1 GHz the
		 * calculations in .apply() won't overflow.
		 */
		if (!pc->clk_pwms[hwpwm].rate ||
		    pc->clk_pwms[hwpwm].rate > 1000000000) {
			ret = -EINVAL;
			goto disable_clk_hwpwm;
		}
	}

	return 0;

disable_clk_hwpwm:
	clk_disable_unprepare(pc->clk_pwms[hwpwm].clk);
disable_clk_main:
	clk_disable_unprepare(pc->clk_main);
disable_clk_top:
	clk_disable_unprepare(pc->clk_top);

	return ret;
}

static void pwm_mediatek_clk_disable(struct pwm_mediatek_chip *pc,
				     unsigned int hwpwm)
{
	clk_disable_unprepare(pc->clk_pwms[hwpwm].clk);
	clk_disable_unprepare(pc->clk_main);
	clk_disable_unprepare(pc->clk_top);
}

static inline void pwm_mediatek_writel(struct pwm_mediatek_chip *chip,
				       unsigned int num, unsigned int offset,
				       u32 value)
{
	writel(value, chip->regs + chip->soc->chanreg_base +
	       num * chip->soc->chanreg_width + offset);
}

static inline u32 pwm_mediatek_readl(struct pwm_mediatek_chip *chip,
				     unsigned int num, unsigned int offset)
{
	return readl(chip->regs + chip->soc->chanreg_base +
		     num * chip->soc->chanreg_width + offset);
}

static void pwm_mediatek_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	u32 value;

	value = readl(pc->regs);
	value |= BIT(pwm->hwpwm);
	writel(value, pc->regs);
}

static void pwm_mediatek_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	u32 value;

	value = readl(pc->regs);
	value &= ~BIT(pwm->hwpwm);
	writel(value, pc->regs);
}

static int pwm_mediatek_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       u64 duty_ns, u64 period_ns)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	u32 clkdiv, enable;
	u32 reg_width = PWMDWIDTH, reg_thres = PWMTHRES;
	u64 cnt_period, cnt_duty;
	unsigned long clk_rate;
	int ret;

	ret = pwm_mediatek_clk_enable(pc, pwm->hwpwm);
	if (ret < 0)
		return ret;

	clk_rate = pc->clk_pwms[pwm->hwpwm].rate;

	/* Make sure we use the bus clock and not the 26MHz clock */
	if (pc->soc->pwm_ck_26m_sel_reg)
		writel(0, pc->regs + pc->soc->pwm_ck_26m_sel_reg);

	cnt_period = mul_u64_u64_div_u64(period_ns, clk_rate, NSEC_PER_SEC);
	if (cnt_period == 0) {
		ret = -ERANGE;
		goto out;
	}

	if (cnt_period > FIELD_MAX(PWMDWIDTH_PERIOD) + 1) {
		if (cnt_period >= ((FIELD_MAX(PWMDWIDTH_PERIOD) + 1) << FIELD_MAX(PWMCON_CLKDIV))) {
			clkdiv = FIELD_MAX(PWMCON_CLKDIV);
			cnt_period = FIELD_MAX(PWMDWIDTH_PERIOD) + 1;
		} else {
			clkdiv = ilog2(cnt_period) - ilog2(FIELD_MAX(PWMDWIDTH_PERIOD));
			cnt_period >>= clkdiv;
		}
	} else {
		clkdiv = 0;
	}

	cnt_duty = mul_u64_u64_div_u64(duty_ns, clk_rate, NSEC_PER_SEC) >> clkdiv;
	if (cnt_duty > cnt_period)
		cnt_duty = cnt_period;

	if (cnt_duty) {
		cnt_duty -= 1;
		enable = BIT(pwm->hwpwm);
	} else {
		enable = 0;
	}

	cnt_period -= 1;

	dev_dbg(&chip->dev, "pwm#%u: %lld/%lld @%lu -> CON: %x, PERIOD: %llx, DUTY: %llx\n",
		pwm->hwpwm, duty_ns, period_ns, clk_rate, clkdiv, cnt_period, cnt_duty);

	if (pc->soc->pwm45_fixup && pwm->hwpwm > 2) {
		/*
		 * PWM[4,5] has distinct offset for PWMDWIDTH and PWMTHRES
		 * from the other PWMs on MT7623.
		 */
		reg_width = PWM45DWIDTH_FIXUP;
		reg_thres = PWM45THRES_FIXUP;
	}

	pwm_mediatek_writel(pc, pwm->hwpwm, PWMCON, BIT(15) | clkdiv);
	pwm_mediatek_writel(pc, pwm->hwpwm, reg_width, cnt_period);

	if (enable) {
		pwm_mediatek_writel(pc, pwm->hwpwm, reg_thres, cnt_duty);
		pwm_mediatek_enable(chip, pwm);
	} else {
		pwm_mediatek_disable(chip, pwm);
	}

out:
	pwm_mediatek_clk_disable(pc, pwm->hwpwm);

	return ret;
}

static int pwm_mediatek_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled) {
			pwm_mediatek_disable(chip, pwm);
			pwm_mediatek_clk_disable(pc, pwm->hwpwm);
		}

		return 0;
	}

	err = pwm_mediatek_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!pwm->state.enabled)
		err = pwm_mediatek_clk_enable(pc, pwm->hwpwm);

	return err;
}

static int pwm_mediatek_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct pwm_mediatek_chip *pc = to_pwm_mediatek_chip(chip);
	int ret;
	u32 enable;
	u32 reg_width = PWMDWIDTH, reg_thres = PWMTHRES;

	if (pc->soc->pwm45_fixup && pwm->hwpwm > 2) {
		/*
		 * PWM[4,5] has distinct offset for PWMDWIDTH and PWMTHRES
		 * from the other PWMs on MT7623.
		 */
		reg_width = PWM45DWIDTH_FIXUP;
		reg_thres = PWM45THRES_FIXUP;
	}

	ret = pwm_mediatek_clk_enable(pc, pwm->hwpwm);
	if (ret < 0)
		return ret;

	enable = readl(pc->regs);
	if (enable & BIT(pwm->hwpwm)) {
		u32 clkdiv, cnt_period, cnt_duty;
		unsigned long clk_rate;

		clk_rate = pc->clk_pwms[pwm->hwpwm].rate;

		state->enabled = true;
		state->polarity = PWM_POLARITY_NORMAL;

		clkdiv = FIELD_GET(PWMCON_CLKDIV,
				   pwm_mediatek_readl(pc, pwm->hwpwm, PWMCON));
		cnt_period = FIELD_GET(PWMDWIDTH_PERIOD,
				       pwm_mediatek_readl(pc, pwm->hwpwm, reg_width));
		cnt_duty = FIELD_GET(PWMTHRES_DUTY,
				     pwm_mediatek_readl(pc, pwm->hwpwm, reg_thres));

		/*
		 * cnt_period is a 13 bit value, NSEC_PER_SEC is 30 bits wide
		 * and clkdiv is less than 8, so the multiplication doesn't
		 * overflow an u64.
		 */
		state->period =
			DIV_ROUND_UP_ULL((u64)cnt_period * NSEC_PER_SEC << clkdiv, clk_rate);
		state->duty_cycle =
			DIV_ROUND_UP_ULL((u64)cnt_duty * NSEC_PER_SEC << clkdiv, clk_rate);
	} else {
		state->enabled = false;
	}

	pwm_mediatek_clk_disable(pc, pwm->hwpwm);

	return ret;
}

static const struct pwm_ops pwm_mediatek_ops = {
	.apply = pwm_mediatek_apply,
	.get_state = pwm_mediatek_get_state,
};

static int pwm_mediatek_init_used_clks(struct pwm_mediatek_chip *pc)
{
	const struct pwm_mediatek_of_data *soc = pc->soc;
	unsigned int hwpwm;
	u32 enabled, handled = 0;
	int ret;

	ret = clk_prepare_enable(pc->clk_top);
	if (ret)
		return ret;

	ret = clk_prepare_enable(pc->clk_main);
	if (ret)
		goto err_enable_main;

	enabled = readl(pc->regs) & GENMASK(soc->num_pwms - 1, 0);

	while (enabled & ~handled) {
		hwpwm = ilog2(enabled & ~handled);

		ret = pwm_mediatek_clk_enable(pc, hwpwm);
		if (ret) {
			while (handled) {
				hwpwm = ilog2(handled);

				pwm_mediatek_clk_disable(pc, hwpwm);
				handled &= ~BIT(hwpwm);
			}

			break;
		}

		handled |= BIT(hwpwm);
	}

	clk_disable_unprepare(pc->clk_main);
err_enable_main:

	clk_disable_unprepare(pc->clk_top);

	return ret;
}

static int pwm_mediatek_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct pwm_mediatek_chip *pc;
	const struct pwm_mediatek_of_data *soc;
	unsigned int i;
	int ret;

	soc = of_device_get_match_data(&pdev->dev);

	chip = devm_pwmchip_alloc(&pdev->dev, soc->num_pwms,
				  sizeof(*pc) + soc->num_pwms * sizeof(*pc->clk_pwms));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_pwm_mediatek_chip(chip);

	pc->soc = soc;

	pc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->regs))
		return PTR_ERR(pc->regs);

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

		pc->clk_pwms[i].clk = devm_clk_get(&pdev->dev, name);
		if (IS_ERR(pc->clk_pwms[i].clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk_pwms[i].clk),
					     "Failed to get %s clock\n", name);

		ret = devm_clk_rate_exclusive_get(&pdev->dev, pc->clk_pwms[i].clk);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "Failed to lock clock rate for %s\n", name);
	}

	ret = pwm_mediatek_init_used_clks(pc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to initialize used clocks\n");

	chip->ops = &pwm_mediatek_ops;

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "pwmchip_add() failed\n");

	return 0;
}

static const struct pwm_mediatek_of_data mt2712_pwm_data = {
	.num_pwms = 8,
	.pwm45_fixup = false,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt6795_pwm_data = {
	.num_pwms = 7,
	.pwm45_fixup = false,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7622_pwm_data = {
	.num_pwms = 6,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7623_pwm_data = {
	.num_pwms = 5,
	.pwm45_fixup = true,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7628_pwm_data = {
	.num_pwms = 4,
	.pwm45_fixup = true,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7629_pwm_data = {
	.num_pwms = 1,
	.pwm45_fixup = false,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7981_pwm_data = {
	.num_pwms = 3,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL,
	.chanreg_base = 0x80,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7986_pwm_data = {
	.num_pwms = 2,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt7988_pwm_data = {
	.num_pwms = 8,
	.pwm45_fixup = false,
	.chanreg_base = 0x80,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt8183_pwm_data = {
	.num_pwms = 4,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt8365_pwm_data = {
	.num_pwms = 3,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt8516_pwm_data = {
	.num_pwms = 5,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL,
	.chanreg_base = 0x10,
	.chanreg_width = 0x40,
};

static const struct pwm_mediatek_of_data mt6991_pwm_data = {
	.num_pwms = 4,
	.pwm45_fixup = false,
	.pwm_ck_26m_sel_reg = PWM_CK_26M_SEL_V3,
	.chanreg_base = 0x100,
	.chanreg_width = 0x100,
};

static const struct of_device_id pwm_mediatek_of_match[] = {
	{ .compatible = "mediatek,mt2712-pwm", .data = &mt2712_pwm_data },
	{ .compatible = "mediatek,mt6795-pwm", .data = &mt6795_pwm_data },
	{ .compatible = "mediatek,mt6991-pwm", .data = &mt6991_pwm_data },
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
