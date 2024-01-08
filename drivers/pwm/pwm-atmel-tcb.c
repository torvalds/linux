// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Overkiz SAS 2012
 *
 * Author: Boris BREZILLON <b.brezillon@overkiz.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <soc/at91/atmel_tcb.h>

#define NPWM	2

#define ATMEL_TC_ACMR_MASK	(ATMEL_TC_ACPA | ATMEL_TC_ACPC |	\
				 ATMEL_TC_AEEVT | ATMEL_TC_ASWTRG)

#define ATMEL_TC_BCMR_MASK	(ATMEL_TC_BCPB | ATMEL_TC_BCPC |	\
				 ATMEL_TC_BEEVT | ATMEL_TC_BSWTRG)

struct atmel_tcb_pwm_device {
	unsigned div;			/* PWM clock divider */
	unsigned duty;			/* PWM duty expressed in clk cycles */
	unsigned period;		/* PWM period expressed in clk cycles */
};

struct atmel_tcb_channel {
	u32 enabled;
	u32 cmr;
	u32 ra;
	u32 rb;
	u32 rc;
};

struct atmel_tcb_pwm_chip {
	struct pwm_chip chip;
	spinlock_t lock;
	u8 channel;
	u8 width;
	struct regmap *regmap;
	struct clk *clk;
	struct clk *gclk;
	struct clk *slow_clk;
	struct atmel_tcb_pwm_device pwms[NPWM];
	struct atmel_tcb_channel bkup;
};

static const u8 atmel_tcb_divisors[] = { 2, 8, 32, 128, 0, };

static inline struct atmel_tcb_pwm_chip *to_tcb_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct atmel_tcb_pwm_chip, chip);
}

static int atmel_tcb_pwm_request(struct pwm_chip *chip,
				 struct pwm_device *pwm)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = &tcbpwmc->pwms[pwm->hwpwm];
	unsigned cmr;
	int ret;

	ret = clk_prepare_enable(tcbpwmc->clk);
	if (ret)
		return ret;

	tcbpwm->duty = 0;
	tcbpwm->period = 0;
	tcbpwm->div = 0;

	spin_lock(&tcbpwmc->lock);
	regmap_read(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CMR), &cmr);
	/*
	 * Get init config from Timer Counter registers if
	 * Timer Counter is already configured as a PWM generator.
	 */
	if (cmr & ATMEL_TC_WAVE) {
		if (pwm->hwpwm == 0)
			regmap_read(tcbpwmc->regmap,
				    ATMEL_TC_REG(tcbpwmc->channel, RA),
				    &tcbpwm->duty);
		else
			regmap_read(tcbpwmc->regmap,
				    ATMEL_TC_REG(tcbpwmc->channel, RB),
				    &tcbpwm->duty);

		tcbpwm->div = cmr & ATMEL_TC_TCCLKS;
		regmap_read(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, RC),
			    &tcbpwm->period);
		cmr &= (ATMEL_TC_TCCLKS | ATMEL_TC_ACMR_MASK |
			ATMEL_TC_BCMR_MASK);
	} else
		cmr = 0;

	cmr |= ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO | ATMEL_TC_EEVT_XC0;
	regmap_write(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CMR), cmr);
	spin_unlock(&tcbpwmc->lock);

	return 0;
}

static void atmel_tcb_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);

	clk_disable_unprepare(tcbpwmc->clk);
}

static void atmel_tcb_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm,
				  enum pwm_polarity polarity)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = &tcbpwmc->pwms[pwm->hwpwm];
	unsigned cmr;

	/*
	 * If duty is 0 the timer will be stopped and we have to
	 * configure the output correctly on software trigger:
	 *  - set output to high if PWM_POLARITY_INVERSED
	 *  - set output to low if PWM_POLARITY_NORMAL
	 *
	 * This is why we're reverting polarity in this case.
	 */
	if (tcbpwm->duty == 0)
		polarity = !polarity;

	spin_lock(&tcbpwmc->lock);
	regmap_read(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CMR), &cmr);

	/* flush old setting and set the new one */
	if (pwm->hwpwm == 0) {
		cmr &= ~ATMEL_TC_ACMR_MASK;
		if (polarity == PWM_POLARITY_INVERSED)
			cmr |= ATMEL_TC_ASWTRG_CLEAR;
		else
			cmr |= ATMEL_TC_ASWTRG_SET;
	} else {
		cmr &= ~ATMEL_TC_BCMR_MASK;
		if (polarity == PWM_POLARITY_INVERSED)
			cmr |= ATMEL_TC_BSWTRG_CLEAR;
		else
			cmr |= ATMEL_TC_BSWTRG_SET;
	}

	regmap_write(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CMR), cmr);

	/*
	 * Use software trigger to apply the new setting.
	 * If both PWM devices in this group are disabled we stop the clock.
	 */
	if (!(cmr & (ATMEL_TC_ACPC | ATMEL_TC_BCPC))) {
		regmap_write(tcbpwmc->regmap,
			     ATMEL_TC_REG(tcbpwmc->channel, CCR),
			     ATMEL_TC_SWTRG | ATMEL_TC_CLKDIS);
		tcbpwmc->bkup.enabled = 1;
	} else {
		regmap_write(tcbpwmc->regmap,
			     ATMEL_TC_REG(tcbpwmc->channel, CCR),
			     ATMEL_TC_SWTRG);
		tcbpwmc->bkup.enabled = 0;
	}

	spin_unlock(&tcbpwmc->lock);
}

static int atmel_tcb_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm,
				enum pwm_polarity polarity)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = &tcbpwmc->pwms[pwm->hwpwm];
	u32 cmr;

	/*
	 * If duty is 0 the timer will be stopped and we have to
	 * configure the output correctly on software trigger:
	 *  - set output to high if PWM_POLARITY_INVERSED
	 *  - set output to low if PWM_POLARITY_NORMAL
	 *
	 * This is why we're reverting polarity in this case.
	 */
	if (tcbpwm->duty == 0)
		polarity = !polarity;

	spin_lock(&tcbpwmc->lock);
	regmap_read(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CMR), &cmr);

	/* flush old setting and set the new one */
	cmr &= ~ATMEL_TC_TCCLKS;

	if (pwm->hwpwm == 0) {
		cmr &= ~ATMEL_TC_ACMR_MASK;

		/* Set CMR flags according to given polarity */
		if (polarity == PWM_POLARITY_INVERSED)
			cmr |= ATMEL_TC_ASWTRG_CLEAR;
		else
			cmr |= ATMEL_TC_ASWTRG_SET;
	} else {
		cmr &= ~ATMEL_TC_BCMR_MASK;
		if (polarity == PWM_POLARITY_INVERSED)
			cmr |= ATMEL_TC_BSWTRG_CLEAR;
		else
			cmr |= ATMEL_TC_BSWTRG_SET;
	}

	/*
	 * If duty is 0 or equal to period there's no need to register
	 * a specific action on RA/RB and RC compare.
	 * The output will be configured on software trigger and keep
	 * this config till next config call.
	 */
	if (tcbpwm->duty != tcbpwm->period && tcbpwm->duty > 0) {
		if (pwm->hwpwm == 0) {
			if (polarity == PWM_POLARITY_INVERSED)
				cmr |= ATMEL_TC_ACPA_SET | ATMEL_TC_ACPC_CLEAR;
			else
				cmr |= ATMEL_TC_ACPA_CLEAR | ATMEL_TC_ACPC_SET;
		} else {
			if (polarity == PWM_POLARITY_INVERSED)
				cmr |= ATMEL_TC_BCPB_SET | ATMEL_TC_BCPC_CLEAR;
			else
				cmr |= ATMEL_TC_BCPB_CLEAR | ATMEL_TC_BCPC_SET;
		}
	}

	cmr |= (tcbpwm->div & ATMEL_TC_TCCLKS);

	regmap_write(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CMR), cmr);

	if (pwm->hwpwm == 0)
		regmap_write(tcbpwmc->regmap,
			     ATMEL_TC_REG(tcbpwmc->channel, RA),
			     tcbpwm->duty);
	else
		regmap_write(tcbpwmc->regmap,
			     ATMEL_TC_REG(tcbpwmc->channel, RB),
			     tcbpwm->duty);

	regmap_write(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, RC),
		     tcbpwm->period);

	/* Use software trigger to apply the new setting */
	regmap_write(tcbpwmc->regmap, ATMEL_TC_REG(tcbpwmc->channel, CCR),
		     ATMEL_TC_SWTRG | ATMEL_TC_CLKEN);
	tcbpwmc->bkup.enabled = 1;
	spin_unlock(&tcbpwmc->lock);
	return 0;
}

static int atmel_tcb_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				int duty_ns, int period_ns)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = &tcbpwmc->pwms[pwm->hwpwm];
	struct atmel_tcb_pwm_device *atcbpwm = NULL;
	int i = 0;
	int slowclk = 0;
	unsigned period;
	unsigned duty;
	unsigned rate = clk_get_rate(tcbpwmc->clk);
	unsigned long long min;
	unsigned long long max;

	/*
	 * Find best clk divisor:
	 * the smallest divisor which can fulfill the period_ns requirements.
	 * If there is a gclk, the first divisor is actually the gclk selector
	 */
	if (tcbpwmc->gclk)
		i = 1;
	for (; i < ARRAY_SIZE(atmel_tcb_divisors); ++i) {
		if (atmel_tcb_divisors[i] == 0) {
			slowclk = i;
			continue;
		}
		min = div_u64((u64)NSEC_PER_SEC * atmel_tcb_divisors[i], rate);
		max = min << tcbpwmc->width;
		if (max >= period_ns)
			break;
	}

	/*
	 * If none of the divisor are small enough to represent period_ns
	 * take slow clock (32KHz).
	 */
	if (i == ARRAY_SIZE(atmel_tcb_divisors)) {
		i = slowclk;
		rate = clk_get_rate(tcbpwmc->slow_clk);
		min = div_u64(NSEC_PER_SEC, rate);
		max = min << tcbpwmc->width;

		/* If period is too big return ERANGE error */
		if (max < period_ns)
			return -ERANGE;
	}

	duty = div_u64(duty_ns, min);
	period = div_u64(period_ns, min);

	if (pwm->hwpwm == 0)
		atcbpwm = &tcbpwmc->pwms[1];
	else
		atcbpwm = &tcbpwmc->pwms[0];

	/*
	 * PWM devices provided by the TCB driver are grouped by 2.
	 * PWM devices in a given group must be configured with the
	 * same period_ns.
	 *
	 * We're checking the period value of the second PWM device
	 * in this group before applying the new config.
	 */
	if ((atcbpwm && atcbpwm->duty > 0 &&
			atcbpwm->duty != atcbpwm->period) &&
		(atcbpwm->div != i || atcbpwm->period != period)) {
		dev_err(chip->dev,
			"failed to configure period_ns: PWM group already configured with a different value\n");
		return -EINVAL;
	}

	tcbpwm->period = period;
	tcbpwm->div = i;
	tcbpwm->duty = duty;

	return 0;
}

static int atmel_tcb_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			       const struct pwm_state *state)
{
	int duty_cycle, period;
	int ret;

	if (!state->enabled) {
		atmel_tcb_pwm_disable(chip, pwm, state->polarity);
		return 0;
	}

	period = state->period < INT_MAX ? state->period : INT_MAX;
	duty_cycle = state->duty_cycle < INT_MAX ? state->duty_cycle : INT_MAX;

	ret = atmel_tcb_pwm_config(chip, pwm, duty_cycle, period);
	if (ret)
		return ret;

	return atmel_tcb_pwm_enable(chip, pwm, state->polarity);
}

static const struct pwm_ops atmel_tcb_pwm_ops = {
	.request = atmel_tcb_pwm_request,
	.free = atmel_tcb_pwm_free,
	.apply = atmel_tcb_pwm_apply,
};

static struct atmel_tcb_config tcb_rm9200_config = {
	.counter_width = 16,
};

static struct atmel_tcb_config tcb_sam9x5_config = {
	.counter_width = 32,
};

static struct atmel_tcb_config tcb_sama5d2_config = {
	.counter_width = 32,
	.has_gclk = 1,
};

static const struct of_device_id atmel_tcb_of_match[] = {
	{ .compatible = "atmel,at91rm9200-tcb", .data = &tcb_rm9200_config, },
	{ .compatible = "atmel,at91sam9x5-tcb", .data = &tcb_sam9x5_config, },
	{ .compatible = "atmel,sama5d2-tcb", .data = &tcb_sama5d2_config, },
	{ /* sentinel */ }
};

static int atmel_tcb_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct atmel_tcb_pwm_chip *tcbpwm;
	const struct atmel_tcb_config *config;
	struct device_node *np = pdev->dev.of_node;
	char clk_name[] = "t0_clk";
	int err;
	int channel;

	tcbpwm = devm_kzalloc(&pdev->dev, sizeof(*tcbpwm), GFP_KERNEL);
	if (tcbpwm == NULL)
		return -ENOMEM;

	err = of_property_read_u32(np, "reg", &channel);
	if (err < 0) {
		dev_err(&pdev->dev,
			"failed to get Timer Counter Block channel from device tree (error: %d)\n",
			err);
		return err;
	}

	tcbpwm->regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(tcbpwm->regmap))
		return PTR_ERR(tcbpwm->regmap);

	tcbpwm->slow_clk = of_clk_get_by_name(np->parent, "slow_clk");
	if (IS_ERR(tcbpwm->slow_clk))
		return PTR_ERR(tcbpwm->slow_clk);

	clk_name[1] += channel;
	tcbpwm->clk = of_clk_get_by_name(np->parent, clk_name);
	if (IS_ERR(tcbpwm->clk))
		tcbpwm->clk = of_clk_get_by_name(np->parent, "t0_clk");
	if (IS_ERR(tcbpwm->clk)) {
		err = PTR_ERR(tcbpwm->clk);
		goto err_slow_clk;
	}

	match = of_match_node(atmel_tcb_of_match, np->parent);
	config = match->data;

	if (config->has_gclk) {
		tcbpwm->gclk = of_clk_get_by_name(np->parent, "gclk");
		if (IS_ERR(tcbpwm->gclk)) {
			err = PTR_ERR(tcbpwm->gclk);
			goto err_clk;
		}
	}

	tcbpwm->chip.dev = &pdev->dev;
	tcbpwm->chip.ops = &atmel_tcb_pwm_ops;
	tcbpwm->chip.npwm = NPWM;
	tcbpwm->channel = channel;
	tcbpwm->width = config->counter_width;

	err = clk_prepare_enable(tcbpwm->slow_clk);
	if (err)
		goto err_gclk;

	spin_lock_init(&tcbpwm->lock);

	err = pwmchip_add(&tcbpwm->chip);
	if (err < 0)
		goto err_disable_clk;

	platform_set_drvdata(pdev, tcbpwm);

	return 0;

err_disable_clk:
	clk_disable_unprepare(tcbpwm->slow_clk);

err_gclk:
	clk_put(tcbpwm->gclk);

err_clk:
	clk_put(tcbpwm->clk);

err_slow_clk:
	clk_put(tcbpwm->slow_clk);

	return err;
}

static void atmel_tcb_pwm_remove(struct platform_device *pdev)
{
	struct atmel_tcb_pwm_chip *tcbpwm = platform_get_drvdata(pdev);

	pwmchip_remove(&tcbpwm->chip);

	clk_disable_unprepare(tcbpwm->slow_clk);
	clk_put(tcbpwm->gclk);
	clk_put(tcbpwm->clk);
	clk_put(tcbpwm->slow_clk);
}

static const struct of_device_id atmel_tcb_pwm_dt_ids[] = {
	{ .compatible = "atmel,tcb-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_tcb_pwm_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int atmel_tcb_pwm_suspend(struct device *dev)
{
	struct atmel_tcb_pwm_chip *tcbpwm = dev_get_drvdata(dev);
	struct atmel_tcb_channel *chan = &tcbpwm->bkup;
	unsigned int channel = tcbpwm->channel;

	regmap_read(tcbpwm->regmap, ATMEL_TC_REG(channel, CMR), &chan->cmr);
	regmap_read(tcbpwm->regmap, ATMEL_TC_REG(channel, RA), &chan->ra);
	regmap_read(tcbpwm->regmap, ATMEL_TC_REG(channel, RB), &chan->rb);
	regmap_read(tcbpwm->regmap, ATMEL_TC_REG(channel, RC), &chan->rc);

	return 0;
}

static int atmel_tcb_pwm_resume(struct device *dev)
{
	struct atmel_tcb_pwm_chip *tcbpwm = dev_get_drvdata(dev);
	struct atmel_tcb_channel *chan = &tcbpwm->bkup;
	unsigned int channel = tcbpwm->channel;

	regmap_write(tcbpwm->regmap, ATMEL_TC_REG(channel, CMR), chan->cmr);
	regmap_write(tcbpwm->regmap, ATMEL_TC_REG(channel, RA), chan->ra);
	regmap_write(tcbpwm->regmap, ATMEL_TC_REG(channel, RB), chan->rb);
	regmap_write(tcbpwm->regmap, ATMEL_TC_REG(channel, RC), chan->rc);

	if (chan->enabled)
		regmap_write(tcbpwm->regmap,
			     ATMEL_TC_CLKEN | ATMEL_TC_SWTRG,
			     ATMEL_TC_REG(channel, CCR));

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(atmel_tcb_pwm_pm_ops, atmel_tcb_pwm_suspend,
			 atmel_tcb_pwm_resume);

static struct platform_driver atmel_tcb_pwm_driver = {
	.driver = {
		.name = "atmel-tcb-pwm",
		.of_match_table = atmel_tcb_pwm_dt_ids,
		.pm = &atmel_tcb_pwm_pm_ops,
	},
	.probe = atmel_tcb_pwm_probe,
	.remove_new = atmel_tcb_pwm_remove,
};
module_platform_driver(atmel_tcb_pwm_driver);

MODULE_AUTHOR("Boris BREZILLON <b.brezillon@overkiz.com>");
MODULE_DESCRIPTION("Atmel Timer Counter Pulse Width Modulation Driver");
MODULE_LICENSE("GPL v2");
