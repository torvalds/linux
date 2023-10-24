// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ECAP PWM driver
 *
 * Copyright (C) 2012 Texas Instruments, Inc. - https://www.ti.com/
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/of.h>

/* ECAP registers and bits definitions */
#define CAP1			0x08
#define CAP2			0x0C
#define CAP3			0x10
#define CAP4			0x14
#define ECCTL2			0x2A
#define ECCTL2_APWM_POL_LOW	BIT(10)
#define ECCTL2_APWM_MODE	BIT(9)
#define ECCTL2_SYNC_SEL_DISA	(BIT(7) | BIT(6))
#define ECCTL2_TSCTR_FREERUN	BIT(4)

struct ecap_context {
	u32 cap3;
	u32 cap4;
	u16 ecctl2;
};

struct ecap_pwm_chip {
	struct pwm_chip chip;
	unsigned int clk_rate;
	void __iomem *mmio_base;
	struct ecap_context ctx;
};

static inline struct ecap_pwm_chip *to_ecap_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ecap_pwm_chip, chip);
}

/*
 * period_ns = 10^9 * period_cycles / PWM_CLK_RATE
 * duty_ns   = 10^9 * duty_cycles / PWM_CLK_RATE
 */
static int ecap_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			   int duty_ns, int period_ns, int enabled)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	u32 period_cycles, duty_cycles;
	unsigned long long c;
	u16 value;

	c = pc->clk_rate;
	c = c * period_ns;
	do_div(c, NSEC_PER_SEC);
	period_cycles = (u32)c;

	if (period_cycles < 1) {
		period_cycles = 1;
		duty_cycles = 1;
	} else {
		c = pc->clk_rate;
		c = c * duty_ns;
		do_div(c, NSEC_PER_SEC);
		duty_cycles = (u32)c;
	}

	pm_runtime_get_sync(pc->chip.dev);

	value = readw(pc->mmio_base + ECCTL2);

	/* Configure APWM mode & disable sync option */
	value |= ECCTL2_APWM_MODE | ECCTL2_SYNC_SEL_DISA;

	writew(value, pc->mmio_base + ECCTL2);

	if (!enabled) {
		/* Update active registers if not running */
		writel(duty_cycles, pc->mmio_base + CAP2);
		writel(period_cycles, pc->mmio_base + CAP1);
	} else {
		/*
		 * Update shadow registers to configure period and
		 * compare values. This helps current PWM period to
		 * complete on reconfiguring
		 */
		writel(duty_cycles, pc->mmio_base + CAP4);
		writel(period_cycles, pc->mmio_base + CAP3);
	}

	if (!enabled) {
		value = readw(pc->mmio_base + ECCTL2);
		/* Disable APWM mode to put APWM output Low */
		value &= ~ECCTL2_APWM_MODE;
		writew(value, pc->mmio_base + ECCTL2);
	}

	pm_runtime_put_sync(pc->chip.dev);

	return 0;
}

static int ecap_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				 enum pwm_polarity polarity)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	u16 value;

	pm_runtime_get_sync(pc->chip.dev);

	value = readw(pc->mmio_base + ECCTL2);

	if (polarity == PWM_POLARITY_INVERSED)
		/* Duty cycle defines LOW period of PWM */
		value |= ECCTL2_APWM_POL_LOW;
	else
		/* Duty cycle defines HIGH period of PWM */
		value &= ~ECCTL2_APWM_POL_LOW;

	writew(value, pc->mmio_base + ECCTL2);

	pm_runtime_put_sync(pc->chip.dev);

	return 0;
}

static int ecap_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	u16 value;

	/* Leave clock enabled on enabling PWM */
	pm_runtime_get_sync(pc->chip.dev);

	/*
	 * Enable 'Free run Time stamp counter mode' to start counter
	 * and  'APWM mode' to enable APWM output
	 */
	value = readw(pc->mmio_base + ECCTL2);
	value |= ECCTL2_TSCTR_FREERUN | ECCTL2_APWM_MODE;
	writew(value, pc->mmio_base + ECCTL2);

	return 0;
}

static void ecap_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	u16 value;

	/*
	 * Disable 'Free run Time stamp counter mode' to stop counter
	 * and 'APWM mode' to put APWM output to low
	 */
	value = readw(pc->mmio_base + ECCTL2);
	value &= ~(ECCTL2_TSCTR_FREERUN | ECCTL2_APWM_MODE);
	writew(value, pc->mmio_base + ECCTL2);

	/* Disable clock on PWM disable */
	pm_runtime_put_sync(pc->chip.dev);
}

static int ecap_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	int err;
	int enabled = pwm->state.enabled;

	if (state->polarity != pwm->state.polarity) {

		if (enabled) {
			ecap_pwm_disable(chip, pwm);
			enabled = false;
		}

		err = ecap_pwm_set_polarity(chip, pwm, state->polarity);
		if (err)
			return err;
	}

	if (!state->enabled) {
		if (enabled)
			ecap_pwm_disable(chip, pwm);
		return 0;
	}

	if (state->period > NSEC_PER_SEC)
		return -ERANGE;

	err = ecap_pwm_config(chip, pwm, state->duty_cycle,
			      state->period, enabled);
	if (err)
		return err;

	if (!enabled)
		return ecap_pwm_enable(chip, pwm);

	return 0;
}

static const struct pwm_ops ecap_pwm_ops = {
	.apply = ecap_pwm_apply,
	.owner = THIS_MODULE,
};

static const struct of_device_id ecap_of_match[] = {
	{ .compatible	= "ti,am3352-ecap" },
	{ .compatible	= "ti,am33xx-ecap" },
	{},
};
MODULE_DEVICE_TABLE(of, ecap_of_match);

static int ecap_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ecap_pwm_chip *pc;
	struct clk *clk;
	int ret;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	clk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(clk)) {
		if (of_device_is_compatible(np, "ti,am33xx-ecap")) {
			dev_warn(&pdev->dev, "Binding is obsolete.\n");
			clk = devm_clk_get(pdev->dev.parent, "fck");
		}
	}

	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}

	pc->clk_rate = clk_get_rate(clk);
	if (!pc->clk_rate) {
		dev_err(&pdev->dev, "failed to get clock rate\n");
		return -EINVAL;
	}

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &ecap_pwm_ops;
	pc->chip.npwm = 1;

	pc->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->mmio_base))
		return PTR_ERR(pc->mmio_base);

	ret = devm_pwmchip_add(&pdev->dev, &pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pc);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static void ecap_pwm_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static void ecap_pwm_save_context(struct ecap_pwm_chip *pc)
{
	pm_runtime_get_sync(pc->chip.dev);
	pc->ctx.ecctl2 = readw(pc->mmio_base + ECCTL2);
	pc->ctx.cap4 = readl(pc->mmio_base + CAP4);
	pc->ctx.cap3 = readl(pc->mmio_base + CAP3);
	pm_runtime_put_sync(pc->chip.dev);
}

static void ecap_pwm_restore_context(struct ecap_pwm_chip *pc)
{
	writel(pc->ctx.cap3, pc->mmio_base + CAP3);
	writel(pc->ctx.cap4, pc->mmio_base + CAP4);
	writew(pc->ctx.ecctl2, pc->mmio_base + ECCTL2);
}

static int ecap_pwm_suspend(struct device *dev)
{
	struct ecap_pwm_chip *pc = dev_get_drvdata(dev);
	struct pwm_device *pwm = pc->chip.pwms;

	ecap_pwm_save_context(pc);

	/* Disable explicitly if PWM is running */
	if (pwm_is_enabled(pwm))
		pm_runtime_put_sync(dev);

	return 0;
}

static int ecap_pwm_resume(struct device *dev)
{
	struct ecap_pwm_chip *pc = dev_get_drvdata(dev);
	struct pwm_device *pwm = pc->chip.pwms;

	/* Enable explicitly if PWM was running */
	if (pwm_is_enabled(pwm))
		pm_runtime_get_sync(dev);

	ecap_pwm_restore_context(pc);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ecap_pwm_pm_ops, ecap_pwm_suspend, ecap_pwm_resume);

static struct platform_driver ecap_pwm_driver = {
	.driver = {
		.name = "ecap",
		.of_match_table = ecap_of_match,
		.pm = &ecap_pwm_pm_ops,
	},
	.probe = ecap_pwm_probe,
	.remove_new = ecap_pwm_remove,
};
module_platform_driver(ecap_pwm_driver);

MODULE_DESCRIPTION("ECAP PWM driver");
MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");
