// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Versal SysMon I2C driver
 *
 * Copyright (C) 2023 - 2026, Advanced Micro Devices, Inc.
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include "versal-sysmon.h"

#define SYSMON_I2C_INSTR_READ	BIT(2)
#define SYSMON_I2C_INSTR_WRITE	BIT(3)

/*
 * I2C command frame layout (8 bytes):
 *   [0..3] data payload (little-endian u32)
 *   [4..5] register offset >> 2 (little-endian u16)
 *   [6]    instruction (read/write)
 *   [7]    reserved
 */
#define SYSMON_I2C_DATA_OFS	0
#define SYSMON_I2C_REG_OFS	4
#define SYSMON_I2C_INSTR_OFS	6

static int sysmon_i2c_reg_read(void *context, unsigned int reg,
			       unsigned int *val)
{
	struct i2c_client *client = context;
	u8 write_buf[8] = { };
	u8 read_buf[4];
	int ret;

	put_unaligned_le16(reg >> 2, &write_buf[SYSMON_I2C_REG_OFS]);
	write_buf[SYSMON_I2C_INSTR_OFS] = SYSMON_I2C_INSTR_READ;

	ret = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(write_buf))
		return -EIO;

	ret = i2c_master_recv(client, read_buf, sizeof(read_buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(read_buf))
		return -EIO;

	*val = get_unaligned_le32(read_buf);

	return 0;
}

static int sysmon_i2c_reg_write(void *context, unsigned int reg,
				unsigned int val)
{
	struct i2c_client *client = context;
	u8 write_buf[8] = { };
	int ret;

	put_unaligned_le32(val, &write_buf[SYSMON_I2C_DATA_OFS]);
	put_unaligned_le16(reg >> 2, &write_buf[SYSMON_I2C_REG_OFS]);
	write_buf[SYSMON_I2C_INSTR_OFS] = SYSMON_I2C_INSTR_WRITE;

	ret = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(write_buf))
		return -EIO;

	return 0;
}

/*
 * Almost all registers are volatile (live ADC readings, interrupt
 * status). The rest are not accessed often enough to benefit from
 * caching.
 */
static const struct regmap_config sysmon_i2c_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = SYSMON_REG_STRIDE,
	.max_register = SYSMON_MAX_REG,
	.reg_read = sysmon_i2c_reg_read,
	.reg_write = sysmon_i2c_reg_write,
};

static int sysmon_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *regmap;

	regmap = devm_regmap_init(dev, NULL, client, &sysmon_i2c_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* I2C has no IRQ connection; events are not supported */
	return devm_versal_sysmon_core_probe(dev, regmap);
}

static const struct of_device_id sysmon_i2c_of_match_table[] = {
	{ .compatible = "xlnx,versal-sysmon" },
	{ }
};
MODULE_DEVICE_TABLE(of, sysmon_i2c_of_match_table);

static const struct i2c_device_id sysmon_i2c_id_table[] = {
	{ .name = "versal-sysmon" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sysmon_i2c_id_table);

static struct i2c_driver sysmon_i2c_driver = {
	.probe = sysmon_i2c_probe,
	.driver = {
		.name = "versal-sysmon-i2c",
		.of_match_table = sysmon_i2c_of_match_table,
	},
	.id_table = sysmon_i2c_id_table,
};
module_i2c_driver(sysmon_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Versal SysMon I2C Driver");
MODULE_IMPORT_NS("VERSAL_SYSMON");
MODULE_AUTHOR("Conall O'Griofa <conall.ogriofa@amd.com>");
MODULE_AUTHOR("Salih Erim <salih.erim@amd.com>");
