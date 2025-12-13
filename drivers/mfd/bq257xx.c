// SPDX-License-Identifier: GPL-2.0
/*
 * BQ257XX Core Driver
 * Copyright (C) 2025 Chris Morgan <macromorgan@hotmail.com>
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/mfd/bq257xx.h>
#include <linux/mfd/core.h>
#include <linux/regmap.h>

static const struct regmap_range bq25703_readonly_reg_ranges[] = {
	regmap_reg_range(BQ25703_CHARGER_STATUS, BQ25703_MANUFACT_DEV_ID),
};

static const struct regmap_access_table bq25703_writeable_regs = {
	.no_ranges = bq25703_readonly_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(bq25703_readonly_reg_ranges),
};

static const struct regmap_range bq25703_volatile_reg_ranges[] = {
	regmap_reg_range(BQ25703_CHARGE_OPTION_0, BQ25703_IIN_HOST),
	regmap_reg_range(BQ25703_CHARGER_STATUS, BQ25703_ADC_OPTION),
};

static const struct regmap_access_table bq25703_volatile_regs = {
	.yes_ranges = bq25703_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bq25703_volatile_reg_ranges),
};

static const struct regmap_config bq25703_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = BQ25703_ADC_OPTION,
	.cache_type = REGCACHE_MAPLE,
	.wr_table = &bq25703_writeable_regs,
	.volatile_table = &bq25703_volatile_regs,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static const struct mfd_cell cells[] = {
	MFD_CELL_NAME("bq257xx-regulator"),
	MFD_CELL_NAME("bq257xx-charger"),
};

static int bq257xx_probe(struct i2c_client *client)
{
	struct bq257xx_device *ddata;
	int ret;

	ddata = devm_kzalloc(&client->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->client = client;

	ddata->regmap = devm_regmap_init_i2c(client, &bq25703_regmap_config);
	if (IS_ERR(ddata->regmap)) {
		return dev_err_probe(&client->dev, PTR_ERR(ddata->regmap),
				     "Failed to allocate register map\n");
	}

	i2c_set_clientdata(client, ddata);

	ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_AUTO,
				   cells, ARRAY_SIZE(cells), NULL, 0, NULL);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "Failed to register child devices\n");

	return ret;
}

static const struct i2c_device_id bq257xx_i2c_ids[] = {
	{ "bq25703a" },
	{}
};
MODULE_DEVICE_TABLE(i2c, bq257xx_i2c_ids);

static const struct of_device_id bq257xx_of_match[] = {
	{ .compatible = "ti,bq25703a" },
	{}
};
MODULE_DEVICE_TABLE(of, bq257xx_of_match);

static struct i2c_driver bq257xx_driver = {
	.driver = {
		.name = "bq257xx",
		.of_match_table = bq257xx_of_match,
	},
	.probe = bq257xx_probe,
	.id_table = bq257xx_i2c_ids,
};
module_i2c_driver(bq257xx_driver);

MODULE_DESCRIPTION("bq257xx buck/boost/charger driver");
MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_LICENSE("GPL");
