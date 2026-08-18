// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * I2C multiplexer using a single register
 *
 * Copyright 2015 Freescale Semiconductor
 * York Sun  <yorksun@freescale.com>
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_data/i2c-mux-reg.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct regmux {
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

static int i2c_mux_reg_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct regmux *mux = i2c_mux_priv(muxc);

	return i2c_mux_reg_set(mux, chan);
}

static int i2c_mux_reg_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct regmux *mux = i2c_mux_priv(muxc);

	if (mux->data.idle_in_use)
		return i2c_mux_reg_set(mux, mux->data.idle);

	return 0;
}

static int i2c_mux_reg_probe_fw(struct regmux *mux, struct device *dev)
{
	struct fwnode_handle *fwnode, *child;
	struct i2c_adapter *adapter;
	unsigned *values;
	int ret, i = 0;

	if (!dev_fwnode(dev))
		return -ENODEV;

	fwnode = fwnode_find_reference(dev_fwnode(dev), "i2c-parent", 0);
	if (IS_ERR(fwnode)) {
		dev_err(dev, "missing 'i2c-parent' property\n");
		return -ENODEV;
	}

	adapter = i2c_find_adapter_by_fwnode(fwnode);
	fwnode_handle_put(fwnode);
	if (!adapter)
		return -EPROBE_DEFER;

	mux->data.parent = i2c_adapter_id(adapter);
	put_device(&adapter->dev);

	mux->data.n_values = device_get_child_node_count(dev);
	if (device_property_read_bool(dev, "little-endian")) {
		mux->data.little_endian = true;
	} else if (device_property_read_bool(dev, "big-endian")) {
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
	mux->data.write_only = device_property_read_bool(dev, "write-only");

	values = devm_kcalloc(dev, mux->data.n_values, sizeof(*mux->data.values),
			      GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	device_for_each_child_node(dev, child) {
		if (is_acpi_device_node(child)) {
			ret = acpi_get_local_address(ACPI_HANDLE_FWNODE(child),
						     &values[i]);
			if (ret) {
				fwnode_handle_put(child);
				return dev_err_probe(dev, ret,
						     "Cannot get address\n");
			}
		} else {
			fwnode_property_read_u32(child, "reg", &values[i]);
		}

		i++;
	}
	mux->data.values = values;

	if (!device_property_read_u32(dev, "idle-state", &mux->data.idle))
		mux->data.idle_in_use = true;

	return 0;
}

static int i2c_mux_reg_probe(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc;
	struct regmux *mux;
	struct i2c_adapter *parent;
	struct resource *res;
	int i, ret, nr;

	mux = devm_kzalloc(&pdev->dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	if (dev_get_platdata(&pdev->dev)) {
		memcpy(&mux->data, dev_get_platdata(&pdev->dev),
			sizeof(mux->data));
	} else {
		ret = i2c_mux_reg_probe_fw(mux, &pdev->dev);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret,
					     "Error parsing firmware description\n");
	}

	if (!mux->data.reg) {
		mux->data.reg = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
		if (IS_ERR(mux->data.reg))
			return PTR_ERR(mux->data.reg);
		mux->data.reg_size = resource_size(res);
	}

	if (mux->data.reg_size != 4 && mux->data.reg_size != 2 &&
	    mux->data.reg_size != 1) {
		dev_err(&pdev->dev, "Invalid register size\n");
		return -EINVAL;
	}

	parent = i2c_get_adapter(mux->data.parent);
	if (!parent)
		return -EPROBE_DEFER;

	muxc = i2c_mux_alloc(parent, &pdev->dev, mux->data.n_values, 0, 0,
			     i2c_mux_reg_select, NULL);
	if (!muxc) {
		ret = -ENOMEM;
		goto err_put_parent;
	}
	muxc->priv = mux;

	platform_set_drvdata(pdev, muxc);

	if (mux->data.idle_in_use)
		muxc->deselect = i2c_mux_reg_deselect;

	for (i = 0; i < mux->data.n_values; i++) {
		nr = mux->data.base_nr ? (mux->data.base_nr + i) : 0;

		ret = i2c_mux_add_adapter(muxc, nr, mux->data.values[i]);
		if (ret)
			goto err_del_mux_adapters;
	}

	dev_dbg(&pdev->dev, "%d port mux on %s adapter\n",
		 mux->data.n_values, muxc->parent->name);

	return 0;

err_del_mux_adapters:
	i2c_mux_del_adapters(muxc);
err_put_parent:
	i2c_put_adapter(parent);

	return ret;
}

static void i2c_mux_reg_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);

	i2c_mux_del_adapters(muxc);
	i2c_put_adapter(muxc->parent);
}

static const struct of_device_id i2c_mux_reg_of_match[] = {
	{ .compatible = "i2c-mux-reg", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_mux_reg_of_match);

static struct platform_driver i2c_mux_reg_driver = {
	.probe	= i2c_mux_reg_probe,
	.remove = i2c_mux_reg_remove,
	.driver	= {
		.name	= "i2c-mux-reg",
		.of_match_table = i2c_mux_reg_of_match,
	},
};

module_platform_driver(i2c_mux_reg_driver);

MODULE_DESCRIPTION("Register-based I2C multiplexer driver");
MODULE_AUTHOR("York Sun <yorksun@freescale.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-mux-reg");
