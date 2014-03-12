/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/types.h>

#define PWM_CONTROL_OFFSET			(0x00000000)
#define PWM_CONTROL_SMOOTH_SHIFT(chan)		(24 + (chan))
#define PWM_CONTROL_TYPE_SHIFT(chan)		(16 + (chan))
#define PWM_CONTROL_POLARITY_SHIFT(chan)	(8 + (chan))
#define PWM_CONTROL_ENABLE_SHIFT(chan)		(chan)

#define PRESCALE_OFFSET				(0x00000004)
#define PRESCALE_SHIFT(chan)			((chan) << 2)
#define PRESCALE_MASK(chan)			(0x7 << PRESCALE_SHIFT(chan))
#define PRESCALE_MIN				(0x00000000)
#define PRESCALE_MAX				(0x00000007)

#define PERIOD_COUNT_OFFSET(chan)		(0x00000008 + ((chan) << 3))
#define PERIOD_COUNT_MIN			(0x00000002)
#define PERIOD_COUNT_MAX			(0x00ffffff)

#define DUTY_CYCLE_HIGH_OFFSET(chan)		(0x0000000c + ((chan) << 3))
#define DUTY_CYCLE_HIGH_MIN			(0x00000000)
#define DUTY_CYCLE_HIGH_MAX			(0x00ffffff)

struct kona_pwmc {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
};

static void kona_pwmc_apply_settings(struct kona_pwmc *kp, unsigned int chan)
{
	unsigned long value = readl(kp->base + PWM_CONTROL_OFFSET);

	/*
	 * New duty and period settings are only reflected in the PWM output
	 * after a rising edge of the enable bit.  When smooth bit is set, the
	 * new settings are delayed until the prior PWM period has completed.
	 * Furthermore, if the smooth bit is set, the PWM continues to output a
	 * waveform based on the old settings during the time that the enable
	 * bit is low.  Otherwise the output is a constant high signal while
	 * the enable bit is low.
	 */

	/* clear enable bit but set smooth bit to maintain old output */
	value |= (1 << PWM_CONTROL_SMOOTH_SHIFT(chan));
	value &= ~(1 << PWM_CONTROL_ENABLE_SHIFT(chan));
	writel(value, kp->base + PWM_CONTROL_OFFSET);

	/* set enable bit and clear smooth bit to apply new settings */
	value &= ~(1 << PWM_CONTROL_SMOOTH_SHIFT(chan));
	value |= (1 << PWM_CONTROL_ENABLE_SHIFT(chan));
	writel(value, kp->base + PWM_CONTROL_OFFSET);
}

static int kona_pwmc_config(struct pwm_chip *chip, struct pwm_device *pwm,
				int duty_ns, int period_ns)
{
	struct kona_pwmc *kp = dev_get_drvdata(chip->dev);
	u64 val, div, clk_rate;
	unsigned long prescale = PRESCALE_MIN, pc, dc;
	unsigned int value, chan = pwm->hwpwm;

	/*
	 * Find period count, duty count and prescale to suit duty_ns and
	 * period_ns. This is done according to formulas described below:
	 *
	 * period_ns = 10^9 * (PRESCALE + 1) * PC / PWM_CLK_RATE
	 * duty_ns = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
	 *
	 * PC = (PWM_CLK_RATE * period_ns) / (10^9 * (PRESCALE + 1))
	 * DC = (PWM_CLK_RATE * duty_ns) / (10^9 * (PRESCALE + 1))
	 */

	clk_rate = clk_get_rate(kp->clk);

	/* There is polarity support in HW but it is easier to manage in SW */
	if (pwm->polarity == PWM_POLARITY_INVERSED)
		duty_ns = period_ns - duty_ns;

	while (1) {
		div = 1000000000;
		div *= 1 + prescale;
		val = clk_rate * period_ns;
		pc = div64_u64(val, div);
		val = clk_rate * duty_ns;
		dc = div64_u64(val, div);

		/* If duty_ns or period_ns are not achievable then return */
		if (pc < PERIOD_COUNT_MIN || dc < DUTY_CYCLE_HIGH_MIN)
			return -EINVAL;

		/* If pc and dc are in bounds, the calculation is done */
		if (pc <= PERIOD_COUNT_MAX && dc <= DUTY_CYCLE_HIGH_MAX)
			break;

		/* Otherwise, increase prescale and recalculate pc and dc */
		if (++prescale > PRESCALE_MAX)
			return -EINVAL;
	}

	/* If the PWM channel is enabled, write the settings to the HW */
	if (test_bit(PWMF_ENABLED, &pwm->flags)) {
		value = readl(kp->base + PRESCALE_OFFSET);
		value &= ~PRESCALE_MASK(chan);
		value |= prescale << PRESCALE_SHIFT(chan);
		writel(value, kp->base + PRESCALE_OFFSET);

		writel(pc, kp->base + PERIOD_COUNT_OFFSET(chan));

		writel(dc, kp->base + DUTY_CYCLE_HIGH_OFFSET(chan));

		kona_pwmc_apply_settings(kp, chan);
	}

	return 0;
}

static int kona_pwmc_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				  enum pwm_polarity polarity)
{
	/*
	 * The framework only allows the polarity to be changed when a PWM is
	 * disabled so no immediate action is required here.  When a channel is
	 * enabled, the polarity gets handled as part of the re-config step.
	 */

	return 0;
}

static int kona_pwmc_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct kona_pwmc *kp = dev_get_drvdata(chip->dev);
	int ret;

	/*
	 * The PWM framework does not clear the enable bit in the flags if an
	 * error is returned from a PWM driver's enable function so it must be
	 * cleared here if any trouble is encountered.
	 */

	ret = clk_prepare_enable(kp->clk);
	if (ret < 0) {
		dev_err(chip->dev, "failed to enable clock: %d\n", ret);
		clear_bit(PWMF_ENABLED, &pwm->flags);
		return ret;
	}

	ret = kona_pwmc_config(chip, pwm, pwm->duty_cycle, pwm->period);
	if (ret < 0) {
		clk_disable_unprepare(kp->clk);
		clear_bit(PWMF_ENABLED, &pwm->flags);
		return ret;
	}

	return 0;
}

static void kona_pwmc_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct kona_pwmc *kp = dev_get_drvdata(chip->dev);
	unsigned int chan = pwm->hwpwm;

	/*
	 * The "enable" bits in the control register only affect when settings
	 * start to take effect so the only real way to disable the PWM output
	 * is to program a zero duty cycle.
	 */

	writel(0, kp->base + DUTY_CYCLE_HIGH_OFFSET(chan));
	kona_pwmc_apply_settings(kp, chan);

	/*
	 * When the PWM clock is disabled, the output is pegged high or low
	 * depending on its state at that instant.  To guarantee that the new
	 * settings have taken effect and the output is low a delay of 400ns is
	 * required.
	 */

	ndelay(400);

	clk_disable_unprepare(kp->clk);
}

static const struct pwm_ops kona_pwm_ops = {
	.config = kona_pwmc_config,
	.set_polarity = kona_pwmc_set_polarity,
	.enable = kona_pwmc_enable,
	.disable = kona_pwmc_disable,
	.owner = THIS_MODULE,
};

static int kona_pwmc_probe(struct platform_device *pdev)
{
	struct kona_pwmc *kp;
	struct resource *res;
	unsigned int chan, value;
	int ret = 0;

	kp = devm_kzalloc(&pdev->dev, sizeof(*kp), GFP_KERNEL);
	if (kp == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, kp);

	kp->chip.dev = &pdev->dev;
	kp->chip.ops = &kona_pwm_ops;
	kp->chip.base = -1;
	kp->chip.npwm = 6;
	kp->chip.of_xlate = of_pwm_xlate_with_flags;
	kp->chip.of_pwm_n_cells = 3;
	kp->chip.can_sleep = true;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	kp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(kp->base))
		return PTR_ERR(kp->base);

	kp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(kp->clk)) {
		dev_err(&pdev->dev, "failed to get clock: %ld\n",
			PTR_ERR(kp->clk));
		return PTR_ERR(kp->clk);
	}

	ret = clk_prepare_enable(kp->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	/* Set smooth mode, push/pull, and normal polarity for all channels */
	for (value = 0, chan = 0; chan < kp->chip.npwm; chan++) {
		value |= (1 << PWM_CONTROL_SMOOTH_SHIFT(chan));
		value |= (1 << PWM_CONTROL_TYPE_SHIFT(chan));
		value |= (1 << PWM_CONTROL_POLARITY_SHIFT(chan));
	}
	writel(value, kp->base + PWM_CONTROL_OFFSET);

	clk_disable_unprepare(kp->clk);

	ret = pwmchip_add(&kp->chip);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);

	return ret;
}

static int kona_pwmc_remove(struct platform_device *pdev)
{
	struct kona_pwmc *kp = platform_get_drvdata(pdev);
	unsigned int chan;

	for (chan = 0; chan < kp->chip.npwm; chan++)
		if (test_bit(PWMF_ENABLED, &kp->chip.pwms[chan].flags))
			clk_disable_unprepare(kp->clk);

	return pwmchip_remove(&kp->chip);
}

static const struct of_device_id bcm_kona_pwmc_dt[] = {
	{ .compatible = "brcm,kona-pwm" },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm_kona_pwmc_dt);

static struct platform_driver kona_pwmc_driver = {

	.driver = {
		.name = "bcm-kona-pwm",
		.of_match_table = bcm_kona_pwmc_dt,
	},
	.probe = kona_pwmc_probe,
	.remove = kona_pwmc_remove,
};

module_platform_driver(kona_pwmc_driver);

MODULE_AUTHOR("Broadcom Corporation <bcm-kernel-feedback-list@broadcom.com>");
MODULE_AUTHOR("Tim Kryger <tkryger@broadcom.com>");
MODULE_DESCRIPTION("Driver for Kona PWM controller");
MODULE_LICENSE("GPL v2");
