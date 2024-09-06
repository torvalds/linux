// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Jedec 5118 compliant temperature sensors
 *
 * Derived from https://github.com/Steve-Tech/SPD5118-DKMS
 * Originally from T/2 driver at https://t2sde.org/packages/linux
 *	Copyright (c) 2023 René Rebe, ExactCODE GmbH; Germany.
 *
 * Copyright (c) 2024 Guenter Roeck
 *
 * Inspired by ee1004.c and jc42.c.
 *
 * SPD5118 compliant temperature sensors are typically used on DDR5
 * memory modules.
 */

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/units.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = {
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, I2C_CLIENT_END };

/* SPD5118 registers. */
#define SPD5118_REG_TYPE		0x00	/* MR0:MR1 */
#define SPD5118_REG_REVISION		0x02	/* MR2 */
#define SPD5118_REG_VENDOR		0x03	/* MR3:MR4 */
#define SPD5118_REG_CAPABILITY		0x05	/* MR5 */
#define SPD5118_REG_I2C_LEGACY_MODE	0x0B	/* MR11 */
#define SPD5118_REG_TEMP_CLR		0x13	/* MR19 */
#define SPD5118_REG_ERROR_CLR		0x14	/* MR20 */
#define SPD5118_REG_TEMP_CONFIG		0x1A	/* MR26 */
#define SPD5118_REG_TEMP_MAX		0x1c	/* MR28:MR29 */
#define SPD5118_REG_TEMP_MIN		0x1e	/* MR30:MR31 */
#define SPD5118_REG_TEMP_CRIT		0x20	/* MR32:MR33 */
#define SPD5118_REG_TEMP_LCRIT		0x22	/* MR34:MR35 */
#define SPD5118_REG_TEMP		0x31	/* MR49:MR50 */
#define SPD5118_REG_TEMP_STATUS		0x33	/* MR51 */

#define SPD5118_TEMP_STATUS_HIGH	BIT(0)
#define SPD5118_TEMP_STATUS_LOW		BIT(1)
#define SPD5118_TEMP_STATUS_CRIT	BIT(2)
#define SPD5118_TEMP_STATUS_LCRIT	BIT(3)

#define SPD5118_CAP_TS_SUPPORT		BIT(1)	/* temperature sensor support */

#define SPD5118_TS_DISABLE		BIT(0)	/* temperature sensor disable */

#define SPD5118_LEGACY_MODE_ADDR	BIT(3)
#define SPD5118_LEGACY_PAGE_MASK	GENMASK(2, 0)
#define SPD5118_LEGACY_MODE_MASK	(SPD5118_LEGACY_MODE_ADDR | SPD5118_LEGACY_PAGE_MASK)

#define SPD5118_NUM_PAGES		8
#define SPD5118_PAGE_SIZE		128
#define SPD5118_PAGE_SHIFT		7
#define SPD5118_PAGE_MASK		GENMASK(6, 0)
#define SPD5118_EEPROM_BASE		0x80
#define SPD5118_EEPROM_SIZE		(SPD5118_PAGE_SIZE * SPD5118_NUM_PAGES)

/* Temperature unit in millicelsius */
#define SPD5118_TEMP_UNIT		(MILLIDEGREE_PER_DEGREE / 4)
/* Representable temperature range in millicelsius */
#define SPD5118_TEMP_RANGE_MIN		-256000
#define SPD5118_TEMP_RANGE_MAX		255750

struct spd5118_data {
	struct regmap *regmap;
	struct mutex nvmem_lock;
};

/* hwmon */

static int spd5118_temp_from_reg(u16 reg)
{
	int temp = sign_extend32(reg >> 2, 10);

	return temp * SPD5118_TEMP_UNIT;
}

static u16 spd5118_temp_to_reg(long temp)
{
	temp = clamp_val(temp, SPD5118_TEMP_RANGE_MIN, SPD5118_TEMP_RANGE_MAX);
	return (DIV_ROUND_CLOSEST(temp, SPD5118_TEMP_UNIT) & 0x7ff) << 2;
}

static int spd5118_read_temp(struct regmap *regmap, u32 attr, long *val)
{
	int reg, err;
	u8 regval[2];
	u16 temp;

	switch (attr) {
	case hwmon_temp_input:
		reg = SPD5118_REG_TEMP;
		break;
	case hwmon_temp_max:
		reg = SPD5118_REG_TEMP_MAX;
		break;
	case hwmon_temp_min:
		reg = SPD5118_REG_TEMP_MIN;
		break;
	case hwmon_temp_crit:
		reg = SPD5118_REG_TEMP_CRIT;
		break;
	case hwmon_temp_lcrit:
		reg = SPD5118_REG_TEMP_LCRIT;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = regmap_bulk_read(regmap, reg, regval, 2);
	if (err)
		return err;

	temp = (regval[1] << 8) | regval[0];

	*val = spd5118_temp_from_reg(temp);
	return 0;
}

static int spd5118_read_alarm(struct regmap *regmap, u32 attr, long *val)
{
	unsigned int mask, regval;
	int err;

	switch (attr) {
	case hwmon_temp_max_alarm:
		mask = SPD5118_TEMP_STATUS_HIGH;
		break;
	case hwmon_temp_min_alarm:
		mask = SPD5118_TEMP_STATUS_LOW;
		break;
	case hwmon_temp_crit_alarm:
		mask = SPD5118_TEMP_STATUS_CRIT;
		break;
	case hwmon_temp_lcrit_alarm:
		mask = SPD5118_TEMP_STATUS_LCRIT;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = regmap_read(regmap, SPD5118_REG_TEMP_STATUS, &regval);
	if (err < 0)
		return err;
	*val = !!(regval & mask);
	if (*val)
		return regmap_write(regmap, SPD5118_REG_TEMP_CLR, mask);
	return 0;
}

static int spd5118_read_enable(struct regmap *regmap, long *val)
{
	u32 regval;
	int err;

	err = regmap_read(regmap, SPD5118_REG_TEMP_CONFIG, &regval);
	if (err < 0)
		return err;
	*val = !(regval & SPD5118_TS_DISABLE);
	return 0;
}

static int spd5118_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_max:
	case hwmon_temp_min:
	case hwmon_temp_crit:
	case hwmon_temp_lcrit:
		return spd5118_read_temp(regmap, attr, val);
	case hwmon_temp_max_alarm:
	case hwmon_temp_min_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_lcrit_alarm:
		return spd5118_read_alarm(regmap, attr, val);
	case hwmon_temp_enable:
		return spd5118_read_enable(regmap, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int spd5118_write_temp(struct regmap *regmap, u32 attr, long val)
{
	u8 regval[2];
	u16 temp;
	int reg;

	switch (attr) {
	case hwmon_temp_max:
		reg = SPD5118_REG_TEMP_MAX;
		break;
	case hwmon_temp_min:
		reg = SPD5118_REG_TEMP_MIN;
		break;
	case hwmon_temp_crit:
		reg = SPD5118_REG_TEMP_CRIT;
		break;
	case hwmon_temp_lcrit:
		reg = SPD5118_REG_TEMP_LCRIT;
		break;
	default:
		return -EOPNOTSUPP;
	}

	temp = spd5118_temp_to_reg(val);
	regval[0] = temp & 0xff;
	regval[1] = temp >> 8;

	return regmap_bulk_write(regmap, reg, regval, 2);
}

static int spd5118_write_enable(struct regmap *regmap, long val)
{
	if (val && val != 1)
		return -EINVAL;

	return regmap_update_bits(regmap, SPD5118_REG_TEMP_CONFIG,
				  SPD5118_TS_DISABLE,
				  val ? 0 : SPD5118_TS_DISABLE);
}

static int spd5118_temp_write(struct regmap *regmap, u32 attr, long val)
{
	switch (attr) {
	case hwmon_temp_max:
	case hwmon_temp_min:
	case hwmon_temp_crit:
	case hwmon_temp_lcrit:
		return spd5118_write_temp(regmap, attr, val);
	case hwmon_temp_enable:
		return spd5118_write_enable(regmap, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int spd5118_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return spd5118_temp_write(regmap, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t spd5118_is_visible(const void *_data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_lcrit:
	case hwmon_temp_crit:
	case hwmon_temp_enable:
		return 0644;
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_lcrit_alarm:
		return 0444;
	default:
		return 0;
	}
}

static inline bool spd5118_parity8(u8 w)
{
	w ^= w >> 4;
	return (0x6996 >> (w & 0xf)) & 1;
}

/*
 * Bank and vendor id are 8-bit fields with seven data bits and odd parity.
 * Vendor IDs 0 and 0x7f are invalid.
 * See Jedec standard JEP106BJ for details and a list of assigned vendor IDs.
 */
static bool spd5118_vendor_valid(u8 bank, u8 id)
{
	if (!spd5118_parity8(bank) || !spd5118_parity8(id))
		return false;

	id &= 0x7f;
	return id && id != 0x7f;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int spd5118_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int regval;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	regval = i2c_smbus_read_word_swapped(client, SPD5118_REG_TYPE);
	if (regval != 0x5118)
		return -ENODEV;

	regval = i2c_smbus_read_word_data(client, SPD5118_REG_VENDOR);
	if (regval < 0 || !spd5118_vendor_valid(regval & 0xff, regval >> 8))
		return -ENODEV;

	regval = i2c_smbus_read_byte_data(client, SPD5118_REG_CAPABILITY);
	if (regval < 0)
		return -ENODEV;
	if (!(regval & SPD5118_CAP_TS_SUPPORT) || (regval & 0xfc))
		return -ENODEV;

	regval = i2c_smbus_read_byte_data(client, SPD5118_REG_TEMP_CLR);
	if (regval)
		return -ENODEV;
	regval = i2c_smbus_read_byte_data(client, SPD5118_REG_ERROR_CLR);
	if (regval)
		return -ENODEV;

	regval = i2c_smbus_read_byte_data(client, SPD5118_REG_REVISION);
	if (regval < 0 || (regval & 0xc1))
		return -ENODEV;

	regval = i2c_smbus_read_byte_data(client, SPD5118_REG_TEMP_CONFIG);
	if (regval < 0)
		return -ENODEV;
	if (regval & ~SPD5118_TS_DISABLE)
		return -ENODEV;

	strscpy(info->type, "spd5118", I2C_NAME_SIZE);
	return 0;
}

static const struct hwmon_channel_info *spd5118_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT |
			   HWMON_T_LCRIT | HWMON_T_LCRIT_ALARM |
			   HWMON_T_MIN | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_CRIT | HWMON_T_CRIT_ALARM |
			   HWMON_T_ENABLE),
	NULL
};

static const struct hwmon_ops spd5118_hwmon_ops = {
	.is_visible = spd5118_is_visible,
	.read = spd5118_read,
	.write = spd5118_write,
};

static const struct hwmon_chip_info spd5118_chip_info = {
	.ops = &spd5118_hwmon_ops,
	.info = spd5118_info,
};

/* nvmem */

static ssize_t spd5118_nvmem_read_page(struct regmap *regmap, char *buf,
				       unsigned int offset, size_t count)
{
	int addr = (offset >> SPD5118_PAGE_SHIFT) * 0x100 + SPD5118_EEPROM_BASE;
	int err;

	offset &= SPD5118_PAGE_MASK;

	/* Can't cross page boundaries */
	if (offset + count > SPD5118_PAGE_SIZE)
		count = SPD5118_PAGE_SIZE - offset;

	err = regmap_bulk_read(regmap, addr + offset, buf, count);
	if (err)
		return err;

	return count;
}

static int spd5118_nvmem_read(void *priv, unsigned int off, void *val, size_t count)
{
	struct spd5118_data *data = priv;
	char *buf = val;
	int ret;

	if (unlikely(!count))
		return count;

	if (off + count > SPD5118_EEPROM_SIZE)
		return -EINVAL;

	mutex_lock(&data->nvmem_lock);

	while (count) {
		ret = spd5118_nvmem_read_page(data->regmap, buf, off, count);
		if (ret < 0) {
			mutex_unlock(&data->nvmem_lock);
			return ret;
		}
		buf += ret;
		off += ret;
		count -= ret;
	}
	mutex_unlock(&data->nvmem_lock);
	return 0;
}

static int spd5118_nvmem_init(struct device *dev, struct spd5118_data *data)
{
	struct nvmem_config nvmem_config = {
		.type = NVMEM_TYPE_EEPROM,
		.name = dev_name(dev),
		.id = NVMEM_DEVID_NONE,
		.dev = dev,
		.base_dev = dev,
		.read_only = true,
		.root_only = false,
		.owner = THIS_MODULE,
		.compat = true,
		.reg_read = spd5118_nvmem_read,
		.priv = data,
		.stride = 1,
		.word_size = 1,
		.size = SPD5118_EEPROM_SIZE,
	};
	struct nvmem_device *nvmem;

	nvmem = devm_nvmem_register(dev, &nvmem_config);
	return PTR_ERR_OR_ZERO(nvmem);
}

/* regmap */

static bool spd5118_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPD5118_REG_I2C_LEGACY_MODE:
	case SPD5118_REG_TEMP_CLR:
	case SPD5118_REG_TEMP_CONFIG:
	case SPD5118_REG_TEMP_MAX:
	case SPD5118_REG_TEMP_MAX + 1:
	case SPD5118_REG_TEMP_MIN:
	case SPD5118_REG_TEMP_MIN + 1:
	case SPD5118_REG_TEMP_CRIT:
	case SPD5118_REG_TEMP_CRIT + 1:
	case SPD5118_REG_TEMP_LCRIT:
	case SPD5118_REG_TEMP_LCRIT + 1:
		return true;
	default:
		return false;
	}
}

static bool spd5118_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPD5118_REG_TEMP_CLR:
	case SPD5118_REG_ERROR_CLR:
	case SPD5118_REG_TEMP:
	case SPD5118_REG_TEMP + 1:
	case SPD5118_REG_TEMP_STATUS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_range_cfg spd5118_regmap_range_cfg[] = {
	{
	.selector_reg   = SPD5118_REG_I2C_LEGACY_MODE,
	.selector_mask  = SPD5118_LEGACY_PAGE_MASK,
	.selector_shift = 0,
	.window_start   = 0,
	.window_len     = 0x100,
	.range_min      = 0,
	.range_max      = 0x7ff,
	},
};

static const struct regmap_config spd5118_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x7ff,
	.writeable_reg = spd5118_writeable_reg,
	.volatile_reg = spd5118_volatile_reg,
	.cache_type = REGCACHE_MAPLE,

	.ranges = spd5118_regmap_range_cfg,
	.num_ranges = ARRAY_SIZE(spd5118_regmap_range_cfg),
};

static int spd5118_init(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int err, regval, mode;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	regval = i2c_smbus_read_word_swapped(client, SPD5118_REG_TYPE);
	if (regval < 0 || (regval && regval != 0x5118))
		return -ENODEV;

	/*
	 * If the device type registers return 0, it is possible that the chip
	 * has a non-zero page selected and takes the specification literally,
	 * i.e. disables access to volatile registers besides the page register
	 * if the page is not 0. Try to identify such chips.
	 */
	if (!regval) {
		/* Vendor ID registers must also be 0 */
		regval = i2c_smbus_read_word_data(client, SPD5118_REG_VENDOR);
		if (regval)
			return -ENODEV;

		/* The selected page in MR11 must not be 0 */
		mode = i2c_smbus_read_byte_data(client, SPD5118_REG_I2C_LEGACY_MODE);
		if (mode < 0 || (mode & ~SPD5118_LEGACY_MODE_MASK) ||
		    !(mode & SPD5118_LEGACY_PAGE_MASK))
			return -ENODEV;

		err = i2c_smbus_write_byte_data(client, SPD5118_REG_I2C_LEGACY_MODE,
						mode & SPD5118_LEGACY_MODE_ADDR);
		if (err)
			return -ENODEV;

		/*
		 * If the device type registers are still bad after selecting
		 * page 0, this is not a SPD5118 device. Restore original
		 * legacy mode register value and abort.
		 */
		regval = i2c_smbus_read_word_swapped(client, SPD5118_REG_TYPE);
		if (regval != 0x5118) {
			i2c_smbus_write_byte_data(client, SPD5118_REG_I2C_LEGACY_MODE, mode);
			return -ENODEV;
		}
	}

	/* We are reasonably sure that this is really a SPD5118 hub controller */
	return 0;
}

static int spd5118_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	unsigned int regval, revision, vendor, bank;
	struct spd5118_data *data;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int err;

	err = spd5118_init(client);
	if (err)
		return err;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &spd5118_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "regmap init failed\n");

	err = regmap_read(regmap, SPD5118_REG_CAPABILITY, &regval);
	if (err)
		return err;
	if (!(regval & SPD5118_CAP_TS_SUPPORT))
		return -ENODEV;

	err = regmap_read(regmap, SPD5118_REG_REVISION, &revision);
	if (err)
		return err;

	err = regmap_read(regmap, SPD5118_REG_VENDOR, &bank);
	if (err)
		return err;
	err = regmap_read(regmap, SPD5118_REG_VENDOR + 1, &vendor);
	if (err)
		return err;
	if (!spd5118_vendor_valid(bank, vendor))
		return -ENODEV;

	data->regmap = regmap;
	mutex_init(&data->nvmem_lock);
	dev_set_drvdata(dev, data);

	err = spd5118_nvmem_init(dev, data);
	/* Ignore if NVMEM support is disabled */
	if (err && err != -EOPNOTSUPP) {
		dev_err_probe(dev, err, "failed to register nvmem\n");
		return err;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "spd5118",
							 regmap, &spd5118_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	/*
	 * From JESD300-5B
	 *   MR2 bits [5:4]: Major revision, 1..4
	 *   MR2 bits [3:1]: Minor revision, 0..8? Probably a typo, assume 1..8
	 */
	dev_info(dev, "DDR5 temperature sensor: vendor 0x%02x:0x%02x revision %d.%d\n",
		 bank & 0x7f, vendor, ((revision >> 4) & 0x03) + 1, ((revision >> 1) & 0x07) + 1);

	return 0;
}

static int spd5118_suspend(struct device *dev)
{
	struct spd5118_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 regval;
	int err;

	/*
	 * Make sure the configuration register in the regmap cache is current
	 * before bypassing it.
	 */
	err = regmap_read(regmap, SPD5118_REG_TEMP_CONFIG, &regval);
	if (err < 0)
		return err;

	regcache_cache_bypass(regmap, true);
	regmap_update_bits(regmap, SPD5118_REG_TEMP_CONFIG, SPD5118_TS_DISABLE,
			   SPD5118_TS_DISABLE);
	regcache_cache_bypass(regmap, false);

	regcache_cache_only(regmap, true);
	regcache_mark_dirty(regmap);

	return 0;
}

static int spd5118_resume(struct device *dev)
{
	struct spd5118_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;

	regcache_cache_only(regmap, false);
	return regcache_sync(regmap);
}

static DEFINE_SIMPLE_DEV_PM_OPS(spd5118_pm_ops, spd5118_suspend, spd5118_resume);

static const struct i2c_device_id spd5118_id[] = {
	{ "spd5118", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, spd5118_id);

static const struct of_device_id spd5118_of_ids[] = {
	{ .compatible = "jedec,spd5118", },
	{ }
};
MODULE_DEVICE_TABLE(of, spd5118_of_ids);

static struct i2c_driver spd5118_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "spd5118",
		.of_match_table = spd5118_of_ids,
		.pm = pm_sleep_ptr(&spd5118_pm_ops),
	},
	.probe		= spd5118_probe,
	.id_table	= spd5118_id,
	.detect		= IS_ENABLED(CONFIG_SENSORS_SPD5118_DETECT) ? spd5118_detect : NULL,
	.address_list	= IS_ENABLED(CONFIG_SENSORS_SPD5118_DETECT) ? normal_i2c : NULL,
};

module_i2c_driver(spd5118_driver);

MODULE_AUTHOR("René Rebe <rene@exactcode.de>");
MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("SPD 5118 driver");
MODULE_LICENSE("GPL");
