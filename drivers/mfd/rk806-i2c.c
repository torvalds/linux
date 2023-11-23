// SPDX-License-Identifier: GPL-2.0
/*
 * rk806-i2c.c  --  I2C access for Rockchip RK806
 *
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */

#include <linux/i2c.h>
#include <linux/mfd/rk806.h>
#include <linux/regmap.h>

static int rk806_i2c_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct rk806 *rk806;

	rk806 = devm_kzalloc(&client->dev, sizeof(*rk806), GFP_KERNEL);
	if (!rk806)
		return -ENOMEM;

	i2c_set_clientdata(client, rk806);
	rk806->dev = &client->dev;
	rk806->irq = client->irq;

	if (!client->irq) {
		dev_err(&client->dev, "No interrupt support, no core IRQ\n");
		return -EINVAL;
	}

	rk806->regmap = devm_regmap_init_i2c(client, &rk806_regmap_config);
	if (IS_ERR(rk806->regmap)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk806->regmap);
	}

	return rk806_device_init(rk806);
}

static int rk806_remove(struct i2c_client *client)
{
	struct rk806 *rk806 = i2c_get_clientdata(client);

	rk806_device_exit(rk806);

	return 0;
}

static struct i2c_driver rk806_i2c_driver = {
	.driver = {
		.name = "rk806",
		.of_match_table = of_match_ptr(rk806_of_match),
	},
	.probe    = rk806_i2c_probe,
	.remove   = rk806_remove,
};
module_i2c_driver(rk806_i2c_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("RK806 I2C Interface Driver");
MODULE_LICENSE("GPL");
