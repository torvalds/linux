// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include "common.h"

struct p8_i2c_occ {
	struct occ occ;
	struct i2c_client *client;
};

#define to_p8_i2c_occ(x)	container_of((x), struct p8_i2c_occ, occ)

static int p8_i2c_occ_send_cmd(struct occ *occ, u8 *cmd)
{
	return -EOPNOTSUPP;
}

static int p8_i2c_occ_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct occ *occ;
	struct p8_i2c_occ *ctx = devm_kzalloc(&client->dev, sizeof(*ctx),
					      GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->client = client;
	occ = &ctx->occ;
	occ->bus_dev = &client->dev;
	dev_set_drvdata(&client->dev, occ);

	occ->poll_cmd_data = 0x10;		/* P8 OCC poll data */
	occ->send_cmd = p8_i2c_occ_send_cmd;

	return occ_setup(occ, "p8_occ");
}

static const struct of_device_id p8_i2c_occ_of_match[] = {
	{ .compatible = "ibm,p8-occ-hwmon" },
	{}
};
MODULE_DEVICE_TABLE(of, p8_i2c_occ_of_match);

static struct i2c_driver p8_i2c_occ_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "occ-hwmon",
		.of_match_table = p8_i2c_occ_of_match,
	},
	.probe = p8_i2c_occ_probe,
};

module_i2c_driver(p8_i2c_occ_driver);

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("BMC P8 OCC hwmon driver");
MODULE_LICENSE("GPL");
