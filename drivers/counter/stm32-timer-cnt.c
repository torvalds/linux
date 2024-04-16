// SPDX-License-Identifier: GPL-2.0
/*
 * STM32 Timer Encoder and Counter driver
 *
 * Copyright (C) STMicroelectronics 2018
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 *
 */
#include <linux/counter.h>
#include <linux/mfd/stm32-timers.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define TIM_CCMR_CCXS	(BIT(8) | BIT(0))
#define TIM_CCMR_MASK	(TIM_CCMR_CC1S | TIM_CCMR_CC2S | \
			 TIM_CCMR_IC1F | TIM_CCMR_IC2F)
#define TIM_CCER_MASK	(TIM_CCER_CC1P | TIM_CCER_CC1NP | \
			 TIM_CCER_CC2P | TIM_CCER_CC2NP)

struct stm32_timer_regs {
	u32 cr1;
	u32 cnt;
	u32 smcr;
	u32 arr;
};

struct stm32_timer_cnt {
	struct regmap *regmap;
	struct clk *clk;
	u32 max_arr;
	bool enabled;
	struct stm32_timer_regs bak;
};

static const enum counter_function stm32_count_functions[] = {
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_QUADRATURE_X2_A,
	COUNTER_FUNCTION_QUADRATURE_X2_B,
	COUNTER_FUNCTION_QUADRATURE_X4,
};

static int stm32_count_read(struct counter_device *counter,
			    struct counter_count *count, u64 *val)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 cnt;

	regmap_read(priv->regmap, TIM_CNT, &cnt);
	*val = cnt;

	return 0;
}

static int stm32_count_write(struct counter_device *counter,
			     struct counter_count *count, const u64 val)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 ceiling;

	regmap_read(priv->regmap, TIM_ARR, &ceiling);
	if (val > ceiling)
		return -EINVAL;

	return regmap_write(priv->regmap, TIM_CNT, val);
}

static int stm32_count_function_read(struct counter_device *counter,
				     struct counter_count *count,
				     enum counter_function *function)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 smcr;

	regmap_read(priv->regmap, TIM_SMCR, &smcr);

	switch (smcr & TIM_SMCR_SMS) {
	case TIM_SMCR_SMS_SLAVE_MODE_DISABLED:
		*function = COUNTER_FUNCTION_INCREASE;
		return 0;
	case TIM_SMCR_SMS_ENCODER_MODE_1:
		*function = COUNTER_FUNCTION_QUADRATURE_X2_A;
		return 0;
	case TIM_SMCR_SMS_ENCODER_MODE_2:
		*function = COUNTER_FUNCTION_QUADRATURE_X2_B;
		return 0;
	case TIM_SMCR_SMS_ENCODER_MODE_3:
		*function = COUNTER_FUNCTION_QUADRATURE_X4;
		return 0;
	default:
		return -EINVAL;
	}
}

static int stm32_count_function_write(struct counter_device *counter,
				      struct counter_count *count,
				      enum counter_function function)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 cr1, sms;

	switch (function) {
	case COUNTER_FUNCTION_INCREASE:
		sms = TIM_SMCR_SMS_SLAVE_MODE_DISABLED;
		break;
	case COUNTER_FUNCTION_QUADRATURE_X2_A:
		sms = TIM_SMCR_SMS_ENCODER_MODE_1;
		break;
	case COUNTER_FUNCTION_QUADRATURE_X2_B:
		sms = TIM_SMCR_SMS_ENCODER_MODE_2;
		break;
	case COUNTER_FUNCTION_QUADRATURE_X4:
		sms = TIM_SMCR_SMS_ENCODER_MODE_3;
		break;
	default:
		return -EINVAL;
	}

	/* Store enable status */
	regmap_read(priv->regmap, TIM_CR1, &cr1);

	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);

	regmap_update_bits(priv->regmap, TIM_SMCR, TIM_SMCR_SMS, sms);

	/* Make sure that registers are updated */
	regmap_update_bits(priv->regmap, TIM_EGR, TIM_EGR_UG, TIM_EGR_UG);

	/* Restore the enable status */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, cr1);

	return 0;
}

static int stm32_count_direction_read(struct counter_device *counter,
				      struct counter_count *count,
				      enum counter_count_direction *direction)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 cr1;

	regmap_read(priv->regmap, TIM_CR1, &cr1);
	*direction = (cr1 & TIM_CR1_DIR) ? COUNTER_COUNT_DIRECTION_BACKWARD :
		COUNTER_COUNT_DIRECTION_FORWARD;

	return 0;
}

static int stm32_count_ceiling_read(struct counter_device *counter,
				    struct counter_count *count, u64 *ceiling)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 arr;

	regmap_read(priv->regmap, TIM_ARR, &arr);

	*ceiling = arr;

	return 0;
}

static int stm32_count_ceiling_write(struct counter_device *counter,
				     struct counter_count *count, u64 ceiling)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);

	if (ceiling > priv->max_arr)
		return -ERANGE;

	/* TIMx_ARR register shouldn't be buffered (ARPE=0) */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, 0);
	regmap_write(priv->regmap, TIM_ARR, ceiling);

	return 0;
}

static int stm32_count_enable_read(struct counter_device *counter,
				   struct counter_count *count, u8 *enable)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 cr1;

	regmap_read(priv->regmap, TIM_CR1, &cr1);

	*enable = cr1 & TIM_CR1_CEN;

	return 0;
}

static int stm32_count_enable_write(struct counter_device *counter,
				    struct counter_count *count, u8 enable)
{
	struct stm32_timer_cnt *const priv = counter_priv(counter);
	u32 cr1;

	if (enable) {
		regmap_read(priv->regmap, TIM_CR1, &cr1);
		if (!(cr1 & TIM_CR1_CEN))
			clk_enable(priv->clk);

		regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN,
				   TIM_CR1_CEN);
	} else {
		regmap_read(priv->regmap, TIM_CR1, &cr1);
		regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);
		if (cr1 & TIM_CR1_CEN)
			clk_disable(priv->clk);
	}

	/* Keep enabled state to properly handle low power states */
	priv->enabled = enable;

	return 0;
}

static struct counter_comp stm32_count_ext[] = {
	COUNTER_COMP_DIRECTION(stm32_count_direction_read),
	COUNTER_COMP_ENABLE(stm32_count_enable_read, stm32_count_enable_write),
	COUNTER_COMP_CEILING(stm32_count_ceiling_read,
			     stm32_count_ceiling_write),
};

static const enum counter_synapse_action stm32_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES
};

static int stm32_action_read(struct counter_device *counter,
			     struct counter_count *count,
			     struct counter_synapse *synapse,
			     enum counter_synapse_action *action)
{
	enum counter_function function;
	int err;

	err = stm32_count_function_read(counter, count, &function);
	if (err)
		return err;

	switch (function) {
	case COUNTER_FUNCTION_INCREASE:
		/* counts on internal clock when CEN=1 */
		*action = COUNTER_SYNAPSE_ACTION_NONE;
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X2_A:
		/* counts up/down on TI1FP1 edge depending on TI2FP2 level */
		if (synapse->signal->id == count->synapses[0].signal->id)
			*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		else
			*action = COUNTER_SYNAPSE_ACTION_NONE;
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X2_B:
		/* counts up/down on TI2FP2 edge depending on TI1FP1 level */
		if (synapse->signal->id == count->synapses[1].signal->id)
			*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		else
			*action = COUNTER_SYNAPSE_ACTION_NONE;
		return 0;
	case COUNTER_FUNCTION_QUADRATURE_X4:
		/* counts up/down on both TI1FP1 and TI2FP2 edges */
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct counter_ops stm32_timer_cnt_ops = {
	.count_read = stm32_count_read,
	.count_write = stm32_count_write,
	.function_read = stm32_count_function_read,
	.function_write = stm32_count_function_write,
	.action_read = stm32_action_read,
};

static struct counter_signal stm32_signals[] = {
	{
		.id = 0,
		.name = "Channel 1 Quadrature A"
	},
	{
		.id = 1,
		.name = "Channel 1 Quadrature B"
	}
};

static struct counter_synapse stm32_count_synapses[] = {
	{
		.actions_list = stm32_synapse_actions,
		.num_actions = ARRAY_SIZE(stm32_synapse_actions),
		.signal = &stm32_signals[0]
	},
	{
		.actions_list = stm32_synapse_actions,
		.num_actions = ARRAY_SIZE(stm32_synapse_actions),
		.signal = &stm32_signals[1]
	}
};

static struct counter_count stm32_counts = {
	.id = 0,
	.name = "Channel 1 Count",
	.functions_list = stm32_count_functions,
	.num_functions = ARRAY_SIZE(stm32_count_functions),
	.synapses = stm32_count_synapses,
	.num_synapses = ARRAY_SIZE(stm32_count_synapses),
	.ext = stm32_count_ext,
	.num_ext = ARRAY_SIZE(stm32_count_ext)
};

static int stm32_timer_cnt_probe(struct platform_device *pdev)
{
	struct stm32_timers *ddata = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct stm32_timer_cnt *priv;
	struct counter_device *counter;
	int ret;

	if (IS_ERR_OR_NULL(ddata))
		return -EINVAL;

	counter = devm_counter_alloc(dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;

	priv = counter_priv(counter);

	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->max_arr = ddata->max_arr;

	counter->name = dev_name(dev);
	counter->parent = dev;
	counter->ops = &stm32_timer_cnt_ops;
	counter->counts = &stm32_counts;
	counter->num_counts = 1;
	counter->signals = stm32_signals;
	counter->num_signals = ARRAY_SIZE(stm32_signals);

	platform_set_drvdata(pdev, priv);

	/* Register Counter device */
	ret = devm_counter_add(dev, counter);
	if (ret < 0)
		dev_err_probe(dev, ret, "Failed to add counter\n");

	return ret;
}

static int __maybe_unused stm32_timer_cnt_suspend(struct device *dev)
{
	struct stm32_timer_cnt *priv = dev_get_drvdata(dev);

	/* Only take care of enabled counter: don't disturb other MFD child */
	if (priv->enabled) {
		/* Backup registers that may get lost in low power mode */
		regmap_read(priv->regmap, TIM_SMCR, &priv->bak.smcr);
		regmap_read(priv->regmap, TIM_ARR, &priv->bak.arr);
		regmap_read(priv->regmap, TIM_CNT, &priv->bak.cnt);
		regmap_read(priv->regmap, TIM_CR1, &priv->bak.cr1);

		/* Disable the counter */
		regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);
		clk_disable(priv->clk);
	}

	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused stm32_timer_cnt_resume(struct device *dev)
{
	struct stm32_timer_cnt *priv = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret)
		return ret;

	if (priv->enabled) {
		clk_enable(priv->clk);

		/* Restore registers that may have been lost */
		regmap_write(priv->regmap, TIM_SMCR, priv->bak.smcr);
		regmap_write(priv->regmap, TIM_ARR, priv->bak.arr);
		regmap_write(priv->regmap, TIM_CNT, priv->bak.cnt);

		/* Also re-enables the counter */
		regmap_write(priv->regmap, TIM_CR1, priv->bak.cr1);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(stm32_timer_cnt_pm_ops, stm32_timer_cnt_suspend,
			 stm32_timer_cnt_resume);

static const struct of_device_id stm32_timer_cnt_of_match[] = {
	{ .compatible = "st,stm32-timer-counter", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_timer_cnt_of_match);

static struct platform_driver stm32_timer_cnt_driver = {
	.probe = stm32_timer_cnt_probe,
	.driver = {
		.name = "stm32-timer-counter",
		.of_match_table = stm32_timer_cnt_of_match,
		.pm = &stm32_timer_cnt_pm_ops,
	},
};
module_platform_driver(stm32_timer_cnt_driver);

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_ALIAS("platform:stm32-timer-counter");
MODULE_DESCRIPTION("STMicroelectronics STM32 TIMER counter driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(COUNTER);
