// SPDX-License-Identifier: GPL-2.0-only
/* MCP23S08 I2C GPIO driver */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "pinctrl-mcp23s08.h"

static int mcp230xx_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	unsigned int type = id->driver_data;
	struct mcp23s08 *mcp;
	int ret;

	mcp = devm_kzalloc(dev, sizeof(*mcp), GFP_KERNEL);
	if (!mcp)
		return -ENOMEM;

	switch (type) {
	case MCP_TYPE_008:
		mcp->regmap = devm_regmap_init_i2c(client, &mcp23x08_regmap);
		mcp->reg_shift = 0;
		mcp->chip.ngpio = 8;
		mcp->chip.label = "mcp23008";
		break;

	case MCP_TYPE_017:
		mcp->regmap = devm_regmap_init_i2c(client, &mcp23x17_regmap);
		mcp->reg_shift = 1;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23017";
		break;

	case MCP_TYPE_018:
		mcp->regmap = devm_regmap_init_i2c(client, &mcp23x17_regmap);
		mcp->reg_shift = 1;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23018";
		break;

	default:
		dev_err(dev, "invalid device type (%d)\n", type);
		return -EINVAL;
	}

	if (IS_ERR(mcp->regmap))
		return PTR_ERR(mcp->regmap);

	mcp->irq = client->irq;
	mcp->pinctrl_desc.name = "mcp23xxx-pinctrl";

	ret = mcp23s08_probe_one(mcp, dev, client->addr, type, -1);
	if (ret)
		return ret;

	i2c_set_clientdata(client, mcp);

	return 0;
}

static const struct i2c_device_id mcp230xx_id[] = {
	{ "mcp23008", MCP_TYPE_008 },
	{ "mcp23017", MCP_TYPE_017 },
	{ "mcp23018", MCP_TYPE_018 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp230xx_id);

static const struct of_device_id mcp23s08_i2c_of_match[] = {
	{
		.compatible = "microchip,mcp23008",
		.data = (void *) MCP_TYPE_008,
	},
	{
		.compatible = "microchip,mcp23017",
		.data = (void *) MCP_TYPE_017,
	},
	{
		.compatible = "microchip,mcp23018",
		.data = (void *) MCP_TYPE_018,
	},
/* NOTE: The use of the mcp prefix is deprecated and will be removed. */
	{
		.compatible = "mcp,mcp23008",
		.data = (void *) MCP_TYPE_008,
	},
	{
		.compatible = "mcp,mcp23017",
		.data = (void *) MCP_TYPE_017,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mcp23s08_i2c_of_match);

static struct i2c_driver mcp230xx_driver = {
	.driver = {
		.name	= "mcp230xx",
		.of_match_table = mcp23s08_i2c_of_match,
	},
	.probe		= mcp230xx_probe,
	.id_table	= mcp230xx_id,
};

static int __init mcp23s08_i2c_init(void)
{
	return i2c_add_driver(&mcp230xx_driver);
}

/*
 * Register after I²C postcore initcall and before
 * subsys initcalls that may rely on these GPIOs.
 */
subsys_initcall(mcp23s08_i2c_init);

static void mcp23s08_i2c_exit(void)
{
	i2c_del_driver(&mcp230xx_driver);
}
module_exit(mcp23s08_i2c_exit);

MODULE_LICENSE("GPL");
