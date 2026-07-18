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

static const struct regmap_range bq25792_writeable_reg_ranges[] = {
	regmap_reg_range(BQ25792_REG00_MIN_SYS_VOLTAGE,
			 BQ25792_REG18_NTC_CONTROL_1),
	regmap_reg_range(BQ25792_REG28_CHARGER_MASK_0,
			 BQ25792_REG30_ADC_FUNCTION_DISABLE_1),
};

static const struct regmap_access_table bq25792_writeable_regs = {
	.yes_ranges = bq25792_writeable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bq25792_writeable_reg_ranges),
};

static const struct regmap_range bq25792_volatile_reg_ranges[] = {
	regmap_reg_range(BQ25792_REG19_ICO_CURRENT_LIMIT,
			 BQ25792_REG27_FAULT_FLAG_1),
	regmap_reg_range(BQ25792_REG31_IBUS_ADC,
			 BQ25792_REG47_DPDM_DRIVER),
};

static const struct regmap_access_table bq25792_volatile_regs = {
	.yes_ranges = bq25792_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bq25792_volatile_reg_ranges),
};

static const struct regmap_config bq25792_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BQ25792_REG48_PART_INFORMATION,
	.cache_type = REGCACHE_MAPLE,
	.wr_table = &bq25792_writeable_regs,
	.volatile_table = &bq25792_volatile_regs,
};

static const struct mfd_cell cells[] = {
	MFD_CELL_NAME("bq257xx-regulator"),
	MFD_CELL_NAME("bq257xx-charger"),
};

static int bq257xx_probe(struct i2c_client *client)
{
	const struct regmap_config *rcfg;
	struct bq257xx_device *ddata;
	int ret;

	ddata = devm_kzalloc(&client->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->type = (uintptr_t)i2c_get_match_data(client);
	ddata->client = client;

	switch (ddata->type) {
	case BQ25703A:
		rcfg = &bq25703_regmap_config;
		break;
	case BQ25792:
		rcfg = &bq25792_regmap_config;
		break;
	default:
		return dev_err_probe(&client->dev, -ENODEV, "Unsupported device type\n");
	}

	ddata->regmap = devm_regmap_init_i2c(client, rcfg);
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
	{ "bq25703a", BQ25703A },
	{ "bq25792", BQ25792 },
	{}
};
MODULE_DEVICE_TABLE(i2c, bq257xx_i2c_ids);

static const struct of_device_id bq257xx_of_match[] = {
	{ .compatible = "ti,bq25703a", .data = (void *)BQ25703A },
	{ .compatible = "ti,bq25792", .data = (void *)BQ25792 },
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
