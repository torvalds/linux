/*
 * Copyright (C) Overkiz SAS 2012
 *
 * Author: Boris BREZILLON <b.brezillon@overkiz.com>
 * License terms: GNU General Public License (GPL) version 2
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
#include <linux/platform_device.h>
#include <linux/atmel_tc.h>
#include <linux/pwm.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#define NPWM	6

#define ATMEL_TC_ACMR_MASK	(ATMEL_TC_ACPA | ATMEL_TC_ACPC |	\
				 ATMEL_TC_AEEVT | ATMEL_TC_ASWTRG)

#define ATMEL_TC_BCMR_MASK	(ATMEL_TC_BCPB | ATMEL_TC_BCPC |	\
				 ATMEL_TC_BEEVT | ATMEL_TC_BSWTRG)

struct atmel_tcb_pwm_device {
	enum pwm_polarity polarity;	/* PWM polarity */
	unsigned div;			/* PWM clock divider */
	unsigned duty;			/* PWM duty expressed in clk cycles */
	unsigned period;		/* PWM period expressed in clk cycles */
};

struct atmel_tcb_pwm_chip {
	struct pwm_chip chip;
	spinlock_t lock;
	struct atmel_tc *tc;
	struct atmel_tcb_pwm_device *pwms[NPWM];
};

static inline struct atmel_tcb_pwm_chip *to_tcb_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct atmel_tcb_pwm_chip, chip);
}

static int atmel_tcb_pwm_set_polarity(struct pwm_chip *chip,
				      struct pwm_device *pwm,
				      enum pwm_polarity polarity)
{
	struct atmel_tcb_pwm_device *tcbpwm = pwm_get_chip_data(pwm);

	tcbpwm->polarity = polarity;

	return 0;
}

static int atmel_tcb_pwm_request(struct pwm_chip *chip,
				 struct pwm_device *pwm)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm;
	struct atmel_tc *tc = tcbpwmc->tc;
	void __iomem *regs = tc->regs;
	unsigned group = pwm->hwpwm / 2;
	unsigned index = pwm->hwpwm % 2;
	unsigned cmr;
	int ret;

	tcbpwm = devm_kzalloc(chip->dev, sizeof(*tcbpwm), GFP_KERNEL);
	if (!tcbpwm)
		return -ENOMEM;

	ret = clk_prepare_enable(tc->clk[group]);
	if (ret) {
		devm_kfree(chip->dev, tcbpwm);
		return ret;
	}

	pwm_set_chip_data(pwm, tcbpwm);
	tcbpwm->polarity = PWM_POLARITY_NORMAL;
	tcbpwm->duty = 0;
	tcbpwm->period = 0;
	tcbpwm->div = 0;

	spin_lock(&tcbpwmc->lock);
	cmr = __raw_readl(regs + ATMEL_TC_REG(group, CMR));
	/*
	 * Get init config from Timer Counter registers if
	 * Timer Counter is already configured as a PWM generator.
	 */
	if (cmr & ATMEL_TC_WAVE) {
		if (index == 0)
			tcbpwm->duty =
				__raw_readl(regs + ATMEL_TC_REG(group, RA));
		else
			tcbpwm->duty =
				__raw_readl(regs + ATMEL_TC_REG(group, RB));

		tcbpwm->div = cmr & ATMEL_TC_TCCLKS;
		tcbpwm->period = __raw_readl(regs + ATMEL_TC_REG(group, RC));
		cmr &= (ATMEL_TC_TCCLKS | ATMEL_TC_ACMR_MASK |
			ATMEL_TC_BCMR_MASK);
	} else
		cmr = 0;

	cmr |= ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO | ATMEL_TC_EEVT_XC0;
	__raw_writel(cmr, regs + ATMEL_TC_REG(group, CMR));
	spin_unlock(&tcbpwmc->lock);

	tcbpwmc->pwms[pwm->hwpwm] = tcbpwm;

	return 0;
}

static void atmel_tcb_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = pwm_get_chip_data(pwm);
	struct atmel_tc *tc = tcbpwmc->tc;

	clk_disable_unprepare(tc->clk[pwm->hwpwm / 2]);
	tcbpwmc->pwms[pwm->hwpwm] = NULL;
	devm_kfree(chip->dev, tcbpwm);
}

static void atmel_tcb_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = pwm_get_chip_data(pwm);
	struct atmel_tc *tc = tcbpwmc->tc;
	void __iomem *regs = tc->regs;
	unsigned group = pwm->hwpwm / 2;
	unsigned index = pwm->hwpwm % 2;
	unsigned cmr;
	enum pwm_polarity polarity = tcbpwm->polarity;

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
	cmr = __raw_readl(regs + ATMEL_TC_REG(group, CMR));

	/* flush old setting and set the new one */
	if (index == 0) {
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

	__raw_writel(cmr, regs + ATMEL_TC_REG(group, CMR));

	/*
	 * Use software trigger to apply the new setting.
	 * If both PWM devices in this group are disabled we stop the clock.
	 */
	if (!(cmr & (ATMEL_TC_ACPC | ATMEL_TC_BCPC)))
		__raw_writel(ATMEL_TC_SWTRG | ATMEL_TC_CLKDIS,
			     regs + ATMEL_TC_REG(group, CCR));
	else
		__raw_writel(ATMEL_TC_SWTRG, regs +
			     ATMEL_TC_REG(group, CCR));

	spin_unlock(&tcbpwmc->lock);
}

static int atmel_tcb_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = pwm_get_chip_data(pwm);
	struct atmel_tc *tc = tcbpwmc->tc;
	void __iomem *regs = tc->regs;
	unsigned group = pwm->hwpwm / 2;
	unsigned index = pwm->hwpwm % 2;
	u32 cmr;
	enum pwm_polarity polarity = tcbpwm->polarity;

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
	cmr = __raw_readl(regs + ATMEL_TC_REG(group, CMR));

	/* flush old setting and set the new one */
	cmr &= ~ATMEL_TC_TCCLKS;

	if (index == 0) {
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
		if (index == 0) {
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

	__raw_writel(cmr, regs + ATMEL_TC_REG(group, CMR));

	if (index == 0)
		__raw_writel(tcbpwm->duty, regs + ATMEL_TC_REG(group, RA));
	else
		__raw_writel(tcbpwm->duty, regs + ATMEL_TC_REG(group, RB));

	__raw_writel(tcbpwm->period, regs + ATMEL_TC_REG(group, RC));

	/* Use software trigger to apply the new setting */
	__raw_writel(ATMEL_TC_CLKEN | ATMEL_TC_SWTRG,
		     regs + ATMEL_TC_REG(group, CCR));
	spin_unlock(&tcbpwmc->lock);
	return 0;
}

static int atmel_tcb_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				int duty_ns, int period_ns)
{
	struct atmel_tcb_pwm_chip *tcbpwmc = to_tcb_chip(chip);
	struct atmel_tcb_pwm_device *tcbpwm = pwm_get_chip_data(pwm);
	unsigned group = pwm->hwpwm / 2;
	unsigned index = pwm->hwpwm % 2;
	struct atmel_tcb_pwm_device *atcbpwm = NULL;
	struct atmel_tc *tc = tcbpwmc->tc;
	int i;
	int slowclk = 0;
	unsigned period;
	unsigned duty;
	unsigned rate = clk_get_rate(tc->clk[group]);
	unsigned long long min;
	unsigned long long max;

	/*
	 * Find best clk divisor:
	 * the smallest divisor which can fulfill the period_ns requirements.
	 */
	for (i = 0; i < 5; ++i) {
		if (atmel_tc_divisors[i] == 0) {
			slowclk = i;
			continue;
		}
		min = div_u64((u64)NSEC_PER_SEC * atmel_tc_divisors[i], rate);
		max = min << tc->tcb_config->counter_width;
		if (max >= period_ns)
			break;
	}

	/*
	 * If none of the divisor are small enough to represent period_ns
	 * take slow clock (32KHz).
	 */
	if (i == 5) {
		i = slowclk;
		rate = 32768;
		min = div_u64(NSEC_PER_SEC, rate);
		max = min << tc->tcb_config->counter_width;

		/* If period is too big return ERANGE error */
		if (max < period_ns)
			return -ERANGE;
	}

	duty = div_u64(duty_ns, min);
	period = div_u64(period_ns, min);

	if (index == 0)
		atcbpwm = tcbpwmc->pwms[pwm->hwpwm + 1];
	else
		atcbpwm = tcbpwmc->pwms[pwm->hwpwm - 1];

	/*
	 * PWM devices provided by TCB driver are grouped by 2:
	 * - group 0: PWM 0 & 1
	 * - group 1: PWM 2 & 3
	 * - group 2: PWM 4 & 5
	 *
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

	/* If the PWM is enabled, call enable to apply the new conf */
	if (test_bit(PWMF_ENABLED, &pwm->flags))
		atmel_tcb_pwm_enable(chip, pwm);

	return 0;
}

static const struct pwm_ops atmel_tcb_pwm_ops = {
	.request = atmel_tcb_pwm_request,
	.free = atmel_tcb_pwm_free,
	.config = atmel_tcb_pwm_config,
	.set_polarity = atmel_tcb_pwm_set_polarity,
	.enable = atmel_tcb_pwm_enable,
	.disable = atmel_tcb_pwm_disable,
	.owner = THIS_MODULE,
};

static int atmel_tcb_pwm_probe(struct platform_device *pdev)
{
	struct atmel_tcb_pwm_chip *tcbpwm;
	struct device_node *np = pdev->dev.of_node;
	struct atmel_tc *tc;
	int err;
	int tcblock;

	err = of_property_read_u32(np, "tc-block", &tcblock);
	if (err < 0) {
		dev_err(&pdev->dev,
			"failed to get Timer Counter Block number from device tree (error: %d)\n",
			err);
		return err;
	}

	tc = atmel_tc_alloc(tcblock);
	if (tc == NULL) {
		dev_err(&pdev->dev, "failed to allocate Timer Counter Block\n");
		return -ENOMEM;
	}

	tcbpwm = devm_kzalloc(&pdev->dev, sizeof(*tcbpwm), GFP_KERNEL);
	if (tcbpwm == NULL) {
		atmel_tc_free(tc);
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	tcbpwm->chip.dev = &pdev->dev;
	tcbpwm->chip.ops = &atmel_tcb_pwm_ops;
	tcbpwm->chip.of_xlate = of_pwm_xlate_with_flags;
	tcbpwm->chip.of_pwm_n_cells = 3;
	tcbpwm->chip.base = -1;
	tcbpwm->chip.npwm = NPWM;
	tcbpwm->tc = tc;

	spin_lock_init(&tcbpwm->lock);

	err = pwmchip_add(&tcbpwm->chip);
	if (err < 0) {
		atmel_tc_free(tc);
		return err;
	}

	platform_set_drvdata(pdev, tcbpwm);

	return 0;
}

static int atmel_tcb_pwm_remove(struct platform_device *pdev)
{
	struct atmel_tcb_pwm_chip *tcbpwm = platform_get_drvdata(pdev);
	int err;

	err = pwmchip_remove(&tcbpwm->chip);
	if (err < 0)
		return err;

	atmel_tc_free(tcbpwm->tc);

	return 0;
}

static const struct of_device_id atmel_tcb_pwm_dt_ids[] = {
	{ .compatible = "atmel,tcb-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_tcb_pwm_dt_ids);

static struct platform_driver atmel_tcb_pwm_driver = {
	.driver = {
		.name = "atmel-tcb-pwm",
		.owner = THIS_MODULE,
		.of_match_table = atmel_tcb_pwm_dt_ids,
	},
	.probe = atmel_tcb_pwm_probe,
	.remove = atmel_tcb_pwm_remove,
};
module_platform_driver(atmel_tcb_pwm_driver);

MODULE_AUTHOR("Boris BREZILLON <b.brezillon@overkiz.com>");
MODULE_DESCRIPTION("Atmel Timer Counter Pulse Width Modulation Driver");
MODULE_LICENSE("GPL v2");
