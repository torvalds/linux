// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform PWM support
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include <asm/mach-jz4740/timer.h>

#define NUM_PWM 8

struct jz4740_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
};

static inline struct jz4740_pwm_chip *to_jz4740(struct pwm_chip *chip)
{
	return container_of(chip, struct jz4740_pwm_chip, chip);
}

static int jz4740_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	/*
	 * Timers 0 and 1 are used for system tasks, so they are unavailable
	 * for use as PWMs.
	 */
	if (pwm->hwpwm < 2)
		return -EBUSY;

	jz4740_timer_start(pwm->hwpwm);

	return 0;
}

static void jz4740_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	jz4740_timer_set_ctrl(pwm->hwpwm, 0);

	jz4740_timer_stop(pwm->hwpwm);
}

static int jz4740_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	uint32_t ctrl = jz4740_timer_get_ctrl(pwm->pwm);

	ctrl |= JZ_TIMER_CTRL_PWM_ENABLE;
	jz4740_timer_set_ctrl(pwm->hwpwm, ctrl);
	jz4740_timer_enable(pwm->hwpwm);

	return 0;
}

static void jz4740_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	uint32_t ctrl = jz4740_timer_get_ctrl(pwm->hwpwm);

	/* Disable PWM output.
	 * In TCU2 mode (channel 1/2 on JZ4750+), this must be done before the
	 * counter is stopped, while in TCU1 mode the order does not matter.
	 */
	ctrl &= ~JZ_TIMER_CTRL_PWM_ENABLE;
	jz4740_timer_set_ctrl(pwm->hwpwm, ctrl);

	/* Stop counter */
	jz4740_timer_disable(pwm->hwpwm);
}

static int jz4740_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			     int duty_ns, int period_ns)
{
	struct jz4740_pwm_chip *jz4740 = to_jz4740(pwm->chip);
	unsigned long long tmp;
	unsigned long period, duty;
	unsigned int prescaler = 0;
	uint16_t ctrl;
	bool is_enabled;

	tmp = (unsigned long long)clk_get_rate(jz4740->clk) * period_ns;
	do_div(tmp, 1000000000);
	period = tmp;

	while (period > 0xffff && prescaler < 6) {
		period >>= 2;
		++prescaler;
	}

	if (prescaler == 6)
		return -EINVAL;

	tmp = (unsigned long long)period * duty_ns;
	do_div(tmp, period_ns);
	duty = period - tmp;

	if (duty >= period)
		duty = period - 1;

	is_enabled = jz4740_timer_is_enabled(pwm->hwpwm);
	if (is_enabled)
		jz4740_pwm_disable(chip, pwm);

	jz4740_timer_set_count(pwm->hwpwm, 0);
	jz4740_timer_set_duty(pwm->hwpwm, duty);
	jz4740_timer_set_period(pwm->hwpwm, period);

	ctrl = JZ_TIMER_CTRL_PRESCALER(prescaler) | JZ_TIMER_CTRL_SRC_EXT |
		JZ_TIMER_CTRL_PWM_ABBRUPT_SHUTDOWN;

	jz4740_timer_set_ctrl(pwm->hwpwm, ctrl);

	if (is_enabled)
		jz4740_pwm_enable(chip, pwm);

	return 0;
}

static int jz4740_pwm_set_polarity(struct pwm_chip *chip,
		struct pwm_device *pwm, enum pwm_polarity polarity)
{
	uint32_t ctrl = jz4740_timer_get_ctrl(pwm->pwm);

	switch (polarity) {
	case PWM_POLARITY_NORMAL:
		ctrl &= ~JZ_TIMER_CTRL_PWM_ACTIVE_LOW;
		break;
	case PWM_POLARITY_INVERSED:
		ctrl |= JZ_TIMER_CTRL_PWM_ACTIVE_LOW;
		break;
	}

	jz4740_timer_set_ctrl(pwm->hwpwm, ctrl);
	return 0;
}

static const struct pwm_ops jz4740_pwm_ops = {
	.request = jz4740_pwm_request,
	.free = jz4740_pwm_free,
	.config = jz4740_pwm_config,
	.set_polarity = jz4740_pwm_set_polarity,
	.enable = jz4740_pwm_enable,
	.disable = jz4740_pwm_disable,
	.owner = THIS_MODULE,
};

static int jz4740_pwm_probe(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740;

	jz4740 = devm_kzalloc(&pdev->dev, sizeof(*jz4740), GFP_KERNEL);
	if (!jz4740)
		return -ENOMEM;

	jz4740->clk = devm_clk_get(&pdev->dev, "ext");
	if (IS_ERR(jz4740->clk))
		return PTR_ERR(jz4740->clk);

	jz4740->chip.dev = &pdev->dev;
	jz4740->chip.ops = &jz4740_pwm_ops;
	jz4740->chip.npwm = NUM_PWM;
	jz4740->chip.base = -1;
	jz4740->chip.of_xlate = of_pwm_xlate_with_flags;
	jz4740->chip.of_pwm_n_cells = 3;

	platform_set_drvdata(pdev, jz4740);

	return pwmchip_add(&jz4740->chip);
}

static int jz4740_pwm_remove(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740 = platform_get_drvdata(pdev);

	return pwmchip_remove(&jz4740->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id jz4740_pwm_dt_ids[] = {
	{ .compatible = "ingenic,jz4740-pwm", },
	{ .compatible = "ingenic,jz4770-pwm", },
	{ .compatible = "ingenic,jz4780-pwm", },
	{},
};
MODULE_DEVICE_TABLE(of, jz4740_pwm_dt_ids);
#endif

static struct platform_driver jz4740_pwm_driver = {
	.driver = {
		.name = "jz4740-pwm",
		.of_match_table = of_match_ptr(jz4740_pwm_dt_ids),
	},
	.probe = jz4740_pwm_probe,
	.remove = jz4740_pwm_remove,
};
module_platform_driver(jz4740_pwm_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 PWM driver");
MODULE_ALIAS("platform:jz4740-pwm");
MODULE_LICENSE("GPL");
