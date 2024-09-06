// SPDX-License-Identifier: GPL-2.0
/*
 * Theobroma Systems Mule I2C device multiplexer
 *
 * Copyright (C) 2024 Theobroma Systems Design und Consulting GmbH
 */

#include <linux/i2c-mux.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define MULE_I2C_MUX_CONFIG_REG  0xff
#define MULE_I2C_MUX_DEFAULT_DEV 0x0

struct mule_i2c_reg_mux {
	struct regmap *regmap;
};

static int mule_i2c_mux_select(struct i2c_mux_core *muxc, u32 dev)
{
	struct mule_i2c_reg_mux *mux = muxc->priv;

	return regmap_write(mux->regmap, MULE_I2C_MUX_CONFIG_REG, dev);
}

static int mule_i2c_mux_deselect(struct i2c_mux_core *muxc, u32 dev)
{
	return mule_i2c_mux_select(muxc, MULE_I2C_MUX_DEFAULT_DEV);
}

static void mule_i2c_mux_remove(void *data)
{
	struct i2c_mux_core *muxc = data;

	i2c_mux_del_adapters(muxc);

	mule_i2c_mux_deselect(muxc, MULE_I2C_MUX_DEFAULT_DEV);
}

static int mule_i2c_mux_probe(struct platform_device *pdev)
{
	struct device *mux_dev = &pdev->dev;
	struct mule_i2c_reg_mux *priv;
	struct i2c_client *client;
	struct i2c_mux_core *muxc;
	struct device_node *dev;
	unsigned int readback;
	int ndev, ret;
	bool old_fw;

	/* Count devices on the mux */
	ndev = of_get_child_count(mux_dev->of_node);
	dev_dbg(mux_dev, "%d devices on the mux\n", ndev);

	client = to_i2c_client(mux_dev->parent);

	muxc = i2c_mux_alloc(client->adapter, mux_dev, ndev, sizeof(*priv),
			     I2C_MUX_LOCKED, mule_i2c_mux_select, mule_i2c_mux_deselect);
	if (!muxc)
		return -ENOMEM;

	priv = i2c_mux_priv(muxc);

	priv->regmap = dev_get_regmap(mux_dev->parent, NULL);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(mux_dev, PTR_ERR(priv->regmap),
				     "No parent i2c register map\n");

	platform_set_drvdata(pdev, muxc);

	/*
	 * MULE_I2C_MUX_DEFAULT_DEV is guaranteed to exist on all old and new
	 * mule fw. Mule fw without mux support will accept write ops to the
	 * config register, but readback returns 0xff (register not updated).
	 */
	ret = mule_i2c_mux_select(muxc, MULE_I2C_MUX_DEFAULT_DEV);
	if (ret)
		return dev_err_probe(mux_dev, ret,
				     "Failed to write config register\n");

	ret = regmap_read(priv->regmap, MULE_I2C_MUX_CONFIG_REG, &readback);
	if (ret)
		return dev_err_probe(mux_dev, ret,
				     "Failed to read config register\n");

	old_fw = (readback != MULE_I2C_MUX_DEFAULT_DEV);

	ret = devm_add_action_or_reset(mux_dev, mule_i2c_mux_remove, muxc);
	if (ret)
		return dev_err_probe(mux_dev, ret,
				     "Failed to register mux remove\n");

	/* Create device adapters */
	for_each_child_of_node(mux_dev->of_node, dev) {
		u32 reg;

		ret = of_property_read_u32(dev, "reg", &reg);
		if (ret)
			return dev_err_probe(mux_dev, ret,
					     "No reg property found for %s\n",
					     of_node_full_name(dev));

		if (old_fw && reg != 0) {
			dev_warn(mux_dev,
				 "Mux is not supported, please update Mule FW\n");
			continue;
		}

		ret = mule_i2c_mux_select(muxc, reg);
		if (ret) {
			dev_warn(mux_dev,
				 "Device %d not supported, please update Mule FW\n", reg);
			continue;
		}

		ret = i2c_mux_add_adapter(muxc, 0, reg);
		if (ret)
			return ret;
	}

	mule_i2c_mux_deselect(muxc, MULE_I2C_MUX_DEFAULT_DEV);

	return 0;
}

static const struct of_device_id mule_i2c_mux_of_match[] = {
	{ .compatible = "tsd,mule-i2c-mux", },
	{},
};
MODULE_DEVICE_TABLE(of, mule_i2c_mux_of_match);

static struct platform_driver mule_i2c_mux_driver = {
	.driver = {
		.name	= "mule-i2c-mux",
		.of_match_table = mule_i2c_mux_of_match,
	},
	.probe		= mule_i2c_mux_probe,
};

module_platform_driver(mule_i2c_mux_driver);

MODULE_AUTHOR("Farouk Bouabid <farouk.bouabid@cherry.de>");
MODULE_DESCRIPTION("I2C mux driver for Theobroma Systems Mule");
MODULE_LICENSE("GPL");
