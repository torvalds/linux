// SPDX-License-Identifier: GPL-2.0
/*
 * MIPI Camera Control Interface (CCI) register access helpers.
 *
 * Copyright (C) 2023 Hans de Goede <hansg@kernel.org>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <asm/unaligned.h>

#include <media/v4l2-cci.h>

int cci_read(struct regmap *map, u32 reg, u64 *val, int *err)
{
	bool little_endian;
	unsigned int len;
	u8 buf[8];
	int ret;

	/*
	 * TODO: Fix smatch. Assign *val to 0 here in order to avoid
	 * failing a smatch check on caller when the caller proceeds to
	 * read *val without initialising it on caller's side. *val is set
	 * to a valid value whenever this function returns 0 but smatch
	 * can't figure that out currently.
	 */
	*val = 0;

	if (err && *err)
		return *err;

	little_endian = reg & CCI_REG_LE;
	len = CCI_REG_WIDTH_BYTES(reg);
	reg = CCI_REG_ADDR(reg);

	ret = regmap_bulk_read(map, reg, buf, len);
	if (ret) {
		dev_err(regmap_get_device(map), "Error reading reg 0x%04x: %d\n",
			reg, ret);
		goto out;
	}

	switch (len) {
	case 1:
		*val = buf[0];
		break;
	case 2:
		if (little_endian)
			*val = get_unaligned_le16(buf);
		else
			*val = get_unaligned_be16(buf);
		break;
	case 3:
		if (little_endian)
			*val = get_unaligned_le24(buf);
		else
			*val = get_unaligned_be24(buf);
		break;
	case 4:
		if (little_endian)
			*val = get_unaligned_le32(buf);
		else
			*val = get_unaligned_be32(buf);
		break;
	case 8:
		if (little_endian)
			*val = get_unaligned_le64(buf);
		else
			*val = get_unaligned_be64(buf);
		break;
	default:
		dev_err(regmap_get_device(map), "Error invalid reg-width %u for reg 0x%04x\n",
			len, reg);
		ret = -EINVAL;
		break;
	}

out:
	if (ret && err)
		*err = ret;

	return ret;
}
EXPORT_SYMBOL_GPL(cci_read);

int cci_write(struct regmap *map, u32 reg, u64 val, int *err)
{
	bool little_endian;
	unsigned int len;
	u8 buf[8];
	int ret;

	if (err && *err)
		return *err;

	little_endian = reg & CCI_REG_LE;
	len = CCI_REG_WIDTH_BYTES(reg);
	reg = CCI_REG_ADDR(reg);

	switch (len) {
	case 1:
		buf[0] = val;
		break;
	case 2:
		if (little_endian)
			put_unaligned_le16(val, buf);
		else
			put_unaligned_be16(val, buf);
		break;
	case 3:
		if (little_endian)
			put_unaligned_le24(val, buf);
		else
			put_unaligned_be24(val, buf);
		break;
	case 4:
		if (little_endian)
			put_unaligned_le32(val, buf);
		else
			put_unaligned_be32(val, buf);
		break;
	case 8:
		if (little_endian)
			put_unaligned_le64(val, buf);
		else
			put_unaligned_be64(val, buf);
		break;
	default:
		dev_err(regmap_get_device(map), "Error invalid reg-width %u for reg 0x%04x\n",
			len, reg);
		ret = -EINVAL;
		goto out;
	}

	ret = regmap_bulk_write(map, reg, buf, len);
	if (ret)
		dev_err(regmap_get_device(map), "Error writing reg 0x%04x: %d\n",
			reg, ret);

out:
	if (ret && err)
		*err = ret;

	return ret;
}
EXPORT_SYMBOL_GPL(cci_write);

int cci_update_bits(struct regmap *map, u32 reg, u64 mask, u64 val, int *err)
{
	u64 readval;
	int ret;

	ret = cci_read(map, reg, &readval, err);
	if (ret)
		return ret;

	val = (readval & ~mask) | (val & mask);

	return cci_write(map, reg, val, err);
}
EXPORT_SYMBOL_GPL(cci_update_bits);

int cci_multi_reg_write(struct regmap *map, const struct cci_reg_sequence *regs,
			unsigned int num_regs, int *err)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_regs; i++) {
		ret = cci_write(map, regs[i].reg, regs[i].val, err);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cci_multi_reg_write);

#if IS_ENABLED(CONFIG_V4L2_CCI_I2C)
struct regmap *devm_cci_regmap_init_i2c(struct i2c_client *client,
					int reg_addr_bits)
{
	struct regmap_config config = {
		.reg_bits = reg_addr_bits,
		.val_bits = 8,
		.reg_format_endian = REGMAP_ENDIAN_BIG,
		.disable_locking = true,
	};

	return devm_regmap_init_i2c(client, &config);
}
EXPORT_SYMBOL_GPL(devm_cci_regmap_init_i2c);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("MIPI Camera Control Interface (CCI) support");
