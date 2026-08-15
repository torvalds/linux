// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Copyright (c) 2016-2017, 2020 The Linux Foundation. All rights reserved.
 *
 * Limitations:
 * - The PWM controller has no publicly available datasheet.
 * - Each of the four channels is programmed via two 32-bit registers
 *   (REG0 and REG1 at 8-byte stride).
 * - Period and duty-cycle reconfiguration is fully atomic: new divider,
 *   pre-divider, and high-duration values are latched by setting the
 *   UPDATE bit (bit 30 in REG1). The hardware applies the new settings
 *   at the beginning of the next period without disabling the output,
 *   so the currently running period is always completed.
 * - On disable (clearing the ENABLE bit 31 in REG1), the hardware
 *   finishes the current period before stopping the output. The pin
 *   is then driven to the inactive (low) level.
 * - Upon disabling, the hardware resets the pre-divider (PRE_DIV) and divider
 *   fields (PWM_DIV) in REG0 and REG1 to 0x0000 and 0x0001 respectively.
 * - Only normal polarity is supported.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/math64.h>
#include <linux/of_device.h>
#include <linux/bitfield.h>
#include <linux/units.h>

/* The frequency range supported is 1 Hz to 100 Mhz (clock rate) */
#define IPQ_PWM_MAX_PERIOD_NS	((u64)NSEC_PER_SEC)
#define IPQ_PWM_MIN_PERIOD_NS	10

/*
 * Two 32-bit registers for each PWM: REG0, and REG1.
 * Base offset for PWM #i is at 8 * #i.
 */
#define IPQ_PWM_REG0			0
#define IPQ_PWM_REG0_PWM_DIV		GENMASK(15, 0)
#define IPQ_PWM_REG0_HI_DURATION	GENMASK(31, 16)

#define IPQ_PWM_REG1			4
#define IPQ_PWM_REG1_PRE_DIV		GENMASK(15, 0)
/*
 * Enable bit is set to enable output toggling in pwm device.
 * Update bit is set to trigger the change and is unset automatically
 * to reflect the changed divider and high duration values in register.
 */
#define IPQ_PWM_REG1_UPDATE		BIT(30)
#define IPQ_PWM_REG1_ENABLE		BIT(31)

/*
 * The max value specified for each field is based on the number of bits
 * in the pwm control register for that field (16-bit)
 */
#define IPQ_PWM_MAX_DIV			FIELD_MAX(IPQ_PWM_REG0_PWM_DIV)

struct ipq_pwm_chip {
	void __iomem *mem;
	unsigned long clk_rate;
};

static struct ipq_pwm_chip *ipq_pwm_from_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static unsigned int ipq_pwm_reg_read(struct pwm_device *pwm, unsigned int reg)
{
	struct ipq_pwm_chip *ipq_chip = ipq_pwm_from_chip(pwm->chip);
	unsigned int off = 8 * pwm->hwpwm + reg;

	return readl(ipq_chip->mem + off);
}

static void ipq_pwm_reg_write(struct pwm_device *pwm, unsigned int reg,
			      unsigned int val)
{
	struct ipq_pwm_chip *ipq_chip = ipq_pwm_from_chip(pwm->chip);
	unsigned int off = 8 * pwm->hwpwm + reg;

	writel(val, ipq_chip->mem + off);
}

static int ipq_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct ipq_pwm_chip *ipq_chip = ipq_pwm_from_chip(chip);
	unsigned int pre_div, pwm_div;
	u64 period_ns, duty_ns;
	unsigned long val = 0;
	unsigned long hi_dur;

	if (!state->enabled) {
		/* clear IPQ_PWM_REG1_ENABLE */
		ipq_pwm_reg_write(pwm, IPQ_PWM_REG1, IPQ_PWM_REG1_UPDATE);
		return 0;
	}

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/*
	 * Check the upper and lower bounds for the period as per
	 * hardware limits
	 */
	if (state->period < IPQ_PWM_MIN_PERIOD_NS)
		return -ERANGE;
	period_ns = min(state->period, IPQ_PWM_MAX_PERIOD_NS);
	duty_ns = min(state->duty_cycle, period_ns);

	/*
	 * Pick the maximal value for PWM_DIV that still allows a
	 * 100% relative duty cycle. This allows a fine grained
	 * selection of duty cycles.
	 */
	pwm_div = IPQ_PWM_MAX_DIV - 1;

	/*
	 * although mul_u64_u64_div_u64 returns a u64, in practice it
	 * won't overflow due to above constraints. Take the max period
	 * of 10^9 (NSEC_PER_SEC) and the pwm_div + 1 (IPQ_PWM_MAX_DIV)
	 *  10^9 * 10^8
	 * ------------- => which fits well into a 32-bit unsigned int.
	 * 10^9 * 65,535
	 */
	pre_div = mul_u64_u64_div_u64(period_ns, ipq_chip->clk_rate,
				      (u64)NSEC_PER_SEC * (pwm_div + 1));

	if (!pre_div)
		return -ERANGE;

	pre_div -= 1;

	if (pre_div > IPQ_PWM_MAX_DIV)
		pre_div = IPQ_PWM_MAX_DIV;

	/* pwm duty = HI_DUR * (PRE_DIV + 1) / clk_rate */
	hi_dur = mul_u64_u64_div_u64(duty_ns, ipq_chip->clk_rate,
				     (u64)NSEC_PER_SEC * (pre_div + 1));

	val = FIELD_PREP(IPQ_PWM_REG0_HI_DURATION, hi_dur) |
		FIELD_PREP(IPQ_PWM_REG0_PWM_DIV, pwm_div);
	ipq_pwm_reg_write(pwm, IPQ_PWM_REG0, val);

	val = FIELD_PREP(IPQ_PWM_REG1_PRE_DIV, pre_div);
	ipq_pwm_reg_write(pwm, IPQ_PWM_REG1, val);

	/* PWM enable toggle needs a separate write to REG1 */
	val |= IPQ_PWM_REG1_UPDATE | IPQ_PWM_REG1_ENABLE;
	ipq_pwm_reg_write(pwm, IPQ_PWM_REG1, val);

	return 0;
}

static int ipq_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct ipq_pwm_chip *ipq_chip = ipq_pwm_from_chip(chip);
	unsigned int pre_div, pwm_div, hi_dur;
	u64 effective_div, hi_div;
	u32 reg0, reg1;

	reg1 = ipq_pwm_reg_read(pwm, IPQ_PWM_REG1);
	state->enabled = reg1 & IPQ_PWM_REG1_ENABLE;

	if (!state->enabled)
		return 0;

	reg0 = ipq_pwm_reg_read(pwm, IPQ_PWM_REG0);

	state->polarity = PWM_POLARITY_NORMAL;

	pwm_div = FIELD_GET(IPQ_PWM_REG0_PWM_DIV, reg0);
	hi_dur = FIELD_GET(IPQ_PWM_REG0_HI_DURATION, reg0);
	pre_div = FIELD_GET(IPQ_PWM_REG1_PRE_DIV, reg1);

	effective_div = (u64)(pwm_div + 1) * (pre_div + 1);

	/*
	 * effective_div <= 0x100000000, so the multiplication doesn't overflow.
	 */
	state->period = DIV64_U64_ROUND_UP(effective_div * NSEC_PER_SEC,
					   ipq_chip->clk_rate);

	hi_div = hi_dur * (pre_div + 1);
	state->duty_cycle = DIV64_U64_ROUND_UP(hi_div * NSEC_PER_SEC,
					       ipq_chip->clk_rate);

	/*
	 * ensure a valid config is passed back to PWM core in case duty_cycle
	 * is > period (>100%)
	 */
	state->duty_cycle = min(state->duty_cycle, state->period);

	return 0;
}

static const struct pwm_ops ipq_pwm_ops = {
	.apply = ipq_pwm_apply,
	.get_state = ipq_pwm_get_state,
};

static int ipq_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ipq_pwm_chip *pwm;
	struct pwm_chip *chip;
	struct clk *clk;
	int ret;

	chip = devm_pwmchip_alloc(dev, 4, sizeof(*pwm));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pwm = ipq_pwm_from_chip(chip);

	pwm->mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pwm->mem))
		return dev_err_probe(dev, PTR_ERR(pwm->mem),
				     "Failed to acquire resource\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "Failed to get clock\n");

	ret = devm_clk_rate_exclusive_get(dev, clk);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to lock clock rate\n");

	pwm->clk_rate = clk_get_rate(clk);
	if (!pwm->clk_rate)
		return dev_err_probe(dev, -EINVAL, "Failed due to clock rate being zero\n");

	chip->ops = &ipq_pwm_ops;

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to add pwm chip\n");

	return 0;
}

static const struct of_device_id pwm_ipq_dt_match[] = {
	{ .compatible = "qcom,ipq6018-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, pwm_ipq_dt_match);

static struct platform_driver ipq_pwm_driver = {
	.driver = {
		.name = "ipq-pwm",
		.of_match_table = pwm_ipq_dt_match,
	},
	.probe = ipq_pwm_probe,
};

module_platform_driver(ipq_pwm_driver);

MODULE_DESCRIPTION("Qualcomm IPQ PWM driver");
MODULE_LICENSE("GPL");
