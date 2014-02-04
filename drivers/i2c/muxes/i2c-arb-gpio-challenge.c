/*
 * GPIO-based I2C Arbitration Using a Challenge & Response Mechanism
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>


/**
 * struct i2c_arbitrator_data - Driver data for I2C arbitrator
 *
 * @parent: Parent adapter
 * @child: Child bus
 * @our_gpio: GPIO we'll use to claim.
 * @our_gpio_release: 0 if active high; 1 if active low; AKA if the GPIO ==
 *   this then consider it released.
 * @their_gpio: GPIO that the other side will use to claim.
 * @their_gpio_release: 0 if active high; 1 if active low; AKA if the GPIO ==
 *   this then consider it released.
 * @slew_delay_us: microseconds to wait for a GPIO to go high.
 * @wait_retry_us: we'll attempt another claim after this many microseconds.
 * @wait_free_us: we'll give up after this many microseconds.
 */

struct i2c_arbitrator_data {
	struct i2c_adapter *parent;
	struct i2c_adapter *child;
	int our_gpio;
	int our_gpio_release;
	int their_gpio;
	int their_gpio_release;
	unsigned int slew_delay_us;
	unsigned int wait_retry_us;
	unsigned int wait_free_us;
};


/**
 * i2c_arbitrator_select - claim the I2C bus
 *
 * Use the GPIO-based signalling protocol; return -EBUSY if we fail.
 */
static int i2c_arbitrator_select(struct i2c_adapter *adap, void *data, u32 chan)
{
	const struct i2c_arbitrator_data *arb = data;
	unsigned long stop_retry, stop_time;

	/* Start a round of trying to claim the bus */
	stop_time = jiffies + usecs_to_jiffies(arb->wait_free_us) + 1;
	do {
		/* Indicate that we want to claim the bus */
		gpio_set_value(arb->our_gpio, !arb->our_gpio_release);
		udelay(arb->slew_delay_us);

		/* Wait for the other master to release it */
		stop_retry = jiffies + usecs_to_jiffies(arb->wait_retry_us) + 1;
		while (time_before(jiffies, stop_retry)) {
			int gpio_val = !!gpio_get_value(arb->their_gpio);

			if (gpio_val == arb->their_gpio_release) {
				/* We got it, so return */
				return 0;
			}

			usleep_range(50, 200);
		}

		/* It didn't release, so give up, wait, and try again */
		gpio_set_value(arb->our_gpio, arb->our_gpio_release);

		usleep_range(arb->wait_retry_us, arb->wait_retry_us * 2);
	} while (time_before(jiffies, stop_time));

	/* Give up, release our claim */
	gpio_set_value(arb->our_gpio, arb->our_gpio_release);
	udelay(arb->slew_delay_us);
	dev_err(&adap->dev, "Could not claim bus, timeout\n");
	return -EBUSY;
}

/**
 * i2c_arbitrator_deselect - release the I2C bus
 *
 * Release the I2C bus using the GPIO-based signalling protocol.
 */
static int i2c_arbitrator_deselect(struct i2c_adapter *adap, void *data,
				   u32 chan)
{
	const struct i2c_arbitrator_data *arb = data;

	/* Release the bus and wait for the other master to notice */
	gpio_set_value(arb->our_gpio, arb->our_gpio_release);
	udelay(arb->slew_delay_us);

	return 0;
}

static int i2c_arbitrator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *parent_np;
	struct i2c_arbitrator_data *arb;
	enum of_gpio_flags gpio_flags;
	unsigned long out_init;
	int ret;

	/* We only support probing from device tree; no platform_data */
	if (!np) {
		dev_err(dev, "Cannot find device tree node\n");
		return -ENODEV;
	}
	if (dev_get_platdata(dev)) {
		dev_err(dev, "Platform data is not supported\n");
		return -EINVAL;
	}

	arb = devm_kzalloc(dev, sizeof(*arb), GFP_KERNEL);
	if (!arb) {
		dev_err(dev, "Cannot allocate i2c_arbitrator_data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, arb);

	/* Request GPIOs */
	ret = of_get_named_gpio_flags(np, "our-claim-gpio", 0, &gpio_flags);
	if (!gpio_is_valid(ret)) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error getting our-claim-gpio\n");
		return ret;
	}
	arb->our_gpio = ret;
	arb->our_gpio_release = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);
	out_init = (gpio_flags & OF_GPIO_ACTIVE_LOW) ?
		GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
	ret = devm_gpio_request_one(dev, arb->our_gpio, out_init,
				    "our-claim-gpio");
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error requesting our-claim-gpio\n");
		return ret;
	}

	ret = of_get_named_gpio_flags(np, "their-claim-gpios", 0, &gpio_flags);
	if (!gpio_is_valid(ret)) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error getting their-claim-gpio\n");
		return ret;
	}
	arb->their_gpio = ret;
	arb->their_gpio_release = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);
	ret = devm_gpio_request_one(dev, arb->their_gpio, GPIOF_IN,
				    "their-claim-gpio");
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error requesting their-claim-gpio\n");
		return ret;
	}

	/* At the moment we only support a single two master (us + 1 other) */
	if (gpio_is_valid(of_get_named_gpio(np, "their-claim-gpios", 1))) {
		dev_err(dev, "Only one other master is supported\n");
		return -EINVAL;
	}

	/* Arbitration parameters */
	if (of_property_read_u32(np, "slew-delay-us", &arb->slew_delay_us))
		arb->slew_delay_us = 10;
	if (of_property_read_u32(np, "wait-retry-us", &arb->wait_retry_us))
		arb->wait_retry_us = 3000;
	if (of_property_read_u32(np, "wait-free-us", &arb->wait_free_us))
		arb->wait_free_us = 50000;

	/* Find our parent */
	parent_np = of_parse_phandle(np, "i2c-parent", 0);
	if (!parent_np) {
		dev_err(dev, "Cannot parse i2c-parent\n");
		return -EINVAL;
	}
	arb->parent = of_find_i2c_adapter_by_node(parent_np);
	if (!arb->parent) {
		dev_err(dev, "Cannot find parent bus\n");
		return -EPROBE_DEFER;
	}

	/* Actually add the mux adapter */
	arb->child = i2c_add_mux_adapter(arb->parent, dev, arb, 0, 0, 0,
					 i2c_arbitrator_select,
					 i2c_arbitrator_deselect);
	if (!arb->child) {
		dev_err(dev, "Failed to add adapter\n");
		ret = -ENODEV;
		i2c_put_adapter(arb->parent);
	}

	return ret;
}

static int i2c_arbitrator_remove(struct platform_device *pdev)
{
	struct i2c_arbitrator_data *arb = platform_get_drvdata(pdev);

	i2c_del_mux_adapter(arb->child);
	i2c_put_adapter(arb->parent);

	return 0;
}

static const struct of_device_id i2c_arbitrator_of_match[] = {
	{ .compatible = "i2c-arb-gpio-challenge", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_arbitrator_of_match);

static struct platform_driver i2c_arbitrator_driver = {
	.probe	= i2c_arbitrator_probe,
	.remove	= i2c_arbitrator_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "i2c-arb-gpio-challenge",
		.of_match_table = i2c_arbitrator_of_match,
	},
};

module_platform_driver(i2c_arbitrator_driver);

MODULE_DESCRIPTION("GPIO-based I2C Arbitration");
MODULE_AUTHOR("Doug Anderson <dianders@chromium.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-arb-gpio-challenge");
