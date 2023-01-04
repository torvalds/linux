// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Damon Ding <damon.ding@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/time.h>

#include "rkx110_x120.h"
#include "rkx120_reg.h"

/*
 * regs for pwm v1-v3
 */
#define PWM_CTRL_TIMER_EN	(1 << 0)
#define PWM_CTRL_OUTPUT_EN	(1 << 3)

#define PWM_ENABLE		(1 << 0)
#define PWM_MODE_SHIFT		1
#define PWM_MODE_MASK		(0x3 << PWM_MODE_SHIFT)
#define PWM_ONESHOT		(0 << PWM_MODE_SHIFT)
#define PWM_CONTINUOUS		(1 << PWM_MODE_SHIFT)
#define PWM_CAPTURE		(2 << PWM_MODE_SHIFT)
#define PWM_DUTY_POSITIVE	(1 << 3)
#define PWM_DUTY_NEGATIVE	(0 << 3)
#define PWM_INACTIVE_NEGATIVE	(0 << 4)
#define PWM_INACTIVE_POSITIVE	(1 << 4)
#define PWM_POLARITY_MASK	(PWM_DUTY_POSITIVE | PWM_INACTIVE_POSITIVE)
#define PWM_OUTPUT_LEFT		(0 << 5)
#define PWM_OUTPUT_CENTER	(1 << 5)
#define PWM_LOCK_EN		(1 << 6)
#define PWM_LP_DISABLE		(0 << 8)
#define PWM_CLK_SEL_SHIFT	9
#define PWM_CLK_SEL_MASK	(1 << PWM_CLK_SEL_SHIFT)
#define PWM_SEL_NO_SCALED_CLOCK	(0 << PWM_CLK_SEL_SHIFT)
#define PWM_SEL_SCALED_CLOCK	(1 << PWM_CLK_SEL_SHIFT)
#define PWM_PRESCELE_SHIFT	12
#define PWM_PRESCALE_MASK	(0x3 << PWM_PRESCELE_SHIFT)
#define PWM_SCALE_SHIFT		16
#define PWM_SCALE_MASK		(0xff << PWM_SCALE_SHIFT)

#define PWM_ONESHOT_COUNT_SHIFT	24
#define PWM_ONESHOT_COUNT_MASK	(0xff << PWM_ONESHOT_COUNT_SHIFT)

#define PWM_REG_INTSTS(n)	((3 - (n)) * 0x10 + 0x10)
#define PWM_REG_INT_EN(n)	((3 - (n)) * 0x10 + 0x14)

#define PWM_CH_INT(n)		BIT(n)

#define PWM_DCLK_RATE		24000000

struct rkx120_pwm_chip {
	struct pwm_chip chip;
	struct rk_serdes *serdes;
	const struct rkx120_pwm_data *data;
	unsigned long clk_rate;
	bool center_aligned;
	bool oneshot_en;
	u32 remote_id;
	u32 channel_id;
};

struct rkx120_pwm_regs {
	unsigned long base;
	unsigned long duty;
	unsigned long period;
	unsigned long cntr;
	unsigned long ctrl;
};

struct rkx120_pwm_data {
	struct rkx120_pwm_regs regs;
	unsigned int prescaler;
	bool supports_polarity;
	bool supports_lock;
	u32 enable_conf;
	u32 enable_conf_mask;
	u32 oneshot_cnt_max;
};

static inline int rkx120_pwm_write(struct rk_serdes *serdes, u8 remote_id, u32 reg, u32 val)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	return serdes->i2c_write_reg(client, reg, val);
}

static inline int rkx120_pwm_read(struct rk_serdes *serdes, u8 remote_id, u32 reg, u32 *val)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	return serdes->i2c_read_reg(client, reg, val);
}

static inline struct rkx120_pwm_chip *to_rkx120_pwm_chip(struct pwm_chip *c)
{
	return container_of(c, struct rkx120_pwm_chip, chip);
}

static void rkx120_pwm_get_state(struct pwm_chip *chip,
				    struct pwm_device *pwm,
				    struct pwm_state *state)
{
	struct rkx120_pwm_chip *pc = to_rkx120_pwm_chip(chip);
	u32 enable_conf = pc->data->enable_conf;
	u64 tmp;
	u32 val;

	rkx120_pwm_read(pc->serdes, pc->remote_id, PWM_PERIOD_HPR(pc->channel_id), &val);
	tmp = val * pc->data->prescaler * NSEC_PER_SEC;
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	rkx120_pwm_read(pc->serdes, pc->remote_id, PWM_DUTY_LPR(pc->channel_id), &val);
	tmp = val * pc->data->prescaler * NSEC_PER_SEC;
	state->duty_cycle =  DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	rkx120_pwm_read(pc->serdes, pc->remote_id, PWM_CTRL(pc->channel_id), &val);
	if (pc->oneshot_en)
		enable_conf &= ~PWM_CONTINUOUS;
	state->enabled = (val & enable_conf) == enable_conf;

	if (pc->data->supports_polarity && !(val & PWM_DUTY_POSITIVE))
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;
}

static void rkx120_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				 const struct pwm_state *state)
{
	struct rkx120_pwm_chip *pc = to_rkx120_pwm_chip(chip);
	unsigned long period, duty, delay_ns;
	u64 div;
	u32 ctrl;
	u8 dclk_div = 1;

#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if (state->oneshot_count > 0 && state->oneshot_count <= pc->data->oneshot_cnt_max)
		dclk_div = 2;
#endif

	/*
	 * Since period and duty cycle registers have a width of 32
	 * bits, every possible input period can be obtained using the
	 * default prescaler value for all practical clock rate values.
	 */
	div = (u64)pc->clk_rate * state->period;
	period = DIV_ROUND_CLOSEST_ULL(div, dclk_div * pc->data->prescaler * NSEC_PER_SEC);

	div = (u64)pc->clk_rate * state->duty_cycle;
	duty = DIV_ROUND_CLOSEST_ULL(div, dclk_div * pc->data->prescaler * NSEC_PER_SEC);

	if (pc->data->supports_lock) {
		div = (u64)10 * NSEC_PER_SEC * dclk_div * pc->data->prescaler;
		delay_ns = DIV_ROUND_UP_ULL(div, pc->clk_rate);
	}

	/*
	 * Lock the period and duty of previous configuration, then
	 * change the duty and period, that would not be effective.
	 */
	rkx120_pwm_read(pc->serdes, pc->remote_id, PWM_CTRL(pc->channel_id), &ctrl);

#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if (state->oneshot_count > 0 && state->oneshot_count <= pc->data->oneshot_cnt_max) {
		/*
		 * This is a workaround, an uncertain waveform will be
		 * generated after oneshot ends. It is needed to enable
		 * the dclk scale function to resolve it. It doesn't
		 * matter what the scale factor is, just make sure the
		 * scale function is turned on, for which we set scale
		 * factor to 2.
		 */
		ctrl &= ~PWM_SCALE_MASK;
		ctrl |= (dclk_div / 2) << PWM_SCALE_SHIFT;
		ctrl &= ~PWM_CLK_SEL_MASK;
		ctrl |= PWM_SEL_SCALED_CLOCK;

		pc->oneshot_en = true;
		ctrl &= ~PWM_MODE_MASK;
		ctrl |= PWM_ONESHOT;

		ctrl &= ~PWM_ONESHOT_COUNT_MASK;
		ctrl |= (state->oneshot_count - 1) << PWM_ONESHOT_COUNT_SHIFT;
	} else {
		ctrl &= ~PWM_SCALE_MASK;
		ctrl &= ~PWM_CLK_SEL_MASK;
		ctrl |= PWM_SEL_NO_SCALED_CLOCK;

		if (state->oneshot_count)
			dev_err(chip->dev, "Oneshot_count must be between 1 and %d.\n",
				pc->data->oneshot_cnt_max);

		pc->oneshot_en = false;
		ctrl &= ~PWM_MODE_MASK;
		ctrl |= PWM_CONTINUOUS;

		ctrl &= ~PWM_ONESHOT_COUNT_MASK;
	}
#endif

	/*
	 * Lock the period and duty of previous configuration, then
	 * change the duty and period, that would not be effective.
	 */
	if (pc->data->supports_lock) {
		ctrl |= PWM_LOCK_EN;
		rkx120_pwm_write(pc->serdes, pc->remote_id, PWM_CTRL(pc->channel_id), ctrl);
	}

	rkx120_pwm_write(pc->serdes, pc->remote_id, PWM_PERIOD_HPR(pc->channel_id), period);
	rkx120_pwm_write(pc->serdes, pc->remote_id, PWM_DUTY_LPR(pc->channel_id), duty);

	if (pc->data->supports_polarity) {
		ctrl &= ~PWM_POLARITY_MASK;
		if (state->polarity == PWM_POLARITY_INVERSED)
			ctrl |= PWM_DUTY_NEGATIVE | PWM_INACTIVE_POSITIVE;
		else
			ctrl |= PWM_DUTY_POSITIVE | PWM_INACTIVE_NEGATIVE;
	}

	/*
	 * Unlock and set polarity at the same time, the configuration of duty,
	 * period and polarity would be effective together at next period. It
	 * takes 10 dclk cycles to make sure lock works before unlocking.
	 */
	if (pc->data->supports_lock) {
		ctrl &= ~PWM_LOCK_EN;
		ndelay(delay_ns);
	}

	rkx120_pwm_write(pc->serdes, pc->remote_id, PWM_CTRL(pc->channel_id), ctrl);
}

static int rkx120_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm, bool enable)
{
	struct rkx120_pwm_chip *pc = to_rkx120_pwm_chip(chip);
	u32 enable_conf = pc->data->enable_conf;
	u32 val;

	rkx120_pwm_read(pc->serdes, pc->remote_id, PWM_CTRL(pc->channel_id), &val);
	val &= ~pc->data->enable_conf_mask;

	if (pc->data->enable_conf_mask & PWM_OUTPUT_CENTER) {
		if (pc->center_aligned)
			val |= PWM_OUTPUT_CENTER;
	}

	if (enable) {
		val |= enable_conf;
		if (pc->oneshot_en)
			val &= ~PWM_CONTINUOUS;
	} else {
		val &= ~enable_conf;
	}

	rkx120_pwm_write(pc->serdes, pc->remote_id, PWM_CTRL(pc->channel_id), val);

	return 0;
}

static int rkx120_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			       const struct pwm_state *state)
{
	struct rkx120_pwm_chip *pc = to_rkx120_pwm_chip(chip);
	struct pwm_state curstate;
	bool enabled;
	int ret = 0;

	pwm_get_state(pwm, &curstate);
	enabled = curstate.enabled;

	if (state->polarity != curstate.polarity && enabled &&
	    !pc->data->supports_lock) {
		ret = rkx120_pwm_enable(chip, pwm, false);
		if (ret)
			return ret;
		enabled = false;
	}

	rkx120_pwm_config(chip, pwm, state);
	if (state->enabled != enabled) {
		ret = rkx120_pwm_enable(chip, pwm, state->enabled);
		if (ret)
			return ret;
	}

	return ret;
}

static const struct pwm_ops rkx120_pwm_ops = {
	.get_state = rkx120_pwm_get_state,
	.apply = rkx120_pwm_apply,
	.owner = THIS_MODULE,
};

static const struct rkx120_pwm_data rkx120_pwm_data = {
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = true,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
	.enable_conf_mask = GENMASK(2, 0) | BIT(5) | BIT(8),
	.oneshot_cnt_max = 0x100,
};

static const struct of_device_id rkx120_pwm_dt_ids[] = {
	{ .compatible = "rockchip,rkx120-pwm", .data = &rkx120_pwm_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rkx120_pwm_dt_ids);

static int rkx120_pwm_probe(struct platform_device *pdev)
{
	struct rk_serdes *serdes = dev_get_drvdata(pdev->dev.parent);
	const struct of_device_id *id;
	struct rkx120_pwm_chip *pc;
	u32 remote_id, channel_id;
	int ret;

	id = of_match_device(rkx120_pwm_dt_ids, &pdev->dev);
	if (!id)
		return -EINVAL;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	platform_set_drvdata(pdev, pc);

	pc->data = id->data;
	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &rkx120_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = 1;
	if (pc->data->supports_polarity) {
		pc->chip.of_xlate = of_pwm_xlate_with_flags;
		pc->chip.of_pwm_n_cells = 3;
	}

	pc->clk_rate = PWM_DCLK_RATE;
	pc->serdes = serdes;
	pc->center_aligned = device_property_read_bool(&pdev->dev, "center-aligned");

	ret = of_property_read_u32(pdev->dev.of_node, "channel-id", &channel_id);
	if (ret) {
		dev_err(&pdev->dev, "failed to read pwm channel id\n");
		return ret;
	}
	pc->channel_id = channel_id;

	ret = of_property_read_u32(pdev->dev.of_node, "remote-id", &remote_id);
	if (ret) {
		dev_err(&pdev->dev, "failed to read pwm remote id\n");
		return ret;
	}
	pc->remote_id = remote_id;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rkx120_pwm_remove(struct platform_device *pdev)
{
	struct rkx120_pwm_chip *pc = platform_get_drvdata(pdev);

	return pwmchip_remove(&pc->chip);
}

static struct platform_driver rkx120_pwm_driver = {
	.driver = {
		.name = "rkx120-pwm",
		.of_match_table = rkx120_pwm_dt_ids,
	},
	.probe = rkx120_pwm_probe,
	.remove = rkx120_pwm_remove,
};
module_platform_driver(rkx120_pwm_driver);
