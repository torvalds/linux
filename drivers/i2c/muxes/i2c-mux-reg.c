/*
 * I2C multiplexer using a single register
 *
 * Copyright 2015 Freescale Semiconductor
 * York Sun  <yorksun@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_data/i2c-mux-reg.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct regmux {
	struct i2c_adapter *parent;
	struct i2c_adapter **adap; /* child busses */
	struct i2c_mux_reg_platform_data data;
};

static int i2c_mux_reg_set(const struct regmux *mux, unsigned int chan_id)
{
	if (!mux->data.reg)
		return -EINVAL;

	/*
	 * Write to the register, followed by a read to ensure the write is
	 * completed on a "posted" bus, for example PCI or write buffers.
	 * The endianness of reading doesn't matter and the return data
	 * is not used.
	 */
	switch (mux->data.reg_size) {
	case 4:
		if (mux->data.little_endian)
			iowrite32(chan_id, mux->data.reg);
		else
			iowrite32be(chan_id, mux->data.reg);
		if (!mux->data.write_only)
			ioread32(mux->data.reg);
		break;
	case 2:
		if (mux->data.little_endian)
			iowrite16(chan_id, mux->data.reg);
		else
			iowrite16be(chan_id, mux->data.reg);
		if (!mux->data.write_only)
			ioread16(mux->data.reg);
		break;
	case 1:
		iowrite8(chan_id, mux->data.reg);
		if (!mux->data.write_only)
			ioread8(mux->data.reg);
		break;
	}

	return 0;
}

static int i2c_mux_reg_select(struct i2c_adapter *adap, void *data,
			      unsigned int chan)
{
	struct regmux *mux = data;

	return i2c_mux_reg_set(mux, chan);
}

static int i2c_mux_reg_deselect(struct i2c_adapter *adap, void *data,
				unsigned int chan)
{
	struct regmux *mux = data;

	if (mux->data.idle_in_use)
		return i2c_mux_reg_set(mux, mux->data.idle);

	return 0;
}

#ifdef CONFIG_OF
static int i2c_mux_reg_probe_dt(struct regmux *mux,
					struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *adapter_np, *child;
	struct i2c_adapter *adapter;
	struct resource res;
	unsigned *values;
	int i = 0;

	if (!np)
		return -ENODEV;

	adapter_np = of_parse_phandle(np, "i2c-parent", 0);
	if (!adapter_np) {
		dev_err(&pdev->dev, "Cannot parse i2c-parent\n");
		return -ENODEV;
	}
	adapter = of_find_i2c_adapter_by_node(adapter_np);
	of_node_put(adapter_np);
	if (!adapter)
		return -EPROBE_DEFER;

	mux->parent = adapter;
	mux->data.parent = i2c_adapter_id(adapter);
	put_device(&adapter->dev);

	mux->data.n_values = of_get_child_count(np);
	if (of_find_property(np, "little-endian", NULL)) {
		mux->data.little_endian = true;
	} else if (of_find_property(np, "big-endian", NULL)) {
		mux->data.little_endian = false;
	} else {
#if defined(__BYTE_ORDER) ? __BYTE_ORDER == __LITTLE_ENDIAN : \
	defined(__LITTLE_ENDIAN)
		mux->data.little_endian = true;
#elif defined(__BYTE_ORDER) ? __BYTE_ORDER == __BIG_ENDIAN : \
	defined(__BIG_ENDIAN)
		mux->data.little_endian = false;
#else
#error Endianness not defined?
#endif
	}
	if (of_find_property(np, "write-only", NULL))
		mux->data.write_only = true;
	else
		mux->data.write_only = false;

	values = devm_kzalloc(&pdev->dev,
			      sizeof(*mux->data.values) * mux->data.n_values,
			      GFP_KERNEL);
	if (!values) {
		dev_err(&pdev->dev, "Cannot allocate values array");
		return -ENOMEM;
	}

	for_each_child_of_node(np, child) {
		of_property_read_u32(child, "reg", values + i);
		i++;
	}
	mux->data.values = values;

	if (!of_property_read_u32(np, "idle-state", &mux->data.idle))
		mux->data.idle_in_use = true;

	/* map address from "reg" if exists */
	if (of_address_to_resource(np, 0, &res) == 0) {
		mux->data.reg_size = resource_size(&res);
		mux->data.reg = devm_ioremap_resource(&pdev->dev, &res);
		if (IS_ERR(mux->data.reg))
			return PTR_ERR(mux->data.reg);
	}

	return 0;
}
#else
static int i2c_mux_reg_probe_dt(struct regmux *mux,
					struct platform_device *pdev)
{
	return 0;
}
#endif

static int i2c_mux_reg_probe(struct platform_device *pdev)
{
	struct regmux *mux;
	struct i2c_adapter *parent;
	struct resource *res;
	int (*deselect)(struct i2c_adapter *, void *, u32);
	unsigned int class;
	int i, ret, nr;

	mux = devm_kzalloc(&pdev->dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	platform_set_drvdata(pdev, mux);

	if (dev_get_platdata(&pdev->dev)) {
		memcpy(&mux->data, dev_get_platdata(&pdev->dev),
			sizeof(mux->data));

		parent = i2c_get_adapter(mux->data.parent);
		if (!parent)
			return -EPROBE_DEFER;

		mux->parent = parent;
	} else {
		ret = i2c_mux_reg_probe_dt(mux, pdev);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error parsing device tree");
			return ret;
		}
	}

	if (!mux->data.reg) {
		dev_info(&pdev->dev,
			"Register not set, using platform resource\n");
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		mux->data.reg_size = resource_size(res);
		mux->data.reg = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(mux->data.reg))
			return PTR_ERR(mux->data.reg);
	}

	if (mux->data.reg_size != 4 && mux->data.reg_size != 2 &&
	    mux->data.reg_size != 1) {
		dev_err(&pdev->dev, "Invalid register size\n");
		return -EINVAL;
	}

	mux->adap = devm_kzalloc(&pdev->dev,
				 sizeof(*mux->adap) * mux->data.n_values,
				 GFP_KERNEL);
	if (!mux->adap) {
		dev_err(&pdev->dev, "Cannot allocate i2c_adapter structure");
		return -ENOMEM;
	}

	if (mux->data.idle_in_use)
		deselect = i2c_mux_reg_deselect;
	else
		deselect = NULL;

	for (i = 0; i < mux->data.n_values; i++) {
		nr = mux->data.base_nr ? (mux->data.base_nr + i) : 0;
		class = mux->data.classes ? mux->data.classes[i] : 0;

		mux->adap[i] = i2c_add_mux_adapter(mux->parent, &pdev->dev, mux,
						   nr, mux->data.values[i],
						   class, i2c_mux_reg_select,
						   deselect);
		if (!mux->adap[i]) {
			ret = -ENODEV;
			dev_err(&pdev->dev, "Failed to add adapter %d\n", i);
			goto add_adapter_failed;
		}
	}

	dev_dbg(&pdev->dev, "%d port mux on %s adapter\n",
		 mux->data.n_values, mux->parent->name);

	return 0;

add_adapter_failed:
	for (; i > 0; i--)
		i2c_del_mux_adapter(mux->adap[i - 1]);

	return ret;
}

static int i2c_mux_reg_remove(struct platform_device *pdev)
{
	struct regmux *mux = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < mux->data.n_values; i++)
		i2c_del_mux_adapter(mux->adap[i]);

	i2c_put_adapter(mux->parent);

	return 0;
}

static const struct of_device_id i2c_mux_reg_of_match[] = {
	{ .compatible = "i2c-mux-reg", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_mux_reg_of_match);

static struct platform_driver i2c_mux_reg_driver = {
	.probe	= i2c_mux_reg_probe,
	.remove	= i2c_mux_reg_remove,
	.driver	= {
		.name	= "i2c-mux-reg",
	},
};

module_platform_driver(i2c_mux_reg_driver);

MODULE_DESCRIPTION("Register-based I2C multiplexer driver");
MODULE_AUTHOR("York Sun <yorksun@freescale.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-mux-reg");
