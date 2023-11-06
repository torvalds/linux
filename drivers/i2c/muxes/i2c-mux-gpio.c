// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C multiplexer using GPIO API
 *
 * Peter Korsgaard <peter.korsgaard@barco.com>
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/overflow.h>
#include <linux/platform_data/i2c-mux-gpio.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bits.h>
#include <linux/gpio/consumer.h>
/* FIXME: stop poking around inside gpiolib */
#include "../../gpio/gpiolib.h"

struct gpiomux {
	struct i2c_mux_gpio_platform_data data;
	int ngpios;
	struct gpio_desc **gpios;
};

static void i2c_mux_gpio_set(const struct gpiomux *mux, unsigned val)
{
	DECLARE_BITMAP(values, BITS_PER_TYPE(val));

	values[0] = val;

	gpiod_set_array_value_cansleep(mux->ngpios, mux->gpios, NULL, values);
}

static int i2c_mux_gpio_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct gpiomux *mux = i2c_mux_priv(muxc);

	i2c_mux_gpio_set(mux, chan);

	return 0;
}

static int i2c_mux_gpio_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct gpiomux *mux = i2c_mux_priv(muxc);

	i2c_mux_gpio_set(mux, mux->data.idle);

	return 0;
}

static int i2c_mux_gpio_probe_fw(struct gpiomux *mux,
				 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct device_node *np = dev->of_node;
	struct device_node *adapter_np;
	struct i2c_adapter *adapter = NULL;
	struct fwnode_handle *child;
	unsigned *values;
	int rc, i = 0;

	if (is_of_node(fwnode)) {
		if (!np)
			return -ENODEV;

		adapter_np = of_parse_phandle(np, "i2c-parent", 0);
		if (!adapter_np) {
			dev_err(&pdev->dev, "Cannot parse i2c-parent\n");
			return -ENODEV;
		}
		adapter = of_find_i2c_adapter_by_node(adapter_np);
		of_node_put(adapter_np);

	} else if (is_acpi_node(fwnode)) {
		/*
		 * In ACPI land the mux should be a direct child of the i2c
		 * bus it muxes.
		 */
		acpi_handle dev_handle = ACPI_HANDLE(dev->parent);

		adapter = i2c_acpi_find_adapter_by_handle(dev_handle);
	}

	if (!adapter)
		return -EPROBE_DEFER;

	mux->data.parent = i2c_adapter_id(adapter);
	put_device(&adapter->dev);

	mux->data.n_values = device_get_child_node_count(dev);
	values = devm_kcalloc(dev,
			      mux->data.n_values, sizeof(*mux->data.values),
			      GFP_KERNEL);
	if (!values) {
		dev_err(dev, "Cannot allocate values array");
		return -ENOMEM;
	}

	device_for_each_child_node(dev, child) {
		if (is_of_node(child)) {
			fwnode_property_read_u32(child, "reg", values + i);

		} else if (is_acpi_node(child)) {
			rc = acpi_get_local_address(ACPI_HANDLE_FWNODE(child), values + i);
			if (rc)
				return dev_err_probe(dev, rc, "Cannot get address\n");
		}

		i++;
	}
	mux->data.values = values;

	if (device_property_read_u32(dev, "idle-state", &mux->data.idle))
		mux->data.idle = I2C_MUX_GPIO_NO_IDLE;

	return 0;
}

static int i2c_mux_gpio_probe(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc;
	struct gpiomux *mux;
	struct i2c_adapter *parent;
	struct i2c_adapter *root;
	unsigned initial_state;
	int i, ngpios, ret;

	mux = devm_kzalloc(&pdev->dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	if (!dev_get_platdata(&pdev->dev)) {
		ret = i2c_mux_gpio_probe_fw(mux, pdev);
		if (ret < 0)
			return ret;
	} else {
		memcpy(&mux->data, dev_get_platdata(&pdev->dev),
			sizeof(mux->data));
	}

	ngpios = gpiod_count(&pdev->dev, "mux");
	if (ngpios <= 0) {
		dev_err(&pdev->dev, "no valid gpios provided\n");
		return ngpios ?: -EINVAL;
	}
	mux->ngpios = ngpios;

	parent = i2c_get_adapter(mux->data.parent);
	if (!parent)
		return -EPROBE_DEFER;

	muxc = i2c_mux_alloc(parent, &pdev->dev, mux->data.n_values,
			     array_size(ngpios, sizeof(*mux->gpios)), 0,
			     i2c_mux_gpio_select, NULL);
	if (!muxc) {
		ret = -ENOMEM;
		goto alloc_failed;
	}
	mux->gpios = muxc->priv;
	muxc->priv = mux;

	platform_set_drvdata(pdev, muxc);

	root = i2c_root_adapter(&parent->dev);

	muxc->mux_locked = true;

	if (mux->data.idle != I2C_MUX_GPIO_NO_IDLE) {
		initial_state = mux->data.idle;
		muxc->deselect = i2c_mux_gpio_deselect;
	} else {
		initial_state = mux->data.values[0];
	}

	for (i = 0; i < ngpios; i++) {
		struct device *gpio_dev;
		struct gpio_desc *gpiod;
		enum gpiod_flags flag;

		if (initial_state & BIT(i))
			flag = GPIOD_OUT_HIGH;
		else
			flag = GPIOD_OUT_LOW;
		gpiod = devm_gpiod_get_index(&pdev->dev, "mux", i, flag);
		if (IS_ERR(gpiod)) {
			ret = PTR_ERR(gpiod);
			goto alloc_failed;
		}

		mux->gpios[i] = gpiod;

		if (!muxc->mux_locked)
			continue;

		/* FIXME: find a proper way to access the GPIO device */
		gpio_dev = &gpiod->gdev->dev;
		muxc->mux_locked = i2c_root_adapter(gpio_dev) == root;
	}

	if (muxc->mux_locked)
		dev_info(&pdev->dev, "mux-locked i2c mux\n");

	for (i = 0; i < mux->data.n_values; i++) {
		u32 nr = mux->data.base_nr ? (mux->data.base_nr + i) : 0;
		unsigned int class = mux->data.classes ? mux->data.classes[i] : 0;

		ret = i2c_mux_add_adapter(muxc, nr, mux->data.values[i], class);
		if (ret)
			goto add_adapter_failed;
	}

	dev_info(&pdev->dev, "%d port mux on %s adapter\n",
		 mux->data.n_values, parent->name);

	return 0;

add_adapter_failed:
	i2c_mux_del_adapters(muxc);
alloc_failed:
	i2c_put_adapter(parent);

	return ret;
}

static void i2c_mux_gpio_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);

	i2c_mux_del_adapters(muxc);
	i2c_put_adapter(muxc->parent);
}

static const struct of_device_id i2c_mux_gpio_of_match[] = {
	{ .compatible = "i2c-mux-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_mux_gpio_of_match);

static struct platform_driver i2c_mux_gpio_driver = {
	.probe	= i2c_mux_gpio_probe,
	.remove_new = i2c_mux_gpio_remove,
	.driver	= {
		.name	= "i2c-mux-gpio",
		.of_match_table = i2c_mux_gpio_of_match,
	},
};

module_platform_driver(i2c_mux_gpio_driver);

MODULE_DESCRIPTION("GPIO-based I2C multiplexer driver");
MODULE_AUTHOR("Peter Korsgaard <peter.korsgaard@barco.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-mux-gpio");
