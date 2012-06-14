/*
 * I2C multiplexer using GPIO API
 *
 * Peter Korsgaard <peter.korsgaard@barco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/i2c-mux-gpio.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>

struct gpiomux {
	struct i2c_adapter *parent;
	struct i2c_adapter **adap; /* child busses */
	struct i2c_mux_gpio_platform_data data;
};

static void i2c_mux_gpio_set(const struct gpiomux *mux, unsigned val)
{
	int i;

	for (i = 0; i < mux->data.n_gpios; i++)
		gpio_set_value(mux->data.gpios[i], val & (1 << i));
}

static int i2c_mux_gpio_select(struct i2c_adapter *adap, void *data, u32 chan)
{
	struct gpiomux *mux = data;

	i2c_mux_gpio_set(mux, mux->data.values[chan]);

	return 0;
}

static int i2c_mux_gpio_deselect(struct i2c_adapter *adap, void *data, u32 chan)
{
	struct gpiomux *mux = data;

	i2c_mux_gpio_set(mux, mux->data.idle);

	return 0;
}

static int __devinit i2c_mux_gpio_probe(struct platform_device *pdev)
{
	struct gpiomux *mux;
	struct i2c_mux_gpio_platform_data *pdata;
	struct i2c_adapter *parent;
	int (*deselect) (struct i2c_adapter *, void *, u32);
	unsigned initial_state;
	int i, ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -ENODEV;
	}

	parent = i2c_get_adapter(pdata->parent);
	if (!parent) {
		dev_err(&pdev->dev, "Parent adapter (%d) not found\n",
			pdata->parent);
		return -ENODEV;
	}

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux) {
		ret = -ENOMEM;
		goto alloc_failed;
	}

	mux->parent = parent;
	mux->data = *pdata;
	mux->adap = kzalloc(sizeof(struct i2c_adapter *) * pdata->n_values,
			    GFP_KERNEL);
	if (!mux->adap) {
		ret = -ENOMEM;
		goto alloc_failed2;
	}

	if (pdata->idle != I2C_MUX_GPIO_NO_IDLE) {
		initial_state = pdata->idle;
		deselect = i2c_mux_gpio_deselect;
	} else {
		initial_state = pdata->values[0];
		deselect = NULL;
	}

	for (i = 0; i < pdata->n_gpios; i++) {
		ret = gpio_request(pdata->gpios[i], "i2c-mux-gpio");
		if (ret)
			goto err_request_gpio;
		gpio_direction_output(pdata->gpios[i],
				      initial_state & (1 << i));
	}

	for (i = 0; i < pdata->n_values; i++) {
		u32 nr = pdata->base_nr ? (pdata->base_nr + i) : 0;

		mux->adap[i] = i2c_add_mux_adapter(parent, &pdev->dev, mux, nr, i,
						   i2c_mux_gpio_select, deselect);
		if (!mux->adap[i]) {
			ret = -ENODEV;
			dev_err(&pdev->dev, "Failed to add adapter %d\n", i);
			goto add_adapter_failed;
		}
	}

	dev_info(&pdev->dev, "%d port mux on %s adapter\n",
		 pdata->n_values, parent->name);

	platform_set_drvdata(pdev, mux);

	return 0;

add_adapter_failed:
	for (; i > 0; i--)
		i2c_del_mux_adapter(mux->adap[i - 1]);
	i = pdata->n_gpios;
err_request_gpio:
	for (; i > 0; i--)
		gpio_free(pdata->gpios[i - 1]);
	kfree(mux->adap);
alloc_failed2:
	kfree(mux);
alloc_failed:
	i2c_put_adapter(parent);

	return ret;
}

static int __devexit i2c_mux_gpio_remove(struct platform_device *pdev)
{
	struct gpiomux *mux = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < mux->data.n_values; i++)
		i2c_del_mux_adapter(mux->adap[i]);

	for (i = 0; i < mux->data.n_gpios; i++)
		gpio_free(mux->data.gpios[i]);

	platform_set_drvdata(pdev, NULL);
	i2c_put_adapter(mux->parent);
	kfree(mux->adap);
	kfree(mux);

	return 0;
}

static struct platform_driver i2c_mux_gpio_driver = {
	.probe	= i2c_mux_gpio_probe,
	.remove	= __devexit_p(i2c_mux_gpio_remove),
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "i2c-mux-gpio",
	},
};

module_platform_driver(i2c_mux_gpio_driver);

MODULE_DESCRIPTION("GPIO-based I2C multiplexer driver");
MODULE_AUTHOR("Peter Korsgaard <peter.korsgaard@barco.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-mux-gpio");
