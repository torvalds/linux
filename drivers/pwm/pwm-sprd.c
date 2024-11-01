// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Spreadtrum Communications Inc.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define SPRD_PWM_PRESCALE	0x0
#define SPRD_PWM_MOD		0x4
#define SPRD_PWM_DUTY		0x8
#define SPRD_PWM_ENABLE		0x18

#define SPRD_PWM_MOD_MAX	GENMASK(7, 0)
#define SPRD_PWM_DUTY_MSK	GENMASK(15, 0)
#define SPRD_PWM_PRESCALE_MSK	GENMASK(7, 0)
#define SPRD_PWM_ENABLE_BIT	BIT(0)

#define SPRD_PWM_CHN_NUM	4
#define SPRD_PWM_REGS_SHIFT	5
#define SPRD_PWM_CHN_CLKS_NUM	2
#define SPRD_PWM_CHN_OUTPUT_CLK	1

struct sprd_pwm_chn {
	struct clk_bulk_data clks[SPRD_PWM_CHN_CLKS_NUM];
	u32 clk_rate;
};

struct sprd_pwm_chip {
	void __iomem *base;
	struct device *dev;
	struct pwm_chip chip;
	int num_pwms;
	struct sprd_pwm_chn chn[SPRD_PWM_CHN_NUM];
};

/*
 * The list of clocks required by PWM channels, and each channel has 2 clocks:
 * enable clock and pwm clock.
 */
static const char * const sprd_pwm_clks[] = {
	"enable0", "pwm0",
	"enable1", "pwm1",
	"enable2", "pwm2",
	"enable3", "pwm3",
};

static u32 sprd_pwm_read(struct sprd_pwm_chip *spc, u32 hwid, u32 reg)
{
	u32 offset = reg + (hwid << SPRD_PWM_REGS_SHIFT);

	return readl_relaxed(spc->base + offset);
}

static void sprd_pwm_write(struct sprd_pwm_chip *spc, u32 hwid,
			   u32 reg, u32 val)
{
	u32 offset = reg + (hwid << SPRD_PWM_REGS_SHIFT);

	writel_relaxed(val, spc->base + offset);
}

static int sprd_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			      struct pwm_state *state)
{
	struct sprd_pwm_chip *spc =
		container_of(chip, struct sprd_pwm_chip, chip);
	struct sprd_pwm_chn *chn = &spc->chn[pwm->hwpwm];
	u32 val, duty, prescale;
	u64 tmp;
	int ret;

	/*
	 * The clocks to PWM channel has to be enabled first before
	 * reading to the registers.
	 */
	ret = clk_bulk_prepare_enable(SPRD_PWM_CHN_CLKS_NUM, chn->clks);
	if (ret) {
		dev_err(spc->dev, "failed to enable pwm%u clocks\n",
			pwm->hwpwm);
		return 0;
	}

	val = sprd_pwm_read(spc, pwm->hwpwm, SPRD_PWM_ENABLE);
	if (val & SPRD_PWM_ENABLE_BIT)
		state->enabled = true;
	else
		state->enabled = false;

	/*
	 * The hardware provides a counter that is feed by the source clock.
	 * The period length is (PRESCALE + 1) * MOD counter steps.
	 * The duty cycle length is (PRESCALE + 1) * DUTY counter steps.
	 * Thus the period_ns and duty_ns calculation formula should be:
	 * period_ns = NSEC_PER_SEC * (prescale + 1) * mod / clk_rate
	 * duty_ns = NSEC_PER_SEC * (prescale + 1) * duty / clk_rate
	 */
	val = sprd_pwm_read(spc, pwm->hwpwm, SPRD_PWM_PRESCALE);
	prescale = val & SPRD_PWM_PRESCALE_MSK;
	tmp = (prescale + 1) * NSEC_PER_SEC * SPRD_PWM_MOD_MAX;
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, chn->clk_rate);

	val = sprd_pwm_read(spc, pwm->hwpwm, SPRD_PWM_DUTY);
	duty = val & SPRD_PWM_DUTY_MSK;
	tmp = (prescale + 1) * NSEC_PER_SEC * duty;
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, chn->clk_rate);
	state->polarity = PWM_POLARITY_NORMAL;

	/* Disable PWM clocks if the PWM channel is not in enable state. */
	if (!state->enabled)
		clk_bulk_disable_unprepare(SPRD_PWM_CHN_CLKS_NUM, chn->clks);

	return 0;
}

static int sprd_pwm_config(struct sprd_pwm_chip *spc, struct pwm_device *pwm,
			   int duty_ns, int period_ns)
{
	struct sprd_pwm_chn *chn = &spc->chn[pwm->hwpwm];
	u32 prescale, duty;
	u64 tmp;

	/*
	 * The hardware provides a counter that is feed by the source clock.
	 * The period length is (PRESCALE + 1) * MOD counter steps.
	 * The duty cycle length is (PRESCALE + 1) * DUTY counter steps.
	 *
	 * To keep the maths simple we're always using MOD = SPRD_PWM_MOD_MAX.
	 * The value for PRESCALE is selected such that the resulting period
	 * gets the maximal length not bigger than the requested one with the
	 * given settings (MOD = SPRD_PWM_MOD_MAX and input clock).
	 */
	duty = duty_ns * SPRD_PWM_MOD_MAX / period_ns;

	tmp = (u64)chn->clk_rate * period_ns;
	do_div(tmp, NSEC_PER_SEC);
	prescale = DIV_ROUND_CLOSEST_ULL(tmp, SPRD_PWM_MOD_MAX) - 1;
	if (prescale > SPRD_PWM_PRESCALE_MSK)
		prescale = SPRD_PWM_PRESCALE_MSK;

	/*
	 * Note: Writing DUTY triggers the hardware to actually apply the
	 * values written to MOD and DUTY to the output, so must keep writing
	 * DUTY last.
	 *
	 * The hardware can ensures that current running period is completed
	 * before changing a new configuration to avoid mixed settings.
	 */
	sprd_pwm_write(spc, pwm->hwpwm, SPRD_PWM_PRESCALE, prescale);
	sprd_pwm_write(spc, pwm->hwpwm, SPRD_PWM_MOD, SPRD_PWM_MOD_MAX);
	sprd_pwm_write(spc, pwm->hwpwm, SPRD_PWM_DUTY, duty);

	return 0;
}

static int sprd_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	struct sprd_pwm_chip *spc =
		container_of(chip, struct sprd_pwm_chip, chip);
	struct sprd_pwm_chn *chn = &spc->chn[pwm->hwpwm];
	struct pwm_state *cstate = &pwm->state;
	int ret;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (state->enabled) {
		if (!cstate->enabled) {
			/*
			 * The clocks to PWM channel has to be enabled first
			 * before writing to the registers.
			 */
			ret = clk_bulk_prepare_enable(SPRD_PWM_CHN_CLKS_NUM,
						      chn->clks);
			if (ret) {
				dev_err(spc->dev,
					"failed to enable pwm%u clocks\n",
					pwm->hwpwm);
				return ret;
			}
		}

		ret = sprd_pwm_config(spc, pwm, state->duty_cycle,
				      state->period);
		if (ret)
			return ret;

		sprd_pwm_write(spc, pwm->hwpwm, SPRD_PWM_ENABLE, 1);
	} else if (cstate->enabled) {
		/*
		 * Note: After setting SPRD_PWM_ENABLE to zero, the controller
		 * will not wait for current period to be completed, instead it
		 * will stop the PWM channel immediately.
		 */
		sprd_pwm_write(spc, pwm->hwpwm, SPRD_PWM_ENABLE, 0);

		clk_bulk_disable_unprepare(SPRD_PWM_CHN_CLKS_NUM, chn->clks);
	}

	return 0;
}

static const struct pwm_ops sprd_pwm_ops = {
	.apply = sprd_pwm_apply,
	.get_state = sprd_pwm_get_state,
	.owner = THIS_MODULE,
};

static int sprd_pwm_clk_init(struct sprd_pwm_chip *spc)
{
	struct clk *clk_pwm;
	int ret, i;

	for (i = 0; i < SPRD_PWM_CHN_NUM; i++) {
		struct sprd_pwm_chn *chn = &spc->chn[i];
		int j;

		for (j = 0; j < SPRD_PWM_CHN_CLKS_NUM; ++j)
			chn->clks[j].id =
				sprd_pwm_clks[i * SPRD_PWM_CHN_CLKS_NUM + j];

		ret = devm_clk_bulk_get(spc->dev, SPRD_PWM_CHN_CLKS_NUM,
					chn->clks);
		if (ret) {
			if (ret == -ENOENT)
				break;

			return dev_err_probe(spc->dev, ret,
					     "failed to get channel clocks\n");
		}

		clk_pwm = chn->clks[SPRD_PWM_CHN_OUTPUT_CLK].clk;
		chn->clk_rate = clk_get_rate(clk_pwm);
	}

	if (!i) {
		dev_err(spc->dev, "no available PWM channels\n");
		return -ENODEV;
	}

	spc->num_pwms = i;

	return 0;
}

static int sprd_pwm_probe(struct platform_device *pdev)
{
	struct sprd_pwm_chip *spc;
	int ret;

	spc = devm_kzalloc(&pdev->dev, sizeof(*spc), GFP_KERNEL);
	if (!spc)
		return -ENOMEM;

	spc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spc->base))
		return PTR_ERR(spc->base);

	spc->dev = &pdev->dev;
	platform_set_drvdata(pdev, spc);

	ret = sprd_pwm_clk_init(spc);
	if (ret)
		return ret;

	spc->chip.dev = &pdev->dev;
	spc->chip.ops = &sprd_pwm_ops;
	spc->chip.npwm = spc->num_pwms;

	ret = pwmchip_add(&spc->chip);
	if (ret)
		dev_err(&pdev->dev, "failed to add PWM chip\n");

	return ret;
}

static int sprd_pwm_remove(struct platform_device *pdev)
{
	struct sprd_pwm_chip *spc = platform_get_drvdata(pdev);

	pwmchip_remove(&spc->chip);

	return 0;
}

static const struct of_device_id sprd_pwm_of_match[] = {
	{ .compatible = "sprd,ums512-pwm", },
	{ },
};
MODULE_DEVICE_TABLE(of, sprd_pwm_of_match);

static struct platform_driver sprd_pwm_driver = {
	.driver = {
		.name = "sprd-pwm",
		.of_match_table = sprd_pwm_of_match,
	},
	.probe = sprd_pwm_probe,
	.remove = sprd_pwm_remove,
};

module_platform_driver(sprd_pwm_driver);

MODULE_DESCRIPTION("Spreadtrum PWM Driver");
MODULE_LICENSE("GPL v2");
