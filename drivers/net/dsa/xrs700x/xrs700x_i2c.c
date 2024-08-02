// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 NovaTech LLC
 * George McCollister <george.mccollister@gmail.com>
 */

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include "xrs700x.h"
#include "xrs700x_reg.h"

struct xrs700x_i2c_cmd {
	__be32 reg;
	__be16 val;
} __packed;

static int xrs700x_i2c_reg_read(void *context, unsigned int reg,
				unsigned int *val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct xrs700x_i2c_cmd cmd;
	int ret;

	cmd.reg = cpu_to_be32(reg | 1);

	ret = i2c_master_send(i2c, (char *)&cmd.reg, sizeof(cmd.reg));
	if (ret < 0) {
		dev_err(dev, "xrs i2c_master_send returned %d\n", ret);
		return ret;
	}

	ret = i2c_master_recv(i2c, (char *)&cmd.val, sizeof(cmd.val));
	if (ret < 0) {
		dev_err(dev, "xrs i2c_master_recv returned %d\n", ret);
		return ret;
	}

	*val = be16_to_cpu(cmd.val);
	return 0;
}

static int xrs700x_i2c_reg_write(void *context, unsigned int reg,
				 unsigned int val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct xrs700x_i2c_cmd cmd;
	int ret;

	cmd.reg = cpu_to_be32(reg);
	cmd.val = cpu_to_be16(val);

	ret = i2c_master_send(i2c, (char *)&cmd, sizeof(cmd));
	if (ret < 0) {
		dev_err(dev, "xrs i2c_master_send returned %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct regmap_config xrs700x_i2c_regmap_config = {
	.val_bits = 16,
	.reg_stride = 2,
	.reg_bits = 32,
	.pad_bits = 0,
	.write_flag_mask = 0,
	.read_flag_mask = 0,
	.reg_read = xrs700x_i2c_reg_read,
	.reg_write = xrs700x_i2c_reg_write,
	.max_register = 0,
	.cache_type = REGCACHE_NONE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG
};

static int xrs700x_i2c_probe(struct i2c_client *i2c)
{
	struct xrs700x *priv;
	int ret;

	priv = xrs700x_switch_alloc(&i2c->dev, i2c);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init(&i2c->dev, NULL, &i2c->dev,
					&xrs700x_i2c_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&i2c->dev, "Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, priv);

	ret = xrs700x_switch_register(priv);

	/* Main DSA driver may not be started yet. */
	if (ret)
		return ret;

	return 0;
}

static void xrs700x_i2c_remove(struct i2c_client *i2c)
{
	struct xrs700x *priv = i2c_get_clientdata(i2c);

	if (!priv)
		return;

	xrs700x_switch_remove(priv);
}

static void xrs700x_i2c_shutdown(struct i2c_client *i2c)
{
	struct xrs700x *priv = i2c_get_clientdata(i2c);

	if (!priv)
		return;

	xrs700x_switch_shutdown(priv);

	i2c_set_clientdata(i2c, NULL);
}

static const struct i2c_device_id xrs700x_i2c_id[] = {
	{ "xrs700x-switch" },
	{}
};

MODULE_DEVICE_TABLE(i2c, xrs700x_i2c_id);

static const struct of_device_id __maybe_unused xrs700x_i2c_dt_ids[] = {
	{ .compatible = "arrow,xrs7003e", .data = &xrs7003e_info },
	{ .compatible = "arrow,xrs7003f", .data = &xrs7003f_info },
	{ .compatible = "arrow,xrs7004e", .data = &xrs7004e_info },
	{ .compatible = "arrow,xrs7004f", .data = &xrs7004f_info },
	{},
};
MODULE_DEVICE_TABLE(of, xrs700x_i2c_dt_ids);

static struct i2c_driver xrs700x_i2c_driver = {
	.driver = {
		.name	= "xrs700x-i2c",
		.of_match_table = of_match_ptr(xrs700x_i2c_dt_ids),
	},
	.probe = xrs700x_i2c_probe,
	.remove	= xrs700x_i2c_remove,
	.shutdown = xrs700x_i2c_shutdown,
	.id_table = xrs700x_i2c_id,
};

module_i2c_driver(xrs700x_i2c_driver);

MODULE_AUTHOR("George McCollister <george.mccollister@gmail.com>");
MODULE_DESCRIPTION("Arrow SpeedChips XRS700x DSA I2C driver");
MODULE_LICENSE("GPL v2");
