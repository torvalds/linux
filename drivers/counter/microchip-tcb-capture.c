// SPDX-License-Identifier: GPL-2.0-only
/**
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
#include <linux/of_device.h>
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
	struct counter_device counter;
	struct regmap *regmap;
	int qdec_mode;
	int num_channels;
	int channel[2];
	bool trig_inverted;
};

enum mchp_tc_count_function {
	MCHP_TC_FUNCTION_INCREASE,
	MCHP_TC_FUNCTION_QUADRATURE,
};

static const enum counter_count_function mchp_tc_count_functions[] = {
	[MCHP_TC_FUNCTION_INCREASE] = COUNTER_COUNT_FUNCTION_INCREASE,
	[MCHP_TC_FUNCTION_QUADRATURE] = COUNTER_COUNT_FUNCTION_QUADRATURE_X4,
};

enum mchp_tc_synapse_action {
	MCHP_TC_SYNAPSE_ACTION_NONE = 0,
	MCHP_TC_SYNAPSE_ACTION_RISING_EDGE,
	MCHP_TC_SYNAPSE_ACTION_FALLING_EDGE,
	MCHP_TC_SYNAPSE_ACTION_BOTH_EDGE
};

static const enum counter_synapse_action mchp_tc_synapse_actions[] = {
	[MCHP_TC_SYNAPSE_ACTION_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[MCHP_TC_SYNAPSE_ACTION_RISING_EDGE] = COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	[MCHP_TC_SYNAPSE_ACTION_FALLING_EDGE] = COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	[MCHP_TC_SYNAPSE_ACTION_BOTH_EDGE] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
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

static int mchp_tc_count_function_get(struct counter_device *counter,
				      struct counter_count *count,
				      size_t *function)
{
	struct mchp_tc_data *const priv = counter->priv;

	if (priv->qdec_mode)
		*function = MCHP_TC_FUNCTION_QUADRATURE;
	else
		*function = MCHP_TC_FUNCTION_INCREASE;

	return 0;
}

static int mchp_tc_count_function_set(struct counter_device *counter,
				      struct counter_count *count,
				      size_t function)
{
	struct mchp_tc_data *const priv = counter->priv;
	u32 bmr, cmr;

	regmap_read(priv->regmap, ATMEL_TC_BMR, &bmr);
	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], CMR), &cmr);

	/* Set capture mode */
	cmr &= ~ATMEL_TC_WAVE;

	switch (function) {
	case MCHP_TC_FUNCTION_INCREASE:
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
	case MCHP_TC_FUNCTION_QUADRATURE:
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
				     enum counter_signal_value *val)
{
	struct mchp_tc_data *const priv = counter->priv;
	bool sigstatus;
	u32 sr;

	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], SR), &sr);

	if (priv->trig_inverted)
		sigstatus = (sr & ATMEL_TC_MTIOB);
	else
		sigstatus = (sr & ATMEL_TC_MTIOA);

	*val = sigstatus ? COUNTER_SIGNAL_HIGH : COUNTER_SIGNAL_LOW;

	return 0;
}

static int mchp_tc_count_action_get(struct counter_device *counter,
				    struct counter_count *count,
				    struct counter_synapse *synapse,
				    size_t *action)
{
	struct mchp_tc_data *const priv = counter->priv;
	u32 cmr;

	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], CMR), &cmr);

	switch (cmr & ATMEL_TC_ETRGEDG) {
	default:
		*action = MCHP_TC_SYNAPSE_ACTION_NONE;
		break;
	case ATMEL_TC_ETRGEDG_RISING:
		*action = MCHP_TC_SYNAPSE_ACTION_RISING_EDGE;
		break;
	case ATMEL_TC_ETRGEDG_FALLING:
		*action = MCHP_TC_SYNAPSE_ACTION_FALLING_EDGE;
		break;
	case ATMEL_TC_ETRGEDG_BOTH:
		*action = MCHP_TC_SYNAPSE_ACTION_BOTH_EDGE;
		break;
	}

	return 0;
}

static int mchp_tc_count_action_set(struct counter_device *counter,
				    struct counter_count *count,
				    struct counter_synapse *synapse,
				    size_t action)
{
	struct mchp_tc_data *const priv = counter->priv;
	u32 edge = ATMEL_TC_ETRGEDG_NONE;

	/* QDEC mode is rising edge only */
	if (priv->qdec_mode)
		return -EINVAL;

	switch (action) {
	case MCHP_TC_SYNAPSE_ACTION_NONE:
		edge = ATMEL_TC_ETRGEDG_NONE;
		break;
	case MCHP_TC_SYNAPSE_ACTION_RISING_EDGE:
		edge = ATMEL_TC_ETRGEDG_RISING;
		break;
	case MCHP_TC_SYNAPSE_ACTION_FALLING_EDGE:
		edge = ATMEL_TC_ETRGEDG_FALLING;
		break;
	case MCHP_TC_SYNAPSE_ACTION_BOTH_EDGE:
		edge = ATMEL_TC_ETRGEDG_BOTH;
		break;
	}

	return regmap_write_bits(priv->regmap,
				ATMEL_TC_REG(priv->channel[0], CMR),
				ATMEL_TC_ETRGEDG, edge);
}

static int mchp_tc_count_read(struct counter_device *counter,
			      struct counter_count *count,
			      unsigned long *val)
{
	struct mchp_tc_data *const priv = counter->priv;
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
	.signal_read  = mchp_tc_count_signal_read,
	.count_read   = mchp_tc_count_read,
	.function_get = mchp_tc_count_function_get,
	.function_set = mchp_tc_count_function_set,
	.action_get   = mchp_tc_count_action_get,
	.action_set   = mchp_tc_count_action_set
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
	struct mchp_tc_data *priv;
	char clk_name[7];
	struct regmap *regmap;
	struct clk *clk[3];
	int channel;
	int ret, i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

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
	priv->counter.name = dev_name(&pdev->dev);
	priv->counter.parent = &pdev->dev;
	priv->counter.ops = &mchp_tc_ops;
	priv->counter.num_counts = ARRAY_SIZE(mchp_tc_counts);
	priv->counter.counts = mchp_tc_counts;
	priv->counter.num_signals = ARRAY_SIZE(mchp_tc_count_signals);
	priv->counter.signals = mchp_tc_count_signals;
	priv->counter.priv = priv;

	return devm_counter_register(&pdev->dev, &priv->counter);
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
