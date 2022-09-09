// SPDX-License-Identifier: GPL-2.0
/*
 * STM32 Low-Power Timer Encoder and Counter driver
 *
 * Copyright (C) STMicroelectronics 2017
 *
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>
 *
 * Inspired by 104-quad-8 and stm32-timer-trigger drivers.
 *
 */

#include <linux/bitfield.h>
#include <linux/counter.h>
#include <linux/mfd/stm32-lptimer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/types.h>

struct stm32_lptim_cnt {
	struct device *dev;
	struct regmap *regmap;
	struct clk *clk;
	u32 ceiling;
	u32 polarity;
	u32 quadrature_mode;
	bool enabled;
};

static int stm32_lptim_is_enabled(struct stm32_lptim_cnt *priv)
{
	u32 val;
	int ret;

	ret = regmap_read(priv->regmap, STM32_LPTIM_CR, &val);
	if (ret)
		return ret;

	return FIELD_GET(STM32_LPTIM_ENABLE, val);
}

static int stm32_lptim_set_enable_state(struct stm32_lptim_cnt *priv,
					int enable)
{
	int ret;
	u32 val;

	val = FIELD_PREP(STM32_LPTIM_ENABLE, enable);
	ret = regmap_write(priv->regmap, STM32_LPTIM_CR, val);
	if (ret)
		return ret;

	if (!enable) {
		clk_disable(priv->clk);
		priv->enabled = false;
		return 0;
	}

	/* LP timer must be enabled before writing CMP & ARR */
	ret = regmap_write(priv->regmap, STM32_LPTIM_ARR, priv->ceiling);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, STM32_LPTIM_CMP, 0);
	if (ret)
		return ret;

	/* ensure CMP & ARR registers are properly written */
	ret = regmap_read_poll_timeout(priv->regmap, STM32_LPTIM_ISR, val,
				       (val & STM32_LPTIM_CMPOK_ARROK),
				       100, 1000);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, STM32_LPTIM_ICR,
			   STM32_LPTIM_CMPOKCF_ARROKCF);
	if (ret)
		return ret;

	ret = clk_enable(priv->clk);
	if (ret) {
		regmap_write(priv->regmap, STM32_LPTIM_CR, 0);
		return ret;
	}
	priv->enabled = true;

	/* Start LP timer in continuous mode */
	return regmap_update_bits(priv->regmap, STM32_LPTIM_CR,
				  STM32_LPTIM_CNTSTRT, STM32_LPTIM_CNTSTRT);
}

static int stm32_lptim_setup(struct stm32_lptim_cnt *priv, int enable)
{
	u32 mask = STM32_LPTIM_ENC | STM32_LPTIM_COUNTMODE |
		   STM32_LPTIM_CKPOL | STM32_LPTIM_PRESC;
	u32 val;

	/* Setup LP timer encoder/counter and polarity, without prescaler */
	if (priv->quadrature_mode)
		val = enable ? STM32_LPTIM_ENC : 0;
	else
		val = enable ? STM32_LPTIM_COUNTMODE : 0;
	val |= FIELD_PREP(STM32_LPTIM_CKPOL, enable ? priv->polarity : 0);

	return regmap_update_bits(priv->regmap, STM32_LPTIM_CFGR, mask, val);
}

/*
 * In non-quadrature mode, device counts up on active edge.
 * In quadrature mode, encoder counting scenarios are as follows:
 * +---------+----------+--------------------+--------------------+
 * | Active  | Level on |      IN1 signal    |     IN2 signal     |
 * | edge    | opposite +----------+---------+----------+---------+
 * |         | signal   |  Rising  | Falling |  Rising  | Falling |
 * +---------+----------+----------+---------+----------+---------+
 * | Rising  | High ->  |   Down   |    -    |   Up     |    -    |
 * | edge    | Low  ->  |   Up     |    -    |   Down   |    -    |
 * +---------+----------+----------+---------+----------+---------+
 * | Falling | High ->  |    -     |   Up    |    -     |   Down  |
 * | edge    | Low  ->  |    -     |   Down  |    -     |   Up    |
 * +---------+----------+----------+---------+----------+---------+
 * | Both    | High ->  |   Down   |   Up    |   Up     |   Down  |
 * | edges   | Low  ->  |   Up     |   Down  |   Down   |   Up    |
 * +---------+----------+----------+---------+----------+---------+
 */
static const enum counter_function stm32_lptim_cnt_functions[] = {
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_QUADRATURE_X4,
};

static const enum counter_synapse_action stm32_lptim_cnt_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	COUNTER_SYNAPSE_ACTION_NONE,
};

static int stm32_lptim_cnt_read(struct counter_device *counter,
				struct counter_count *count, u64 *val)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);
	u32 cnt;
	int ret;

	ret = regmap_read(priv->regmap, STM32_LPTIM_CNT, &cnt);
	if (ret)
		return ret;

	*val = cnt;

	return 0;
}

static int stm32_lptim_cnt_function_read(struct counter_device *counter,
					 struct counter_count *count,
					 enum counter_function *function)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);

	if (!priv->quadrature_mode) {
		*function = COUNTER_FUNCTION_INCREASE;
		return 0;
	}

	if (priv->polarity == STM32_LPTIM_CKPOL_BOTH_EDGES) {
		*function = COUNTER_FUNCTION_QUADRATURE_X4;
		return 0;
	}

	return -EINVAL;
}

static int stm32_lptim_cnt_function_write(struct counter_device *counter,
					  struct counter_count *count,
					  enum counter_function function)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);

	if (stm32_lptim_is_enabled(priv))
		return -EBUSY;

	switch (function) {
	case COUNTER_FUNCTION_INCREASE:
		priv->quadrature_mode = 0;
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X4:
		priv->quadrature_mode = 1;
		priv->polarity = STM32_LPTIM_CKPOL_BOTH_EDGES;
		return 0;
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

static int stm32_lptim_cnt_enable_read(struct counter_device *counter,
				       struct counter_count *count,
				       u8 *enable)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);
	int ret;

	ret = stm32_lptim_is_enabled(priv);
	if (ret < 0)
		return ret;

	*enable = ret;

	return 0;
}

static int stm32_lptim_cnt_enable_write(struct counter_device *counter,
					struct counter_count *count,
					u8 enable)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);
	int ret;

	/* Check nobody uses the timer, or already disabled/enabled */
	ret = stm32_lptim_is_enabled(priv);
	if ((ret < 0) || (!ret && !enable))
		return ret;
	if (enable && ret)
		return -EBUSY;

	ret = stm32_lptim_setup(priv, enable);
	if (ret)
		return ret;

	ret = stm32_lptim_set_enable_state(priv, enable);
	if (ret)
		return ret;

	return 0;
}

static int stm32_lptim_cnt_ceiling_read(struct counter_device *counter,
					struct counter_count *count,
					u64 *ceiling)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);

	*ceiling = priv->ceiling;

	return 0;
}

static int stm32_lptim_cnt_ceiling_write(struct counter_device *counter,
					 struct counter_count *count,
					 u64 ceiling)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);

	if (stm32_lptim_is_enabled(priv))
		return -EBUSY;

	if (ceiling > STM32_LPTIM_MAX_ARR)
		return -ERANGE;

	priv->ceiling = ceiling;

	return 0;
}

static struct counter_comp stm32_lptim_cnt_ext[] = {
	COUNTER_COMP_ENABLE(stm32_lptim_cnt_enable_read,
			    stm32_lptim_cnt_enable_write),
	COUNTER_COMP_CEILING(stm32_lptim_cnt_ceiling_read,
			     stm32_lptim_cnt_ceiling_write),
};

static int stm32_lptim_cnt_action_read(struct counter_device *counter,
				       struct counter_count *count,
				       struct counter_synapse *synapse,
				       enum counter_synapse_action *action)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);
	enum counter_function function;
	int err;

	err = stm32_lptim_cnt_function_read(counter, count, &function);
	if (err)
		return err;

	switch (function) {
	case COUNTER_FUNCTION_INCREASE:
		/* LP Timer acts as up-counter on input 1 */
		if (synapse->signal->id != count->synapses[0].signal->id) {
			*action = COUNTER_SYNAPSE_ACTION_NONE;
			return 0;
		}

		switch (priv->polarity) {
		case STM32_LPTIM_CKPOL_RISING_EDGE:
			*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
			return 0;
		case STM32_LPTIM_CKPOL_FALLING_EDGE:
			*action = COUNTER_SYNAPSE_ACTION_FALLING_EDGE;
			return 0;
		case STM32_LPTIM_CKPOL_BOTH_EDGES:
			*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
			return 0;
		default:
			/* should never reach this path */
			return -EINVAL;
		}
	case COUNTER_FUNCTION_QUADRATURE_X4:
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		return 0;
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

static int stm32_lptim_cnt_action_write(struct counter_device *counter,
					struct counter_count *count,
					struct counter_synapse *synapse,
					enum counter_synapse_action action)
{
	struct stm32_lptim_cnt *const priv = counter_priv(counter);
	enum counter_function function;
	int err;

	if (stm32_lptim_is_enabled(priv))
		return -EBUSY;

	err = stm32_lptim_cnt_function_read(counter, count, &function);
	if (err)
		return err;

	/* only set polarity when in counter mode (on input 1) */
	if (function != COUNTER_FUNCTION_INCREASE
	    || synapse->signal->id != count->synapses[0].signal->id)
		return -EINVAL;

	switch (action) {
	case COUNTER_SYNAPSE_ACTION_RISING_EDGE:
		priv->polarity = STM32_LPTIM_CKPOL_RISING_EDGE;
		return 0;
	case COUNTER_SYNAPSE_ACTION_FALLING_EDGE:
		priv->polarity = STM32_LPTIM_CKPOL_FALLING_EDGE;
		return 0;
	case COUNTER_SYNAPSE_ACTION_BOTH_EDGES:
		priv->polarity = STM32_LPTIM_CKPOL_BOTH_EDGES;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct counter_ops stm32_lptim_cnt_ops = {
	.count_read = stm32_lptim_cnt_read,
	.function_read = stm32_lptim_cnt_function_read,
	.function_write = stm32_lptim_cnt_function_write,
	.action_read = stm32_lptim_cnt_action_read,
	.action_write = stm32_lptim_cnt_action_write,
};

static struct counter_signal stm32_lptim_cnt_signals[] = {
	{
		.id = 0,
		.name = "Channel 1 Quadrature A"
	},
	{
		.id = 1,
		.name = "Channel 1 Quadrature B"
	}
};

static struct counter_synapse stm32_lptim_cnt_synapses[] = {
	{
		.actions_list = stm32_lptim_cnt_synapse_actions,
		.num_actions = ARRAY_SIZE(stm32_lptim_cnt_synapse_actions),
		.signal = &stm32_lptim_cnt_signals[0]
	},
	{
		.actions_list = stm32_lptim_cnt_synapse_actions,
		.num_actions = ARRAY_SIZE(stm32_lptim_cnt_synapse_actions),
		.signal = &stm32_lptim_cnt_signals[1]
	}
};

/* LP timer with encoder */
static struct counter_count stm32_lptim_enc_counts = {
	.id = 0,
	.name = "LPTimer Count",
	.functions_list = stm32_lptim_cnt_functions,
	.num_functions = ARRAY_SIZE(stm32_lptim_cnt_functions),
	.synapses = stm32_lptim_cnt_synapses,
	.num_synapses = ARRAY_SIZE(stm32_lptim_cnt_synapses),
	.ext = stm32_lptim_cnt_ext,
	.num_ext = ARRAY_SIZE(stm32_lptim_cnt_ext)
};

/* LP timer without encoder (counter only) */
static struct counter_count stm32_lptim_in1_counts = {
	.id = 0,
	.name = "LPTimer Count",
	.functions_list = stm32_lptim_cnt_functions,
	.num_functions = 1,
	.synapses = stm32_lptim_cnt_synapses,
	.num_synapses = 1,
	.ext = stm32_lptim_cnt_ext,
	.num_ext = ARRAY_SIZE(stm32_lptim_cnt_ext)
};

static int stm32_lptim_cnt_probe(struct platform_device *pdev)
{
	struct stm32_lptimer *ddata = dev_get_drvdata(pdev->dev.parent);
	struct counter_device *counter;
	struct stm32_lptim_cnt *priv;
	int ret;

	if (IS_ERR_OR_NULL(ddata))
		return -EINVAL;

	counter = devm_counter_alloc(&pdev->dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);

	priv->dev = &pdev->dev;
	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->ceiling = STM32_LPTIM_MAX_ARR;

	/* Initialize Counter device */
	counter->name = dev_name(&pdev->dev);
	counter->parent = &pdev->dev;
	counter->ops = &stm32_lptim_cnt_ops;
	if (ddata->has_encoder) {
		counter->counts = &stm32_lptim_enc_counts;
		counter->num_signals = ARRAY_SIZE(stm32_lptim_cnt_signals);
	} else {
		counter->counts = &stm32_lptim_in1_counts;
		counter->num_signals = 1;
	}
	counter->num_counts = 1;
	counter->signals = stm32_lptim_cnt_signals;

	platform_set_drvdata(pdev, priv);

	ret = devm_counter_add(&pdev->dev, counter);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to add counter\n");

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stm32_lptim_cnt_suspend(struct device *dev)
{
	struct stm32_lptim_cnt *priv = dev_get_drvdata(dev);
	int ret;

	/* Only take care of enabled counter: don't disturb other MFD child */
	if (priv->enabled) {
		ret = stm32_lptim_setup(priv, 0);
		if (ret)
			return ret;

		ret = stm32_lptim_set_enable_state(priv, 0);
		if (ret)
			return ret;

		/* Force enable state for later resume */
		priv->enabled = true;
	}

	return pinctrl_pm_select_sleep_state(dev);
}

static int stm32_lptim_cnt_resume(struct device *dev)
{
	struct stm32_lptim_cnt *priv = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret)
		return ret;

	if (priv->enabled) {
		priv->enabled = false;
		ret = stm32_lptim_setup(priv, 1);
		if (ret)
			return ret;

		ret = stm32_lptim_set_enable_state(priv, 1);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(stm32_lptim_cnt_pm_ops, stm32_lptim_cnt_suspend,
			 stm32_lptim_cnt_resume);

static const struct of_device_id stm32_lptim_cnt_of_match[] = {
	{ .compatible = "st,stm32-lptimer-counter", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_lptim_cnt_of_match);

static struct platform_driver stm32_lptim_cnt_driver = {
	.probe = stm32_lptim_cnt_probe,
	.driver = {
		.name = "stm32-lptimer-counter",
		.of_match_table = stm32_lptim_cnt_of_match,
		.pm = &stm32_lptim_cnt_pm_ops,
	},
};
module_platform_driver(stm32_lptim_cnt_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_ALIAS("platform:stm32-lptimer-counter");
MODULE_DESCRIPTION("STMicroelectronics STM32 LPTIM counter driver");
MODULE_LICENSE("GPL v2");
