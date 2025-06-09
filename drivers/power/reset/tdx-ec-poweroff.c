// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toradex Embedded Controller driver
 *
 * Copyright (C) 2025 Toradex
 *
 * Author: Emanuele Ghidoli <emanuele.ghidoli@toradex.com>
 */

#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define EC_CHIP_ID_REG                  0x00
#define EC_CHIP_ID_SMARC_IMX95          0x11
#define EC_CHIP_ID_SMARC_IMX8MP         0x12

#define EC_VERSION_REG_MAJOR            0x01
#define EC_VERSION_REG_MINOR            0x02
#define EC_ID_VERSION_LEN               3

#define EC_CMD_REG                      0xD0
#define EC_CMD_POWEROFF                 0x01
#define EC_CMD_RESET                    0x02

#define EC_REG_MAX                      0xD0

static const struct regmap_range volatile_ranges[] = {
	regmap_reg_range(EC_CMD_REG, EC_CMD_REG),
};

static const struct regmap_access_table volatile_table = {
	.yes_ranges	= volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(volatile_ranges),
};

static const struct regmap_range read_ranges[] = {
	regmap_reg_range(EC_CHIP_ID_REG, EC_VERSION_REG_MINOR),
};

static const struct regmap_access_table read_table = {
	.yes_ranges	= read_ranges,
	.n_yes_ranges	= ARRAY_SIZE(read_ranges),
};

static const struct regmap_config regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= EC_REG_MAX,
	.cache_type	= REGCACHE_RBTREE,
	.rd_table	= &read_table,
	.volatile_table = &volatile_table,
};

static int tdx_ec_cmd(struct regmap *regmap, u8 cmd)
{
	int err = regmap_write(regmap, EC_CMD_REG, cmd);

	if (err)
		dev_err(regmap_get_device(regmap), "Failed to send command 0x%02X: %d\n", cmd, err);

	return err;
}

static int tdx_ec_power_off(struct sys_off_data *data)
{
	struct regmap *regmap = data->cb_data;
	int err;

	err = tdx_ec_cmd(regmap, EC_CMD_POWEROFF);

	return err ? NOTIFY_BAD : NOTIFY_DONE;
}

static int tdx_ec_restart(struct sys_off_data *data)
{
	struct regmap *regmap = data->cb_data;
	int err;

	err = tdx_ec_cmd(regmap, EC_CMD_RESET);

	return err ? NOTIFY_BAD : NOTIFY_DONE;
}

static int tdx_ec_register_power_off_restart(struct device *dev, struct regmap *regmap)
{
	int err;

	err = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART,
					    SYS_OFF_PRIO_FIRMWARE,
					    tdx_ec_restart, regmap);
	if (err)
		return err;

	return devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_FIRMWARE,
					     tdx_ec_power_off, regmap);
}

static int tdx_ec_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 reg_val[EC_ID_VERSION_LEN];
	struct regmap *regmap;
	int err;

	regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	err = regmap_bulk_read(regmap, EC_CHIP_ID_REG, &reg_val, EC_ID_VERSION_LEN);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot read id and version registers\n");

	dev_info(dev, "Toradex Embedded Controller id %x - Firmware %u.%u\n",
		 reg_val[0], reg_val[1], reg_val[2]);

	err = tdx_ec_register_power_off_restart(dev, regmap);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot register system restart handler\n");

	return 0;
}

static const struct of_device_id __maybe_unused of_tdx_ec_match[] = {
	{ .compatible = "toradex,smarc-ec" },
	{}
};
MODULE_DEVICE_TABLE(of, of_tdx_ec_match);

static struct i2c_driver tdx_ec_driver = {
	.probe			= tdx_ec_probe,
	.driver			= {
		.name		= "toradex-smarc-ec",
		.of_match_table = of_tdx_ec_match,
	},
};
module_i2c_driver(tdx_ec_driver);

MODULE_AUTHOR("Emanuele Ghidoli <emanuele.ghidoli@toradex.com>");
MODULE_DESCRIPTION("Toradex SMARC Embedded Controller driver");
MODULE_LICENSE("GPL");
