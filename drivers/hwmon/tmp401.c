// SPDX-License-Identifier: GPL-2.0-or-later
/* tmp401.c
 *
 * Copyright (C) 2007,2008 Hans de Goede <hdegoede@redhat.com>
 * Preliminary tmp411 support by:
 * Gabriel Konat, Sander Leget, Wouter Willems
 * Copyright (C) 2009 Andre Prendel <andre.prendel@gmx.de>
 *
 * Cleanup and support for TMP431 and TMP432 by Guenter Roeck
 * Copyright (c) 2013 Guenter Roeck <linux@roeck-us.net>
 */

/*
 * Driver for the Texas Instruments TMP401 SMBUS temperature sensor IC.
 *
 * Note this IC is in some aspect similar to the LM90, but it has quite a
 * few differences too, for example the local temp has a higher resolution
 * and thus has 16 bits registers for its value and limit instead of 8 bits.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4c, 0x4d,
	0x4e, 0x4f, I2C_CLIENT_END };

enum chips { tmp401, tmp411, tmp431, tmp432, tmp435 };

/*
 * The TMP401 registers, note some registers have different addresses for
 * reading and writing
 */
#define TMP401_STATUS				0x02
#define TMP401_CONFIG				0x03
#define TMP401_CONVERSION_RATE			0x04
#define TMP401_TEMP_CRIT_HYST			0x21
#define TMP401_MANUFACTURER_ID_REG		0xFE
#define TMP401_DEVICE_ID_REG			0xFF

static const u8 TMP401_TEMP_MSB[7][3] = {
	{ 0x00, 0x01, 0x23 },	/* temp */
	{ 0x06, 0x08, 0x16 },	/* low limit */
	{ 0x05, 0x07, 0x15 },	/* high limit */
	{ 0x20, 0x19, 0x1a },	/* therm (crit) limit */
	{ 0x30, 0x34, 0x00 },	/* lowest */
	{ 0x32, 0xf6, 0x00 },	/* highest */
};

/* [0] = fault, [1] = low, [2] = high, [3] = therm/crit */
static const u8 TMP432_STATUS_REG[] = {
	0x1b, 0x36, 0x35, 0x37 };

/* Flags */
#define TMP401_CONFIG_RANGE			BIT(2)
#define TMP401_CONFIG_SHUTDOWN			BIT(6)
#define TMP401_STATUS_LOCAL_CRIT		BIT(0)
#define TMP401_STATUS_REMOTE_CRIT		BIT(1)
#define TMP401_STATUS_REMOTE_OPEN		BIT(2)
#define TMP401_STATUS_REMOTE_LOW		BIT(3)
#define TMP401_STATUS_REMOTE_HIGH		BIT(4)
#define TMP401_STATUS_LOCAL_LOW			BIT(5)
#define TMP401_STATUS_LOCAL_HIGH		BIT(6)

/* On TMP432, each status has its own register */
#define TMP432_STATUS_LOCAL			BIT(0)
#define TMP432_STATUS_REMOTE1			BIT(1)
#define TMP432_STATUS_REMOTE2			BIT(2)

/* Manufacturer / Device ID's */
#define TMP401_MANUFACTURER_ID			0x55
#define TMP401_DEVICE_ID			0x11
#define TMP411A_DEVICE_ID			0x12
#define TMP411B_DEVICE_ID			0x13
#define TMP411C_DEVICE_ID			0x10
#define TMP431_DEVICE_ID			0x31
#define TMP432_DEVICE_ID			0x32
#define TMP435_DEVICE_ID			0x35

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id tmp401_id[] = {
	{ "tmp401", tmp401 },
	{ "tmp411", tmp411 },
	{ "tmp431", tmp431 },
	{ "tmp432", tmp432 },
	{ "tmp435", tmp435 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp401_id);

/*
 * Client data (each client gets its own)
 */

struct tmp401_data {
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex update_lock;
	enum chips kind;

	bool extended_range;

	/* hwmon API configuration data */
	u32 chip_channel_config[4];
	struct hwmon_channel_info chip_info;
	u32 temp_channel_config[4];
	struct hwmon_channel_info temp_info;
	const struct hwmon_channel_info *info[3];
	struct hwmon_chip_info chip;
};

/* regmap */

static bool tmp401_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0:			/* local temp msb */
	case 1:			/* remote temp msb */
	case 2:			/* status */
	case 0x10:		/* remote temp lsb */
	case 0x15:		/* local temp lsb */
	case 0x1b:		/* status (tmp432) */
	case 0x23 ... 0x24:	/* remote temp 2 msb / lsb */
	case 0x30 ... 0x37:	/* lowest/highest temp; status (tmp432) */
		return true;
	default:
		return false;
	}
}

static int tmp401_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct tmp401_data *data = context;
	struct i2c_client *client = data->client;
	int regval;

	switch (reg) {
	case 0:			/* local temp msb */
	case 1:			/* remote temp msb */
	case 5:			/* local temp high limit msb */
	case 6:			/* local temp low limit msb */
	case 7:			/* remote temp ligh limit msb */
	case 8:			/* remote temp low limit msb */
	case 0x15:		/* remote temp 2 high limit msb */
	case 0x16:		/* remote temp 2 low limit msb */
	case 0x23:		/* remote temp 2 msb */
	case 0x30:		/* local temp minimum, tmp411 */
	case 0x32:		/* local temp maximum, tmp411 */
	case 0x34:		/* remote temp minimum, tmp411 */
	case 0xf6:		/* remote temp maximum, tmp411 (really 0x36) */
		/* work around register overlap between TMP411 and TMP432 */
		if (reg == 0xf6)
			reg = 0x36;
		regval = i2c_smbus_read_word_swapped(client, reg);
		if (regval < 0)
			return regval;
		*val = regval;
		break;
	case 0x19:		/* critical limits, 8-bit registers */
	case 0x1a:
	case 0x20:
		regval = i2c_smbus_read_byte_data(client, reg);
		if (regval < 0)
			return regval;
		*val = regval << 8;
		break;
	case 0x1b:
	case 0x35 ... 0x37:
		if (data->kind == tmp432) {
			regval = i2c_smbus_read_byte_data(client, reg);
			if (regval < 0)
				return regval;
			*val = regval;
			break;
		}
		/* simulate TMP432 status registers */
		regval = i2c_smbus_read_byte_data(client, TMP401_STATUS);
		if (regval < 0)
			return regval;
		*val = 0;
		switch (reg) {
		case 0x1b:	/* open / fault */
			if (regval & TMP401_STATUS_REMOTE_OPEN)
				*val |= BIT(1);
			break;
		case 0x35:	/* high limit */
			if (regval & TMP401_STATUS_LOCAL_HIGH)
				*val |= BIT(0);
			if (regval & TMP401_STATUS_REMOTE_HIGH)
				*val |= BIT(1);
			break;
		case 0x36:	/* low limit */
			if (regval & TMP401_STATUS_LOCAL_LOW)
				*val |= BIT(0);
			if (regval & TMP401_STATUS_REMOTE_LOW)
				*val |= BIT(1);
			break;
		case 0x37:	/* therm / crit limit */
			if (regval & TMP401_STATUS_LOCAL_CRIT)
				*val |= BIT(0);
			if (regval & TMP401_STATUS_REMOTE_CRIT)
				*val |= BIT(1);
			break;
		}
		break;
	default:
		regval = i2c_smbus_read_byte_data(client, reg);
		if (regval < 0)
			return regval;
		*val = regval;
		break;
	}
	return 0;
}

static int tmp401_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct tmp401_data *data = context;
	struct i2c_client *client = data->client;

	switch (reg) {
	case 0x05:		/* local temp high limit msb */
	case 0x06:		/* local temp low limit msb */
	case 0x07:		/* remote temp ligh limit msb */
	case 0x08:		/* remote temp low limit msb */
		reg += 6;	/* adjust for register write address */
		fallthrough;
	case 0x15:		/* remote temp 2 high limit msb */
	case 0x16:		/* remote temp 2 low limit msb */
		return i2c_smbus_write_word_swapped(client, reg, val);
	case 0x19:		/* critical limits, 8-bit registers */
	case 0x1a:
	case 0x20:
		return i2c_smbus_write_byte_data(client, reg, val >> 8);
	case TMP401_CONVERSION_RATE:
	case TMP401_CONFIG:
		reg += 6;	/* adjust for register write address */
		fallthrough;
	default:
		return i2c_smbus_write_byte_data(client, reg, val);
	}
}

static const struct regmap_config tmp401_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tmp401_regmap_is_volatile,
	.reg_read = tmp401_reg_read,
	.reg_write = tmp401_reg_write,
};

/* temperature conversion */

static int tmp401_register_to_temp(u16 reg, bool extended)
{
	int temp = reg;

	if (extended)
		temp -= 64 * 256;

	return DIV_ROUND_CLOSEST(temp * 125, 32);
}

static u16 tmp401_temp_to_register(long temp, bool extended, int zbits)
{
	if (extended) {
		temp = clamp_val(temp, -64000, 191000);
		temp += 64000;
	} else {
		temp = clamp_val(temp, 0, 127000);
	}

	return DIV_ROUND_CLOSEST(temp * (1 << (8 - zbits)), 1000) << zbits;
}

/* hwmon API functions */

static const u8 tmp401_temp_reg_index[] = {
	[hwmon_temp_input] = 0,
	[hwmon_temp_min] = 1,
	[hwmon_temp_max] = 2,
	[hwmon_temp_crit] = 3,
	[hwmon_temp_lowest] = 4,
	[hwmon_temp_highest] = 5,
};

static const u8 tmp401_status_reg_index[] = {
	[hwmon_temp_fault] = 0,
	[hwmon_temp_min_alarm] = 1,
	[hwmon_temp_max_alarm] = 2,
	[hwmon_temp_crit_alarm] = 3,
};

static int tmp401_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct tmp401_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int reg, ret;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_lowest:
	case hwmon_temp_highest:
		reg = TMP401_TEMP_MSB[tmp401_temp_reg_index[attr]][channel];
		ret = regmap_read(regmap, reg, &regval);
		if (ret < 0)
			return ret;
		*val = tmp401_register_to_temp(regval, data->extended_range);
		break;
	case hwmon_temp_crit_hyst:
		mutex_lock(&data->update_lock);
		reg = TMP401_TEMP_MSB[3][channel];
		ret = regmap_read(regmap, reg, &regval);
		if (ret < 0)
			goto unlock;
		*val = tmp401_register_to_temp(regval, data->extended_range);
		ret = regmap_read(regmap, TMP401_TEMP_CRIT_HYST, &regval);
		if (ret < 0)
			goto unlock;
		*val -= regval * 1000;
unlock:
		mutex_unlock(&data->update_lock);
		if (ret < 0)
			return ret;
		break;
	case hwmon_temp_fault:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
		reg = TMP432_STATUS_REG[tmp401_status_reg_index[attr]];
		ret = regmap_read(regmap, reg, &regval);
		if (ret < 0)
			return ret;
		*val = !!(regval & BIT(channel));
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int tmp401_temp_write(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct tmp401_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int reg, ret, temp;

	mutex_lock(&data->update_lock);
	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
		reg = TMP401_TEMP_MSB[tmp401_temp_reg_index[attr]][channel];
		regval = tmp401_temp_to_register(val, data->extended_range,
						 attr == hwmon_temp_crit ? 8 : 4);
		ret = regmap_write(regmap, reg, regval);
		break;
	case hwmon_temp_crit_hyst:
		if (data->extended_range)
			val = clamp_val(val, -64000, 191000);
		else
			val = clamp_val(val, 0, 127000);

		reg = TMP401_TEMP_MSB[3][channel];
		ret = regmap_read(regmap, reg, &regval);
		if (ret < 0)
			break;
		temp = tmp401_register_to_temp(regval, data->extended_range);
		val = clamp_val(val, temp - 255000, temp);
		regval = ((temp - val) + 500) / 1000;
		ret = regmap_write(regmap, TMP401_TEMP_CRIT_HYST, regval);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static int tmp401_chip_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct tmp401_data *data = dev_get_drvdata(dev);
	u32 regval;
	int ret;

	switch (attr) {
	case hwmon_chip_update_interval:
		ret = regmap_read(data->regmap, TMP401_CONVERSION_RATE, &regval);
		if (ret < 0)
			return ret;
		*val = (1 << (7 - regval)) * 125;
		break;
	case hwmon_chip_temp_reset_history:
		*val = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int tmp401_set_convrate(struct regmap *regmap, long val)
{
	int rate;

	/*
	 * For valid rates, interval can be calculated as
	 *	interval = (1 << (7 - rate)) * 125;
	 * Rounded rate is therefore
	 *	rate = 7 - __fls(interval * 4 / (125 * 3));
	 * Use clamp_val() to avoid overflows, and to ensure valid input
	 * for __fls.
	 */
	val = clamp_val(val, 125, 16000);
	rate = 7 - __fls(val * 4 / (125 * 3));
	return regmap_write(regmap, TMP401_CONVERSION_RATE, rate);
}

static int tmp401_chip_write(struct device *dev, u32 attr, int channel, long val)
{
	struct tmp401_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int err;

	mutex_lock(&data->update_lock);
	switch (attr) {
	case hwmon_chip_update_interval:
		err = tmp401_set_convrate(regmap, val);
		break;
	case hwmon_chip_temp_reset_history:
		if (val != 1) {
			err = -EINVAL;
			break;
		}
		/*
		 * Reset history by writing any value to any of the
		 * minimum/maximum registers (0x30-0x37).
		 */
		err = regmap_write(regmap, 0x30, 0);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&data->update_lock);

	return err;
}

static int tmp401_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return tmp401_chip_read(dev, attr, channel, val);
	case hwmon_temp:
		return tmp401_temp_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int tmp401_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return tmp401_chip_write(dev, attr, channel, val);
	case hwmon_temp:
		return tmp401_temp_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t tmp401_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
		case hwmon_chip_temp_reset_history:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
		case hwmon_temp_crit_alarm:
		case hwmon_temp_fault:
		case hwmon_temp_lowest:
		case hwmon_temp_highest:
			return 0444;
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			return 0644;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_ops tmp401_ops = {
	.is_visible = tmp401_is_visible,
	.read = tmp401_read,
	.write = tmp401_write,
};

/* chip initialization, detect, probe */

static int tmp401_init_client(struct tmp401_data *data)
{
	struct regmap *regmap = data->regmap;
	u32 config, config_orig;
	int ret;

	/* Set conversion rate to 2 Hz */
	ret = regmap_write(regmap, TMP401_CONVERSION_RATE, 5);
	if (ret < 0)
		return ret;

	/* Start conversions (disable shutdown if necessary) */
	ret = regmap_read(regmap, TMP401_CONFIG, &config);
	if (ret < 0)
		return ret;

	config_orig = config;
	config &= ~TMP401_CONFIG_SHUTDOWN;

	data->extended_range = !!(config & TMP401_CONFIG_RANGE);

	if (config != config_orig)
		ret = regmap_write(regmap, TMP401_CONFIG, config);

	return ret;
}

static int tmp401_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	enum chips kind;
	struct i2c_adapter *adapter = client->adapter;
	u8 reg;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Detect and identify the chip */
	reg = i2c_smbus_read_byte_data(client, TMP401_MANUFACTURER_ID_REG);
	if (reg != TMP401_MANUFACTURER_ID)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, TMP401_DEVICE_ID_REG);

	switch (reg) {
	case TMP401_DEVICE_ID:
		if (client->addr != 0x4c)
			return -ENODEV;
		kind = tmp401;
		break;
	case TMP411A_DEVICE_ID:
		if (client->addr != 0x4c)
			return -ENODEV;
		kind = tmp411;
		break;
	case TMP411B_DEVICE_ID:
		if (client->addr != 0x4d)
			return -ENODEV;
		kind = tmp411;
		break;
	case TMP411C_DEVICE_ID:
		if (client->addr != 0x4e)
			return -ENODEV;
		kind = tmp411;
		break;
	case TMP431_DEVICE_ID:
		if (client->addr != 0x4c && client->addr != 0x4d)
			return -ENODEV;
		kind = tmp431;
		break;
	case TMP432_DEVICE_ID:
		if (client->addr != 0x4c && client->addr != 0x4d)
			return -ENODEV;
		kind = tmp432;
		break;
	case TMP435_DEVICE_ID:
		kind = tmp435;
		break;
	default:
		return -ENODEV;
	}

	reg = i2c_smbus_read_byte_data(client, TMP401_CONFIG);
	if (reg & 0x1b)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, TMP401_CONVERSION_RATE);
	/* Datasheet says: 0x1-0x6 */
	if (reg > 15)
		return -ENODEV;

	strlcpy(info->type, tmp401_id[kind].name, I2C_NAME_SIZE);

	return 0;
}

static int tmp401_probe(struct i2c_client *client)
{
	static const char * const names[] = {
		"TMP401", "TMP411", "TMP431", "TMP432", "TMP435"
	};
	struct device *dev = &client->dev;
	struct hwmon_channel_info *info;
	struct device *hwmon_dev;
	struct tmp401_data *data;
	int status;

	data = devm_kzalloc(dev, sizeof(struct tmp401_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);
	data->kind = i2c_match_id(tmp401_id, client)->driver_data;

	data->regmap = devm_regmap_init(dev, NULL, data, &tmp401_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	/* initialize configuration data */
	data->chip.ops = &tmp401_ops;
	data->chip.info = data->info;

	data->info[0] = &data->chip_info;
	data->info[1] = &data->temp_info;

	info = &data->chip_info;
	info->type = hwmon_chip;
	info->config = data->chip_channel_config;

	data->chip_channel_config[0] = HWMON_C_UPDATE_INTERVAL;

	info = &data->temp_info;
	info->type = hwmon_temp;
	info->config = data->temp_channel_config;

	data->temp_channel_config[0] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
		HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
		HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM;
	data->temp_channel_config[1] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
		HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
		HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT;

	if (data->kind == tmp411) {
		data->temp_channel_config[0] |= HWMON_T_HIGHEST | HWMON_T_LOWEST;
		data->temp_channel_config[1] |= HWMON_T_HIGHEST | HWMON_T_LOWEST;
		data->chip_channel_config[0] |= HWMON_C_TEMP_RESET_HISTORY;
	}

	if (data->kind == tmp432) {
		data->temp_channel_config[2] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT;
	}

	/* Initialize the TMP401 chip */
	status = tmp401_init_client(data);
	if (status < 0)
		return status;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &data->chip, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "Detected TI %s chip\n", names[data->kind]);

	return 0;
}

static const struct of_device_id __maybe_unused tmp4xx_of_match[] = {
	{ .compatible = "ti,tmp401", },
	{ .compatible = "ti,tmp411", },
	{ .compatible = "ti,tmp431", },
	{ .compatible = "ti,tmp432", },
	{ .compatible = "ti,tmp435", },
	{ },
};
MODULE_DEVICE_TABLE(of, tmp4xx_of_match);

static struct i2c_driver tmp401_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "tmp401",
		.of_match_table = of_match_ptr(tmp4xx_of_match),
	},
	.probe_new	= tmp401_probe,
	.id_table	= tmp401_id,
	.detect		= tmp401_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(tmp401_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Texas Instruments TMP401 temperature sensor driver");
MODULE_LICENSE("GPL");
