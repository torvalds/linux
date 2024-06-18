// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ampere Altra Family SMPro core driver
 * Copyright (c) 2022, Ampere Computing LLC
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

/* Identification Registers */
#define MANUFACTURER_ID_REG     0x02
#define AMPERE_MANUFACTURER_ID  0xCD3A

#define CORE_CE_ERR_DATA        0x82
#define CORE_UE_ERR_DATA        0x85
#define MEM_CE_ERR_DATA         0x92
#define MEM_UE_ERR_DATA         0x95
#define PCIE_CE_ERR_DATA        0xC2
#define PCIE_UE_ERR_DATA        0xC5
#define OTHER_CE_ERR_DATA       0xD2
#define OTHER_UE_ERR_DATA       0xDA

static int smpro_core_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	ret = i2c_master_send(i2c, data, count);
	if (unlikely(ret != count))
		return (ret < 0) ? ret : -EIO;

	return 0;
}

static int smpro_core_read(void *context, const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct i2c_msg xfer[2];
	unsigned char buf[2];
	int ret;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;

	buf[0] = *(u8 *)reg;
	buf[1] = val_size;
	xfer[0].len = 2;
	xfer[0].buf = buf;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = val_size;
	xfer[1].buf = val;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (unlikely(ret != 2))
		return (ret < 0) ? ret : -EIO;

	return 0;
}

static const struct regmap_bus smpro_regmap_bus = {
	.read = smpro_core_read,
	.write = smpro_core_write,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static bool smpro_core_readable_noinc_reg(struct device *dev, unsigned int reg)
{
	return  (reg == CORE_CE_ERR_DATA || reg == CORE_UE_ERR_DATA ||
		 reg == MEM_CE_ERR_DATA || reg == MEM_UE_ERR_DATA ||
		 reg == PCIE_CE_ERR_DATA || reg == PCIE_UE_ERR_DATA ||
		 reg == OTHER_CE_ERR_DATA || reg == OTHER_UE_ERR_DATA);
}

static const struct regmap_config smpro_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.readable_noinc_reg = smpro_core_readable_noinc_reg,
};

static const struct mfd_cell smpro_devs[] = {
	MFD_CELL_NAME("smpro-hwmon"),
	MFD_CELL_NAME("smpro-errmon"),
	MFD_CELL_NAME("smpro-misc"),
};

static int smpro_core_probe(struct i2c_client *i2c)
{
	const struct regmap_config *config;
	struct regmap *regmap;
	unsigned int val;
	int ret;

	config = device_get_match_data(&i2c->dev);
	if (!config)
		return -EINVAL;

	regmap = devm_regmap_init(&i2c->dev, &smpro_regmap_bus, &i2c->dev, config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = regmap_read(regmap, MANUFACTURER_ID_REG, &val);
	if (ret)
		return ret;

	if (val != AMPERE_MANUFACTURER_ID)
		return -ENODEV;

	return devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				    smpro_devs, ARRAY_SIZE(smpro_devs), NULL, 0, NULL);
}

static const struct of_device_id smpro_core_of_match[] = {
	{ .compatible = "ampere,smpro", .data = &smpro_regmap_config },
	{}
};
MODULE_DEVICE_TABLE(of, smpro_core_of_match);

static struct i2c_driver smpro_core_driver = {
	.probe = smpro_core_probe,
	.driver = {
		.name = "smpro-core",
		.of_match_table = smpro_core_of_match,
	},
};
module_i2c_driver(smpro_core_driver);

MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("SMPRO CORE - I2C driver");
MODULE_LICENSE("GPL");
