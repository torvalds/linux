// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Microchip
 *
 * Author: Kamel Bouhara <kamel.bouhara@bootlin.com>
 */
#include <linux/clk.h>
#include <linux/counter.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <uapi/linux/counter/microchip-tcb-capture.h>
#include <soc/at91/atmel_tcb.h>

#define ATMEL_TC_CMR_MASK	(ATMEL_TC_LDRA_RISING | ATMEL_TC_LDRB_FALLING | \
				 ATMEL_TC_ETRGEDG_RISING | ATMEL_TC_LDBDIS | \
				 ATMEL_TC_LDBSTOP)

#define ATMEL_TC_DEF_IRQS	(ATMEL_TC_ETRGS | ATMEL_TC_COVFS | \
				 ATMEL_TC_LDRAS | ATMEL_TC_LDRBS | ATMEL_TC_CPCS)

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
		if (!priv->tc_cfg->has_gclk)
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

static int mchp_tc_count_cap_read(struct counter_device *counter,
				  struct counter_count *count, size_t idx, u64 *val)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 cnt;
	int ret;

	switch (idx) {
	case COUNTER_MCHP_EXCAP_RA:
		ret = regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], RA), &cnt);
		break;
	case COUNTER_MCHP_EXCAP_RB:
		ret = regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], RB), &cnt);
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	*val = cnt;

	return 0;
}

static int mchp_tc_count_cap_write(struct counter_device *counter,
				   struct counter_count *count, size_t idx, u64 val)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	int ret;

	if (val > U32_MAX)
		return -ERANGE;

	switch (idx) {
	case COUNTER_MCHP_EXCAP_RA:
		ret = regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], RA), val);
		break;
	case COUNTER_MCHP_EXCAP_RB:
		ret = regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], RB), val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int mchp_tc_count_compare_read(struct counter_device *counter, struct counter_count *count,
				      u64 *val)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 cnt;
	int ret;

	ret = regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], RC), &cnt);
	if (ret < 0)
		return ret;

	*val = cnt;

	return 0;
}

static int mchp_tc_count_compare_write(struct counter_device *counter, struct counter_count *count,
				       u64 val)
{
	struct mchp_tc_data *const priv = counter_priv(counter);

	if (val > U32_MAX)
		return -ERANGE;

	return regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], RC), val);
}

static DEFINE_COUNTER_ARRAY_CAPTURE(mchp_tc_cnt_cap_array, 2);

static struct counter_comp mchp_tc_count_ext[] = {
	COUNTER_COMP_ARRAY_CAPTURE(mchp_tc_count_cap_read, mchp_tc_count_cap_write,
				   mchp_tc_cnt_cap_array),
	COUNTER_COMP_COMPARE(mchp_tc_count_compare_read, mchp_tc_count_compare_write),
};

static int mchp_tc_watch_validate(struct counter_device *counter,
				  const struct counter_watch *watch)
{
	if (watch->channel == COUNTER_MCHP_EVCHN_CV || watch->channel == COUNTER_MCHP_EVCHN_RA)
		switch (watch->event) {
		case COUNTER_EVENT_CHANGE_OF_STATE:
		case COUNTER_EVENT_OVERFLOW:
		case COUNTER_EVENT_CAPTURE:
			return 0;
		default:
			return -EINVAL;
		}

	if (watch->channel == COUNTER_MCHP_EVCHN_RB && watch->event == COUNTER_EVENT_CAPTURE)
		return 0;

	if (watch->channel == COUNTER_MCHP_EVCHN_RC && watch->event == COUNTER_EVENT_THRESHOLD)
		return 0;

	return -EINVAL;
}

static struct counter_count mchp_tc_counts[] = {
	{
		.id = 0,
		.name = "Timer Counter",
		.functions_list = mchp_tc_count_functions,
		.num_functions = ARRAY_SIZE(mchp_tc_count_functions),
		.synapses = mchp_tc_count_synapses,
		.num_synapses = ARRAY_SIZE(mchp_tc_count_synapses),
		.ext = mchp_tc_count_ext,
		.num_ext = ARRAY_SIZE(mchp_tc_count_ext),
	},
};

static const struct counter_ops mchp_tc_ops = {
	.signal_read    = mchp_tc_count_signal_read,
	.count_read     = mchp_tc_count_read,
	.function_read  = mchp_tc_count_function_read,
	.function_write = mchp_tc_count_function_write,
	.action_read    = mchp_tc_count_action_read,
	.action_write   = mchp_tc_count_action_write,
	.watch_validate = mchp_tc_watch_validate,
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

static irqreturn_t mchp_tc_isr(int irq, void *dev_id)
{
	struct counter_device *const counter = dev_id;
	struct mchp_tc_data *const priv = counter_priv(counter);
	u32 sr, mask;

	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], SR), &sr);
	regmap_read(priv->regmap, ATMEL_TC_REG(priv->channel[0], IMR), &mask);

	sr &= mask;
	if (!(sr & ATMEL_TC_ALL_IRQ))
		return IRQ_NONE;

	if (sr & ATMEL_TC_ETRGS)
		counter_push_event(counter, COUNTER_EVENT_CHANGE_OF_STATE,
				   COUNTER_MCHP_EVCHN_CV);
	if (sr & ATMEL_TC_LDRAS)
		counter_push_event(counter, COUNTER_EVENT_CAPTURE,
				   COUNTER_MCHP_EVCHN_RA);
	if (sr & ATMEL_TC_LDRBS)
		counter_push_event(counter, COUNTER_EVENT_CAPTURE,
				   COUNTER_MCHP_EVCHN_RB);
	if (sr & ATMEL_TC_CPCS)
		counter_push_event(counter, COUNTER_EVENT_THRESHOLD,
				   COUNTER_MCHP_EVCHN_RC);
	if (sr & ATMEL_TC_COVFS)
		counter_push_event(counter, COUNTER_EVENT_OVERFLOW,
				   COUNTER_MCHP_EVCHN_CV);

	return IRQ_HANDLED;
}

static void mchp_tc_irq_remove(void *ptr)
{
	struct mchp_tc_data *priv = ptr;

	regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], IDR), ATMEL_TC_DEF_IRQS);
}

static int mchp_tc_irq_enable(struct counter_device *const counter, int irq)
{
	struct mchp_tc_data *const priv = counter_priv(counter);
	int ret = devm_request_irq(counter->parent, irq, mchp_tc_isr, IRQF_SHARED,
				   dev_name(counter->parent), counter);

	if (ret < 0)
		return ret;

	ret = regmap_write(priv->regmap, ATMEL_TC_REG(priv->channel[0], IER), ATMEL_TC_DEF_IRQS);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(counter->parent, mchp_tc_irq_remove, priv);
	if (ret < 0)
		return ret;

	return 0;
}

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

	/* Disable Quadrature Decoder and position measure */
	ret = regmap_update_bits(regmap, ATMEL_TC_BMR, ATMEL_TC_QDEN | ATMEL_TC_POSEN, 0);
	if (ret)
		return ret;

	/* Setup the period capture mode */
	ret = regmap_update_bits(regmap, ATMEL_TC_REG(priv->channel[0], CMR),
				 ATMEL_TC_WAVE | ATMEL_TC_ABETRG | ATMEL_TC_CMR_MASK |
				 ATMEL_TC_TCCLKS,
				 ATMEL_TC_CMR_MASK);
	if (ret)
		return ret;

	/* Enable clock and trigger counter */
	ret = regmap_write(regmap, ATMEL_TC_REG(priv->channel[0], CCR),
			   ATMEL_TC_CLKEN | ATMEL_TC_SWTRG);
	if (ret)
		return ret;

	priv->tc_cfg = tcb_config;
	priv->regmap = regmap;
	counter->name = dev_name(&pdev->dev);
	counter->parent = &pdev->dev;
	counter->ops = &mchp_tc_ops;
	counter->num_counts = ARRAY_SIZE(mchp_tc_counts);
	counter->counts = mchp_tc_counts;
	counter->num_signals = ARRAY_SIZE(mchp_tc_count_signals);
	counter->signals = mchp_tc_count_signals;

	i = of_irq_get(np->parent, 0);
	if (i == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (i > 0) {
		ret = mchp_tc_irq_enable(counter, i);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret, "Failed to set up IRQ");
	}

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
MODULE_IMPORT_NS("COUNTER");
