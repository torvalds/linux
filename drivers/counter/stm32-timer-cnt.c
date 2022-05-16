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
	struct counter_device counter;
	struct regmap *regmap;
	struct clk *clk;
	u32 ceiling;
	bool enabled;
	struct stm32_timer_regs bak;
};

/**
 * enum stm32_count_function - enumerates stm32 timer counter encoder modes
 * @STM32_COUNT_SLAVE_MODE_DISABLED: counts on internal clock when CEN=1
 * @STM32_COUNT_ENCODER_MODE_1: counts TI1FP1 edges, depending on TI2FP2 level
 * @STM32_COUNT_ENCODER_MODE_2: counts TI2FP2 edges, depending on TI1FP1 level
 * @STM32_COUNT_ENCODER_MODE_3: counts on both TI1FP1 and TI2FP2 edges
 */
enum stm32_count_function {
	STM32_COUNT_SLAVE_MODE_DISABLED = -1,
	STM32_COUNT_ENCODER_MODE_1,
	STM32_COUNT_ENCODER_MODE_2,
	STM32_COUNT_ENCODER_MODE_3,
};

static enum counter_count_function stm32_count_functions[] = {
	[STM32_COUNT_ENCODER_MODE_1] = COUNTER_COUNT_FUNCTION_QUADRATURE_X2_A,
	[STM32_COUNT_ENCODER_MODE_2] = COUNTER_COUNT_FUNCTION_QUADRATURE_X2_B,
	[STM32_COUNT_ENCODER_MODE_3] = COUNTER_COUNT_FUNCTION_QUADRATURE_X4,
};

static int stm32_count_read(struct counter_device *counter,
			    struct counter_count *count, unsigned long *val)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	u32 cnt;

	regmap_read(priv->regmap, TIM_CNT, &cnt);
	*val = cnt;

	return 0;
}

static int stm32_count_write(struct counter_device *counter,
			     struct counter_count *count,
			     const unsigned long val)
{
	struct stm32_timer_cnt *const priv = counter->priv;

	if (val > priv->ceiling)
		return -EINVAL;

	return regmap_write(priv->regmap, TIM_CNT, val);
}

static int stm32_count_function_get(struct counter_device *counter,
				    struct counter_count *count,
				    size_t *function)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	u32 smcr;

	regmap_read(priv->regmap, TIM_SMCR, &smcr);

	switch (smcr & TIM_SMCR_SMS) {
	case 1:
		*function = STM32_COUNT_ENCODER_MODE_1;
		return 0;
	case 2:
		*function = STM32_COUNT_ENCODER_MODE_2;
		return 0;
	case 3:
		*function = STM32_COUNT_ENCODER_MODE_3;
		return 0;
	}

	return -EINVAL;
}

static int stm32_count_function_set(struct counter_device *counter,
				    struct counter_count *count,
				    size_t function)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	u32 cr1, sms;

	switch (function) {
	case STM32_COUNT_ENCODER_MODE_1:
		sms = 1;
		break;
	case STM32_COUNT_ENCODER_MODE_2:
		sms = 2;
		break;
	case STM32_COUNT_ENCODER_MODE_3:
		sms = 3;
		break;
	default:
		sms = 0;
		break;
	}

	/* Store enable status */
	regmap_read(priv->regmap, TIM_CR1, &cr1);

	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);

	/* TIMx_ARR register shouldn't be buffered (ARPE=0) */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, 0);
	regmap_write(priv->regmap, TIM_ARR, priv->ceiling);

	regmap_update_bits(priv->regmap, TIM_SMCR, TIM_SMCR_SMS, sms);

	/* Make sure that registers are updated */
	regmap_update_bits(priv->regmap, TIM_EGR, TIM_EGR_UG, TIM_EGR_UG);

	/* Restore the enable status */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, cr1);

	return 0;
}

static ssize_t stm32_count_direction_read(struct counter_device *counter,
				      struct counter_count *count,
				      void *private, char *buf)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	const char *direction;
	u32 cr1;

	regmap_read(priv->regmap, TIM_CR1, &cr1);
	direction = (cr1 & TIM_CR1_DIR) ? "backward" : "forward";

	return scnprintf(buf, PAGE_SIZE, "%s\n", direction);
}

static ssize_t stm32_count_ceiling_read(struct counter_device *counter,
					struct counter_count *count,
					void *private, char *buf)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	u32 arr;

	regmap_read(priv->regmap, TIM_ARR, &arr);

	return snprintf(buf, PAGE_SIZE, "%u\n", arr);
}

static ssize_t stm32_count_ceiling_write(struct counter_device *counter,
					 struct counter_count *count,
					 void *private,
					 const char *buf, size_t len)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	unsigned int ceiling;
	int ret;

	ret = kstrtouint(buf, 0, &ceiling);
	if (ret)
		return ret;

	/* TIMx_ARR register shouldn't be buffered (ARPE=0) */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, 0);
	regmap_write(priv->regmap, TIM_ARR, ceiling);

	priv->ceiling = ceiling;
	return len;
}

static ssize_t stm32_count_enable_read(struct counter_device *counter,
				       struct counter_count *count,
				       void *private, char *buf)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	u32 cr1;

	regmap_read(priv->regmap, TIM_CR1, &cr1);

	return scnprintf(buf, PAGE_SIZE, "%d\n", (bool)(cr1 & TIM_CR1_CEN));
}

static ssize_t stm32_count_enable_write(struct counter_device *counter,
					struct counter_count *count,
					void *private,
					const char *buf, size_t len)
{
	struct stm32_timer_cnt *const priv = counter->priv;
	int err;
	u32 cr1;
	bool enable;

	err = kstrtobool(buf, &enable);
	if (err)
		return err;

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

	return len;
}

static const struct counter_count_ext stm32_count_ext[] = {
	{
		.name = "direction",
		.read = stm32_count_direction_read,
	},
	{
		.name = "enable",
		.read = stm32_count_enable_read,
		.write = stm32_count_enable_write
	},
	{
		.name = "ceiling",
		.read = stm32_count_ceiling_read,
		.write = stm32_count_ceiling_write
	},
};

enum stm32_synapse_action {
	STM32_SYNAPSE_ACTION_NONE,
	STM32_SYNAPSE_ACTION_BOTH_EDGES
};

static enum counter_synapse_action stm32_synapse_actions[] = {
	[STM32_SYNAPSE_ACTION_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[STM32_SYNAPSE_ACTION_BOTH_EDGES] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES
};

static int stm32_action_get(struct counter_device *counter,
			    struct counter_count *count,
			    struct counter_synapse *synapse,
			    size_t *action)
{
	size_t function;
	int err;

	/* Default action mode (e.g. STM32_COUNT_SLAVE_MODE_DISABLED) */
	*action = STM32_SYNAPSE_ACTION_NONE;

	err = stm32_count_function_get(counter, count, &function);
	if (err)
		return 0;

	switch (function) {
	case STM32_COUNT_ENCODER_MODE_1:
		/* counts up/down on TI1FP1 edge depending on TI2FP2 level */
		if (synapse->signal->id == count->synapses[0].signal->id)
			*action = STM32_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	case STM32_COUNT_ENCODER_MODE_2:
		/* counts up/down on TI2FP2 edge depending on TI1FP1 level */
		if (synapse->signal->id == count->synapses[1].signal->id)
			*action = STM32_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	case STM32_COUNT_ENCODER_MODE_3:
		/* counts up/down on both TI1FP1 and TI2FP2 edges */
		*action = STM32_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	}

	return 0;
}

static const struct counter_ops stm32_timer_cnt_ops = {
	.count_read = stm32_count_read,
	.count_write = stm32_count_write,
	.function_get = stm32_count_function_get,
	.function_set = stm32_count_function_set,
	.action_get = stm32_action_get,
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

	if (IS_ERR_OR_NULL(ddata))
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->ceiling = ddata->max_arr;

	priv->counter.name = dev_name(dev);
	priv->counter.parent = dev;
	priv->counter.ops = &stm32_timer_cnt_ops;
	priv->counter.counts = &stm32_counts;
	priv->counter.num_counts = 1;
	priv->counter.signals = stm32_signals;
	priv->counter.num_signals = ARRAY_SIZE(stm32_signals);
	priv->counter.priv = priv;

	platform_set_drvdata(pdev, priv);

	/* Register Counter device */
	return devm_counter_register(dev, &priv->counter);
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
