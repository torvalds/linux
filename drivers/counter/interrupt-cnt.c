// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */

#include <linux/counter.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define INTERRUPT_CNT_NAME "interrupt-cnt"

struct interrupt_cnt_priv {
	atomic_t count;
	struct counter_device counter;
	struct gpio_desc *gpio;
	int irq;
	bool enabled;
	struct counter_signal signals;
	struct counter_synapse synapses;
	struct counter_count cnts;
};

static irqreturn_t interrupt_cnt_isr(int irq, void *dev_id)
{
	struct interrupt_cnt_priv *priv = dev_id;

	atomic_inc(&priv->count);

	return IRQ_HANDLED;
}

static ssize_t interrupt_cnt_enable_read(struct counter_device *counter,
					 struct counter_count *count,
					 void *private, char *buf)
{
	struct interrupt_cnt_priv *priv = counter->priv;

	return sysfs_emit(buf, "%d\n", priv->enabled);
}

static ssize_t interrupt_cnt_enable_write(struct counter_device *counter,
					  struct counter_count *count,
					  void *private, const char *buf,
					  size_t len)
{
	struct interrupt_cnt_priv *priv = counter->priv;
	bool enable;
	ssize_t ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	if (priv->enabled == enable)
		return len;

	if (enable) {
		priv->enabled = true;
		enable_irq(priv->irq);
	} else {
		disable_irq(priv->irq);
		priv->enabled = false;
	}

	return len;
}

static const struct counter_count_ext interrupt_cnt_ext[] = {
	{
		.name = "enable",
		.read = interrupt_cnt_enable_read,
		.write = interrupt_cnt_enable_write,
	},
};

static const enum counter_synapse_action interrupt_cnt_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
};

static int interrupt_cnt_action_get(struct counter_device *counter,
				    struct counter_count *count,
				    struct counter_synapse *synapse,
				    size_t *action)
{
	*action = 0;

	return 0;
}

static int interrupt_cnt_read(struct counter_device *counter,
			      struct counter_count *count, unsigned long *val)
{
	struct interrupt_cnt_priv *priv = counter->priv;

	*val = atomic_read(&priv->count);

	return 0;
}

static int interrupt_cnt_write(struct counter_device *counter,
			       struct counter_count *count,
			       const unsigned long val)
{
	struct interrupt_cnt_priv *priv = counter->priv;

	atomic_set(&priv->count, val);

	return 0;
}

static const enum counter_count_function interrupt_cnt_functions[] = {
	COUNTER_COUNT_FUNCTION_INCREASE,
};

static int interrupt_cnt_function_get(struct counter_device *counter,
				      struct counter_count *count,
				      size_t *function)
{
	*function = 0;

	return 0;
}

static int interrupt_cnt_signal_read(struct counter_device *counter,
				     struct counter_signal *signal,
				     enum counter_signal_value *val)
{
	struct interrupt_cnt_priv *priv = counter->priv;
	int ret;

	if (!priv->gpio)
		return -EINVAL;

	ret = gpiod_get_value(priv->gpio);
	if (ret < 0)
		return ret;

	*val = ret ? COUNTER_SIGNAL_HIGH : COUNTER_SIGNAL_LOW;

	return 0;
}

static const struct counter_ops interrupt_cnt_ops = {
	.action_get = interrupt_cnt_action_get,
	.count_read = interrupt_cnt_read,
	.count_write = interrupt_cnt_write,
	.function_get = interrupt_cnt_function_get,
	.signal_read  = interrupt_cnt_signal_read,
};

static int interrupt_cnt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct interrupt_cnt_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

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

	priv->counter.signals = &priv->signals;
	priv->counter.num_signals = 1;

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

	priv->counter.priv = priv;
	priv->counter.name = dev_name(dev);
	priv->counter.parent = dev;
	priv->counter.ops = &interrupt_cnt_ops;
	priv->counter.counts = &priv->cnts;
	priv->counter.num_counts = 1;

	irq_set_status_flags(priv->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(dev, priv->irq, interrupt_cnt_isr,
			       IRQF_TRIGGER_RISING | IRQF_NO_THREAD,
			       dev_name(dev), priv);
	if (ret)
		return ret;

	return devm_counter_register(dev, &priv->counter);
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
