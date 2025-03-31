// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */

#include <linux/cleanup.h>
#include <linux/counter.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define INTERRUPT_CNT_NAME "interrupt-cnt"

struct interrupt_cnt_priv {
	atomic_t count;
	struct gpio_desc *gpio;
	int irq;
	bool enabled;
	struct mutex lock;
	struct counter_signal signals;
	struct counter_synapse synapses;
	struct counter_count cnts;
};

static irqreturn_t interrupt_cnt_isr(int irq, void *dev_id)
{
	struct counter_device *counter = dev_id;
	struct interrupt_cnt_priv *priv = counter_priv(counter);

	atomic_inc(&priv->count);

	counter_push_event(counter, COUNTER_EVENT_CHANGE_OF_STATE, 0);

	return IRQ_HANDLED;
}

static int interrupt_cnt_enable_read(struct counter_device *counter,
				     struct counter_count *count, u8 *enable)
{
	struct interrupt_cnt_priv *priv = counter_priv(counter);

	guard(mutex)(&priv->lock);

	*enable = priv->enabled;

	return 0;
}

static int interrupt_cnt_enable_write(struct counter_device *counter,
				      struct counter_count *count, u8 enable)
{
	struct interrupt_cnt_priv *priv = counter_priv(counter);

	guard(mutex)(&priv->lock);

	if (priv->enabled == enable)
		return 0;

	if (enable) {
		priv->enabled = true;
		enable_irq(priv->irq);
	} else {
		disable_irq(priv->irq);
		priv->enabled = false;
	}

	return 0;
}

static struct counter_comp interrupt_cnt_ext[] = {
	COUNTER_COMP_ENABLE(interrupt_cnt_enable_read,
			    interrupt_cnt_enable_write),
};

static const enum counter_synapse_action interrupt_cnt_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
};

static int interrupt_cnt_action_read(struct counter_device *counter,
				     struct counter_count *count,
				     struct counter_synapse *synapse,
				     enum counter_synapse_action *action)
{
	*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;

	return 0;
}

static int interrupt_cnt_read(struct counter_device *counter,
			      struct counter_count *count, u64 *val)
{
	struct interrupt_cnt_priv *priv = counter_priv(counter);

	*val = atomic_read(&priv->count);

	return 0;
}

static int interrupt_cnt_write(struct counter_device *counter,
			       struct counter_count *count, const u64 val)
{
	struct interrupt_cnt_priv *priv = counter_priv(counter);

	if (val != (typeof(priv->count.counter))val)
		return -ERANGE;

	atomic_set(&priv->count, val);

	return 0;
}

static const enum counter_function interrupt_cnt_functions[] = {
	COUNTER_FUNCTION_INCREASE,
};

static int interrupt_cnt_function_read(struct counter_device *counter,
				       struct counter_count *count,
				       enum counter_function *function)
{
	*function = COUNTER_FUNCTION_INCREASE;

	return 0;
}

static int interrupt_cnt_signal_read(struct counter_device *counter,
				     struct counter_signal *signal,
				     enum counter_signal_level *level)
{
	struct interrupt_cnt_priv *priv = counter_priv(counter);
	int ret;

	if (!priv->gpio)
		return -EINVAL;

	ret = gpiod_get_value(priv->gpio);
	if (ret < 0)
		return ret;

	*level = ret ? COUNTER_SIGNAL_LEVEL_HIGH : COUNTER_SIGNAL_LEVEL_LOW;

	return 0;
}

static int interrupt_cnt_watch_validate(struct counter_device *counter,
					const struct counter_watch *watch)
{
	if (watch->channel != 0 ||
	    watch->event != COUNTER_EVENT_CHANGE_OF_STATE)
		return -EINVAL;

	return 0;
}

static const struct counter_ops interrupt_cnt_ops = {
	.action_read = interrupt_cnt_action_read,
	.count_read = interrupt_cnt_read,
	.count_write = interrupt_cnt_write,
	.function_read = interrupt_cnt_function_read,
	.signal_read  = interrupt_cnt_signal_read,
	.watch_validate  = interrupt_cnt_watch_validate,
};

static int interrupt_cnt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct counter_device *counter;
	struct interrupt_cnt_priv *priv;
	int ret;

	counter = devm_counter_alloc(dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);

	priv->irq = platform_get_irq_optional(pdev,  0);
	if (priv->irq == -ENXIO)
		priv->irq = 0;
	else if (priv->irq < 0)
		return dev_err_probe(dev, priv->irq, "failed to get IRQ\n");

	priv->gpio = devm_gpiod_get_optional(dev, NULL, GPIOD_IN);
	if (IS_ERR(priv->gpio))
		return dev_err_probe(dev, PTR_ERR(priv->gpio), "failed to get GPIO\n");

	if (!priv->irq && !priv->gpio) {
		dev_err(dev, "IRQ and GPIO are not found. At least one source should be provided\n");
		return -ENODEV;
	}

	if (!priv->irq) {
		int irq = gpiod_to_irq(priv->gpio);

		if (irq < 0)
			return dev_err_probe(dev, irq, "failed to get IRQ from GPIO\n");

		priv->irq = irq;
	}

	priv->signals.name = devm_kasprintf(dev, GFP_KERNEL, "IRQ %d",
					    priv->irq);
	if (!priv->signals.name)
		return -ENOMEM;

	counter->signals = &priv->signals;
	counter->num_signals = 1;

	priv->synapses.actions_list = interrupt_cnt_synapse_actions;
	priv->synapses.num_actions = ARRAY_SIZE(interrupt_cnt_synapse_actions);
	priv->synapses.signal = &priv->signals;

	priv->cnts.name = "Channel 0 Count";
	priv->cnts.functions_list = interrupt_cnt_functions;
	priv->cnts.num_functions = ARRAY_SIZE(interrupt_cnt_functions);
	priv->cnts.synapses = &priv->synapses;
	priv->cnts.num_synapses = 1;
	priv->cnts.ext = interrupt_cnt_ext;
	priv->cnts.num_ext = ARRAY_SIZE(interrupt_cnt_ext);

	counter->name = dev_name(dev);
	counter->parent = dev;
	counter->ops = &interrupt_cnt_ops;
	counter->counts = &priv->cnts;
	counter->num_counts = 1;

	irq_set_status_flags(priv->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(dev, priv->irq, interrupt_cnt_isr,
			       IRQF_TRIGGER_RISING | IRQF_NO_THREAD,
			       dev_name(dev), counter);
	if (ret)
		return ret;

	mutex_init(&priv->lock);

	ret = devm_counter_add(dev, counter);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to add counter\n");

	return 0;
}

static const struct of_device_id interrupt_cnt_of_match[] = {
	{ .compatible = "interrupt-counter", },
	{}
};
MODULE_DEVICE_TABLE(of, interrupt_cnt_of_match);

static struct platform_driver interrupt_cnt_driver = {
	.probe = interrupt_cnt_probe,
	.driver = {
		.name = INTERRUPT_CNT_NAME,
		.of_match_table = interrupt_cnt_of_match,
	},
};
module_platform_driver(interrupt_cnt_driver);

MODULE_ALIAS("platform:interrupt-counter");
MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
MODULE_DESCRIPTION("Interrupt counter driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("COUNTER");
