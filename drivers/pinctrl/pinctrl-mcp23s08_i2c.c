// SPDX-License-Identifier: GPL-2.0-only
/* MCP23S08 I2C GPIO driver */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "pinctrl-mcp23s08.h"

static int mcp230xx_probe(struct i2c_client *client)
{
	const struct mcp23s08_info *info;
	struct device *dev = &client->dev;
	struct mcp23s08 *mcp;
	int ret;

	mcp = devm_kzalloc(dev, sizeof(*mcp), GFP_KERNEL);
	if (!mcp)
		return -ENOMEM;

	info = i2c_get_match_data(client);
	if (!info)
		return dev_err_probe(dev, -EINVAL, "invalid device type\n");

	mcp->reg_shift = info->reg_shift;
	mcp->chip.ngpio = info->ngpio;
	mcp->chip.label = info->label;
	mcp->regmap = devm_regmap_init_i2c(client, info->regmap);
	if (IS_ERR(mcp->regmap))
		return PTR_ERR(mcp->regmap);

	mcp->irq = client->irq;
	mcp->pinctrl_desc.name = "mcp23xxx-pinctrl";

	ret = mcp23s08_probe_one(mcp, dev, client->addr, info->type, -1);
	if (ret)
		return ret;

	i2c_set_clientdata(client, mcp);

	return 0;
}

static const struct mcp23s08_info mcp23008_i2c = {
	.regmap = &mcp23x08_regmap,
	.label = "mcp23008",
	.type = MCP_TYPE_008,
	.ngpio = 8,
	.reg_shift = 0,
};

static const struct mcp23s08_info mcp23017_i2c = {
	.regmap = &mcp23x17_regmap,
	.label = "mcp23017",
	.type = MCP_TYPE_017,
	.ngpio = 16,
	.reg_shift = 1,
};

static const struct mcp23s08_info  mcp23018_i2c = {
	.regmap = &mcp23x17_regmap,
	.label = "mcp23018",
	.type = MCP_TYPE_018,
	.ngpio = 16,
	.reg_shift = 1,
};

static const struct i2c_device_id mcp230xx_id[] = {
	{ "mcp23008", (kernel_ulong_t)&mcp23008_i2c },
	{ "mcp23017", (kernel_ulong_t)&mcp23017_i2c },
	{ "mcp23018", (kernel_ulong_t)&mcp23018_i2c },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp230xx_id);

static const struct of_device_id mcp23s08_i2c_of_match[] = {
	{ .compatible = "microchip,mcp23008", .data = &mcp23008_i2c },
	{ .compatible = "microchip,mcp23017", .data = &mcp23017_i2c },
	{ .compatible = "microchip,mcp23018", .data = &mcp23018_i2c },
/* NOTE: The use of the mcp prefix is deprecated and will be removed. */
	{ .compatible = "mcp,mcp23008", .data = &mcp23008_i2c },
	{ .compatible = "mcp,mcp23017", .data = &mcp23017_i2c },
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
