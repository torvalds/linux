// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Microchip
 *
 * Author: Kamel Bouhara <kamel.bouhara@bootlin.com>
 */
#include <linux/clk.h>
#include <linux/counter.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <soc/at91/atmel_tcb.h>

#define ATMEL_TC_CMR_MASK	(ATMEL_TC_LDRA_RISING | ATMEL_TC_LDRB_FALLING | \
				 ATMEL_TC_ETRGEDG_RISING | ATMEL_TC_LDBDIS | \
				 ATMEL_TC_LDBSTOP)

#define ATMEL_TC_QDEN			BIT(8)
#define ATMEL_TC_POSEN			BIT(9)

struct mchp_tc_data {
	const struct atmel_tcb_config *tc_cfg;
	struct regmap *regmap;
	int qdec_mode;
	int num_channels;
	int channel[2];
};

static const enum counter_function mchp_tc_count_functions[] = {
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_QUADRATURE_X4,
};

static const enum counter_synapse_action mchp_tc_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

static struct counter_signal mchp_tc_count_signals[] = {
	{
		.id = 0,
		.name = "Channel A",
	},
	{
		.id = 1,
		.name = "Channel B",
	}
};

static struct counter_synapse mchp_tc_count_synapses[] = {
	{
		.actions_list = mchp_tc_synapse_actions,
		.num_actions = ARRAY_SIZE(mchp_tc_synapse_actions),
		.signal = &mchp_tc_count_signals[0]
	},
	{
		.actions_list = mchp_tc_synapse_actions,
		.num_actions = ARRAY_SIZE(mchp_tc_synapse_actions),
		.signal = &mchp_tc_count_signals[1]
	}
};

static int mchp_tc_count_function_read(struct counter_device *counter,
				       struct counter_count *count,
				       enum counter_function *function)
{
	struct mchp_tc_data *const priv = counter_priv(counter);

	if (priv->qdec_mode)
		*function = COUNTER_FUNCTION_QUADRATURE_X4;
	else
		*function = COUNTER_FUNCTION_INCREASE;

	return 0;
}

static int mchp_tc_count_function_write(struct counter_device *counter,
					struct counter_count *count,
					enum counter_function function)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 bmr, cmr;

	regmap_read(priv->regmap, ATMEL_TC_BMR, &bmr);
	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], CMR), &cmr);

	/* Set capture mode */
	cmr &= ~ATMEL_TC_WAVE;

	switch (function) {
	case COUNTER_FUNCTION_INCREASE:
		priv->qdec_mode = 0;
		/* Set highest rate based on whether soc has gclk or not */
		bmr &= ~(ATMEL_TC_QDEN | ATMEL_TC_POSEN);
		if (priv->tc_cfg->has_gclk)
			cmr |= ATMEL_TC_TIMER_CLOCK2;
		else
			cmr |= ATMEL_TC_TIMER_CLOCK1;
		/* Setup the period capture mode */
		cmr |=  ATMEL_TC_CMR_MASK;
		cmr &= ~(ATMEL_TC_ABETRG | ATMEL_TC_XC0);
		break;
	case COUNTER_FUNCTION_QUADRATURE_X4:
		if (!priv->tc_cfg->has_qdec)
			return -EINVAL;
		/* In QDEC mode settings both channels 0 and 1 are required */
		if (priv->num_channels < 2 || priv->channel[0] != 0 ||
		    priv->channel[1] != 1) {
			pr_err("Invalid channels number or id for quadrature mode\n");
			return -EINVAL;
		}
		priv->qdec_mode = 1;
		bmr |= ATMEL_TC_QDEN | ATMEL_TC_POSEN;
		cmr |= ATMEL_TC_ETRGEDG_RISING | ATMEL_TC_ABETRG | ATMEL_TC_XC0;
		break;
	default:
		/* should never reach this path */
		return -EINVAL;
	}

	regmap_write(priv->regmap, ATMEL_TC_BMR, bmr);
	regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], CMR), cmr);

	/* Enable clock and trigger counter */
	regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], CCR),
		     ATMEL_TC_CLKEN | ATMEL_TC_SWTRG);

	if (priv->qdec_mode) {
		regmap_write(priv->regmap,
			     ATMEL_TC_REG(priv->channel[1], CMR), cmr);
		regmap_write(priv->regmap,
			     ATMEL_TC_REG(priv->channel[1], CCR),
			     ATMEL_TC_CLKEN | ATMEL_TC_SWTRG);
	}

	return 0;
}

static int mchp_tc_count_signal_read(struct counter_device *counter,
				     struct counter_signal *signal,
				     enum counter_signal_level *lvl)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	bool sigstatus;
	u32 sr;

	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], SR), &sr);

	if (signal->id == 1)
		sigstatus = (sr & ATMEL_TC_MTIOB);
	else
		sigstatus = (sr & ATMEL_TC_MTIOA);

	*lvl = sigstatus ? COUNTER_SIGNAL_LEVEL_HIGH : COUNTER_SIGNAL_LEVEL_LOW;

	return 0;
}

static int mchp_tc_count_action_read(struct counter_device *counter,
				     struct counter_count *count,
				     struct counter_synapse *synapse,
				     enum counter_synapse_action *action)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 cmr;

	if (priv->qdec_mode) {
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		return 0;
	}

	/* Only TIOA signal is evaluated in non-QDEC mode */
	if (synapse->signal->id != 0) {
		*action = COUNTER_SYNAPSE_ACTION_NONE;
		return 0;
	}

	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], CMR), &cmr);

	switch (cmr & ATMEL_TC_ETRGEDG) {
	default:
		*action = COUNTER_SYNAPSE_ACTION_NONE;
		break;
	case ATMEL_TC_ETRGEDG_RISING:
		*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
		break;
	case ATMEL_TC_ETRGEDG_FALLING:
		*action = COUNTER_SYNAPSE_ACTION_FALLING_EDGE;
		break;
	case ATMEL_TC_ETRGEDG_BOTH:
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	}

	return 0;
}

static int mchp_tc_count_action_write(struct counter_device *counter,
				      struct counter_count *count,
				      struct counter_synapse *synapse,
				      enum counter_synapse_action action)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 edge = ATMEL_TC_ETRGEDG_NONE;

	/* QDEC mode is rising edge only; only TIOA handled in non-QDEC mode */
	if (priv->qdec_mode || synapse->signal->id != 0)
		return -EINVAL;

	switch (action) {
	case COUNTER_SYNAPSE_ACTION_NONE:
		edge = ATMEL_TC_ETRGEDG_NONE;
		break;
	case COUNTER_SYNAPSE_ACTION_RISING_EDGE:
		edge = ATMEL_TC_ETRGEDG_RISING;
		break;
	case COUNTER_SYNAPSE_ACTION_FALLING_EDGE:
		edge = ATMEL_TC_ETRGEDG_FALLING;
		break;
	case COUNTER_SYNAPSE_ACTION_BOTH_EDGES:
		edge = ATMEL_TC_ETRGEDG_BOTH;
		break;
	default:
		/* should never reach this path */
		return -EINVAL;
	}

	return regmap_write_bits(priv->regmap,
				ATMEL_TC_REG(priv->channel[0], CMR),
				ATMEL_TC_ETRGEDG, edge);
}

static int mchp_tc_count_read(struct counter_device *counter,
			      struct counter_count *count, u64 *val)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 cnt;

	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], CV), &cnt);
	*val = cnt;

	return 0;
}

static struct counter_count mchp_tc_counts[] = {
	{
		.id = 0,
		.name = "Timer Counter",
		.functions_list = mchp_tc_count_functions,
		.num_functions = ARRAY_SIZE(mchp_tc_count_functions),
		.synapses = mchp_tc_count_synapses,
		.num_synapses = ARRAY_SIZE(mchp_tc_count_synapses),
	},
};

static const struct counter_ops mchp_tc_ops = {
	.signal_read    = mchp_tc_count_signal_read,
	.count_read     = mchp_tc_count_read,
	.function_read  = mchp_tc_count_function_read,
	.function_write = mchp_tc_count_function_write,
	.action_read    = mchp_tc_count_action_read,
	.action_write   = mchp_tc_count_action_write
};

static const struct atmel_tcb_config tcb_rm9200_config = {
		.counter_width = 16,
};

static const struct atmel_tcb_config tcb_sam9x5_config = {
		.counter_width = 32,
};

static const struct atmel_tcb_config tcb_sama5d2_config = {
		.counter_width = 32,
		.has_gclk = true,
		.has_qdec = true,
};

static const struct atmel_tcb_config tcb_sama5d3_config = {
		.counter_width = 32,
		.has_qdec = true,
};

static const struct of_device_id atmel_tc_of_match[] = {
	{ .compatible = "atmel,at91rm9200-tcb", .data = &tcb_rm9200_config, },
	{ .compatible = "atmel,at91sam9x5-tcb", .data = &tcb_sam9x5_config, },
	{ .compatible = "atmel,sama5d2-tcb", .data = &tcb_sama5d2_config, },
	{ .compatible = "atmel,sama5d3-tcb", .data = &tcb_sama5d3_config, },
	{ /* sentinel */ }
};

static void mchp_tc_clk_remove(void *ptr)
{
	clk_disable_unprepare((struct clk *)ptr);
}

static int mchp_tc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct atmel_tcb_config *tcb_config;
	const struct of_device_id *match;
	struct counter_device *counter;
	struct mchp_tc_data *priv;
	char clk_name[7];
	struct regmap *regmap;
	struct clk *clk[3];
	int channel;
	int ret, i;

	counter = devm_counter_alloc(&pdev->dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);

	match = of_match_node(atmel_tc_of_match, np->parent);
	tcb_config = match->data;
	if (!tcb_config) {
		dev_err(&pdev->dev, "No matching parent node found\n");
		return -ENODEV;
	}

	regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* max. channels number is 2 when in QDEC mode */
	priv->num_channels = of_property_count_u32_elems(np, "reg");
	if (priv->num_channels < 0) {
		dev_err(&pdev->dev, "Invalid or missing channel\n");
		return -EINVAL;
	}

	/* Register channels and initialize clocks */
	for (i = 0; i < priv->num_channels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &channel);
		if (ret < 0 || channel > 2)
			return -ENODEV;

		priv->channel[i] = channel;

		snprintf(clk_name, sizeof(clk_name), "t%d_clk", channel);

		clk[i] = of_clk_get_by_name(np->parent, clk_name);
		if (IS_ERR(clk[i])) {
			/* Fallback to t0_clk */
			clk[i] = of_clk_get_by_name(np->parent, "t0_clk");
			if (IS_ERR(clk[i]))
				return PTR_ERR(clk[i]);
		}

		ret = clk_prepare_enable(clk[i]);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(&pdev->dev,
					       mchp_tc_clk_remove,
					       clk[i]);
		if (ret)
			return ret;

		dev_dbg(&pdev->dev,
			"Initialized capture mode on channel %d\n",
			channel);
	}

	priv->tc_cfg = tcb_config;
	priv->regmap = regmap;
	counter->name = dev_name(&pdev->dev);
	counter->parent = &pdev->dev;
	counter->ops = &mchp_tc_ops;
	counter->num_counts = ARRAY_SIZE(mchp_tc_counts);
	counter->counts = mchp_tc_counts;
	counter->num_signals = ARRAY_SIZE(mchp_tc_count_signals);
	counter->signals = mchp_tc_count_signals;

	ret = devm_counter_add(&pdev->dev, counter);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to add counter\n");

	return 0;
}

static const struct of_device_id mchp_tc_dt_ids[] = {
	{ .compatible = "microchip,tcb-capture", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mchp_tc_dt_ids);

static struct platform_driver mchp_tc_driver = {
	.probe = mchp_tc_probe,
	.driver = {
		.name = "microchip-tcb-capture",
		.of_match_table = mchp_tc_dt_ids,
	},
};
module_platform_driver(mchp_tc_driver);

MODULE_AUTHOR("Kamel Bouhara <kamel.bouhara@bootlin.com>");
MODULE_DESCRIPTION("Microchip TCB Capture driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(COUNTER);
