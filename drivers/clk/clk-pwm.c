// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Philipp Zabel, Pengutronix
 *
 * PWM (mis)used as clock output
 */
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

struct clk_pwm {
	struct clk_hw hw;
	struct pwm_device *pwm;
	u32 fixed_rate;
};

static inline struct clk_pwm *to_clk_pwm(struct clk_hw *hw)
{
	return container_of(hw, struct clk_pwm, hw);
}

static int clk_pwm_prepare(struct clk_hw *hw)
{
	struct clk_pwm *clk_pwm = to_clk_pwm(hw);

	return pwm_enable(clk_pwm->pwm);
}

static void clk_pwm_unprepare(struct clk_hw *hw)
{
	struct clk_pwm *clk_pwm = to_clk_pwm(hw);

	pwm_disable(clk_pwm->pwm);
}

static unsigned long clk_pwm_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pwm *clk_pwm = to_clk_pwm(hw);

	return clk_pwm->fixed_rate;
}

static int clk_pwm_get_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	struct clk_pwm *clk_pwm = to_clk_pwm(hw);
	struct pwm_state state;

	pwm_get_state(clk_pwm->pwm, &state);

	duty->num = state.duty_cycle;
	duty->den = state.period;

	return 0;
}

static const struct clk_ops clk_pwm_ops = {
	.prepare = clk_pwm_prepare,
	.unprepare = clk_pwm_unprepare,
	.recalc_rate = clk_pwm_recalc_rate,
	.get_duty_cycle = clk_pwm_get_duty_cycle,
};

static int clk_pwm_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_init_data init;
	struct clk_pwm *clk_pwm;
	struct pwm_device *pwm;
	struct pwm_args pargs;
	const char *clk_name;
	int ret;

	clk_pwm = devm_kzalloc(&pdev->dev, sizeof(*clk_pwm), GFP_KERNEL);
	if (!clk_pwm)
		return -ENOMEM;

	pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(pwm))
		return PTR_ERR(pwm);

	pwm_get_args(pwm, &pargs);
	if (!pargs.period) {
		dev_err(&pdev->dev, "invalid PWM period\n");
		return -EINVAL;
	}

	if (of_property_read_u32(node, "clock-frequency", &clk_pwm->fixed_rate))
		clk_pwm->fixed_rate = div64_u64(NSEC_PER_SEC, pargs.period);

	if (!clk_pwm->fixed_rate) {
		dev_err(&pdev->dev, "fixed_rate cannot be zero\n");
		return -EINVAL;
	}

	if (pargs.period != NSEC_PER_SEC / clk_pwm->fixed_rate &&
	    pargs.period != DIV_ROUND_UP(NSEC_PER_SEC, clk_pwm->fixed_rate)) {
		dev_err(&pdev->dev,
			"clock-frequency does not match PWM period\n");
		return -EINVAL;
	}

	/*
	 * FIXME: pwm_apply_args() should be removed when switching to the
	 * atomic PWM API.
	 */
	pwm_apply_args(pwm);
	ret = pwm_config(pwm, (pargs.period + 1) >> 1, pargs.period);
	if (ret < 0)
		return ret;

	clk_name = node->name;
	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = &clk_pwm_ops;
	init.flags = 0;
	init.num_parents = 0;

	clk_pwm->pwm = pwm;
	clk_pwm->hw.init = &init;
	ret = devm_clk_hw_register(&pdev->dev, &clk_pwm->hw);
	if (ret)
		return ret;

	return of_clk_add_hw_provider(node, of_clk_hw_simple_get, &clk_pwm->hw);
}

static void clk_pwm_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
}

static const struct of_device_id clk_pwm_dt_ids[] = {
	{ .compatible = "pwm-clock" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_pwm_dt_ids);

static struct platform_driver clk_pwm_driver = {
	.probe = clk_pwm_probe,
	.remove_new = clk_pwm_remove,
	.driver = {
		.name = "pwm-clock",
		.of_match_table = clk_pwm_dt_ids,
	},
};

module_platform_driver(clk_pwm_driver);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("PWM clock driver");
MODULE_LICENSE("GPL");
