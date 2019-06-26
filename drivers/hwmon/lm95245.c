// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 Alexander Stein <alexander.stein@systec-electronic.com>
 *
 * The LM95245 is a sensor chip made by TI / National Semiconductor.
 * It reports up to two temperatures (its own plus an external one).
 *
 * This driver is based on lm95241.c
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x29, 0x4c, 0x4d, I2C_CLIENT_END };

/* LM95245 registers */
/* general registers */
#define LM95245_REG_RW_CONFIG1		0x03
#define LM95245_REG_RW_CONVERS_RATE	0x04
#define LM95245_REG_W_ONE_SHOT		0x0F

/* diode configuration */
#define LM95245_REG_RW_CONFIG2		0xBF
#define LM95245_REG_RW_REMOTE_OFFH	0x11
#define LM95245_REG_RW_REMOTE_OFFL	0x12

/* status registers */
#define LM95245_REG_R_STATUS1		0x02
#define LM95245_REG_R_STATUS2		0x33

/* limit registers */
#define LM95245_REG_RW_REMOTE_OS_LIMIT		0x07
#define LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT	0x20
#define LM95245_REG_RW_REMOTE_TCRIT_LIMIT	0x19
#define LM95245_REG_RW_COMMON_HYSTERESIS	0x21

/* temperature signed */
#define LM95245_REG_R_LOCAL_TEMPH_S	0x00
#define LM95245_REG_R_LOCAL_TEMPL_S	0x30
#define LM95245_REG_R_REMOTE_TEMPH_S	0x01
#define LM95245_REG_R_REMOTE_TEMPL_S	0x10
/* temperature unsigned */
#define LM95245_REG_R_REMOTE_TEMPH_U	0x31
#define LM95245_REG_R_REMOTE_TEMPL_U	0x32

/* id registers */
#define LM95245_REG_R_MAN_ID		0xFE
#define LM95245_REG_R_CHIP_ID		0xFF

/* LM95245 specific bitfields */
#define CFG_STOP		0x40
#define CFG_REMOTE_TCRIT_MASK	0x10
#define CFG_REMOTE_OS_MASK	0x08
#define CFG_LOCAL_TCRIT_MASK	0x04
#define CFG_LOCAL_OS_MASK	0x02

#define CFG2_OS_A0		0x40
#define CFG2_DIODE_FAULT_OS	0x20
#define CFG2_DIODE_FAULT_TCRIT	0x10
#define CFG2_REMOTE_TT		0x08
#define CFG2_REMOTE_FILTER_DIS	0x00
#define CFG2_REMOTE_FILTER_EN	0x06

/* conversation rate in ms */
#define RATE_CR0063	0x00
#define RATE_CR0364	0x01
#define RATE_CR1000	0x02
#define RATE_CR2500	0x03

#define STATUS1_ROS		0x10
#define STATUS1_DIODE_FAULT	0x04
#define STATUS1_RTCRIT		0x02
#define STATUS1_LOC		0x01

#define MANUFACTURER_ID		0x01
#define LM95235_REVISION	0xB1
#define LM95245_REVISION	0xB3

/* Client data (each client gets its own) */
struct lm95245_data {
	struct regmap *regmap;
	struct mutex update_lock;
	int interval;	/* in msecs */
};

/* Conversions */
static int temp_from_reg_unsigned(u8 val_h, u8 val_l)
{
	return val_h * 1000 + val_l * 1000 / 256;
}

static int temp_from_reg_signed(u8 val_h, u8 val_l)
{
	if (val_h & 0x80)
		return (val_h - 0x100) * 1000;
	return temp_from_reg_unsigned(val_h, val_l);
}

static int lm95245_read_conversion_rate(struct lm95245_data *data)
{
	unsigned int rate;
	int ret;

	ret = regmap_read(data->regmap, LM95245_REG_RW_CONVERS_RATE, &rate);
	if (ret < 0)
		return ret;

	switch (rate) {
	case RATE_CR0063:
		data->interval = 63;
		break;
	case RATE_CR0364:
		data->interval = 364;
		break;
	case RATE_CR1000:
		data->interval = 1000;
		break;
	case RATE_CR2500:
	default:
		data->interval = 2500;
		break;
	}
	return 0;
}

static int lm95245_set_conversion_rate(struct lm95245_data *data, long interval)
{
	int ret, rate;

	if (interval <= 63) {
		interval = 63;
		rate = RATE_CR0063;
	} else if (interval <= 364) {
		interval = 364;
		rate = RATE_CR0364;
	} else if (interval <= 1000) {
		interval = 1000;
		rate = RATE_CR1000;
	} else {
		interval = 2500;
		rate = RATE_CR2500;
	}

	ret = regmap_write(data->regmap, LM95245_REG_RW_CONVERS_RATE, rate);
	if (ret < 0)
		return ret;

	data->interval = interval;
	return 0;
}

static int lm95245_read_temp(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct lm95245_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int ret, regl, regh, regvall, regvalh;

	switch (attr) {
	case hwmon_temp_input:
		regl = channel ? LM95245_REG_R_REMOTE_TEMPL_S :
				 LM95245_REG_R_LOCAL_TEMPL_S;
		regh = channel ? LM95245_REG_R_REMOTE_TEMPH_S :
				 LM95245_REG_R_LOCAL_TEMPH_S;
		ret = regmap_read(regmap, regl, &regvall);
		if (ret < 0)
			return ret;
		ret = regmap_read(regmap, regh, &regvalh);
		if (ret < 0)
			return ret;
		/*
		 * Local temp is always signed.
		 * Remote temp has both signed and unsigned data.
		 * Use signed calculation for remote if signed bit is set
		 * or if reported temperature is below signed limit.
		 */
		if (!channel || (regvalh & 0x80) || regvalh < 0x7f) {
			*val = temp_from_reg_signed(regvalh, regvall);
			return 0;
		}
		ret = regmap_read(regmap, LM95245_REG_R_REMOTE_TEMPL_U,
				  &regvall);
		if (ret < 0)
			return ret;
		ret = regmap_read(regmap, LM95245_REG_R_REMOTE_TEMPH_U,
				  &regvalh);
		if (ret < 0)
			return ret;
		*val = temp_from_reg_unsigned(regvalh, regvall);
		return 0;
	case hwmon_temp_max:
		ret = regmap_read(regmap, LM95245_REG_RW_REMOTE_OS_LIMIT,
				  &regvalh);
		if (ret < 0)
			return ret;
		*val = regvalh * 1000;
		return 0;
	case hwmon_temp_crit:
		regh = channel ? LM95245_REG_RW_REMOTE_TCRIT_LIMIT :
				 LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT;
		ret = regmap_read(regmap, regh, &regvalh);
		if (ret < 0)
			return ret;
		*val = regvalh * 1000;
		return 0;
	case hwmon_temp_max_hyst:
		ret = regmap_read(regmap, LM95245_REG_RW_REMOTE_OS_LIMIT,
				  &regvalh);
		if (ret < 0)
			return ret;
		ret = regmap_read(regmap, LM95245_REG_RW_COMMON_HYSTERESIS,
				  &regvall);
		if (ret < 0)
			return ret;
		*val = (regvalh - regvall) * 1000;
		return 0;
	case hwmon_temp_crit_hyst:
		regh = channel ? LM95245_REG_RW_REMOTE_TCRIT_LIMIT :
				 LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT;
		ret = regmap_read(regmap, regh, &regvalh);
		if (ret < 0)
			return ret;
		ret = regmap_read(regmap, LM95245_REG_RW_COMMON_HYSTERESIS,
				  &regvall);
		if (ret < 0)
			return ret;
		*val = (regvalh - regvall) * 1000;
		return 0;
	case hwmon_temp_type:
		ret = regmap_read(regmap, LM95245_REG_RW_CONFIG2, &regvalh);
		if (ret < 0)
			return ret;
		*val = (regvalh & CFG2_REMOTE_TT) ? 1 : 2;
		return 0;
	case hwmon_temp_offset:
		ret = regmap_read(regmap, LM95245_REG_RW_REMOTE_OFFL,
				  &regvall);
		if (ret < 0)
			return ret;
		ret = regmap_read(regmap, LM95245_REG_RW_REMOTE_OFFH,
				  &regvalh);
		if (ret < 0)
			return ret;
		*val = temp_from_reg_signed(regvalh, regvall);
		return 0;
	case hwmon_temp_max_alarm:
		ret = regmap_read(regmap, LM95245_REG_R_STATUS1, &regvalh);
		if (ret < 0)
			return ret;
		*val = !!(regvalh & STATUS1_ROS);
		return 0;
	case hwmon_temp_crit_alarm:
		ret = regmap_read(regmap, LM95245_REG_R_STATUS1, &regvalh);
		if (ret < 0)
			return ret;
		*val = !!(regvalh & (channel ? STATUS1_RTCRIT : STATUS1_LOC));
		return 0;
	case hwmon_temp_fault:
		ret = regmap_read(regmap, LM95245_REG_R_STATUS1, &regvalh);
		if (ret < 0)
			return ret;
		*val = !!(regvalh & STATUS1_DIODE_FAULT);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95245_write_temp(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct lm95245_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret, reg;

	switch (attr) {
	case hwmon_temp_max:
		val = clamp_val(val / 1000, 0, 255);
		ret = regmap_write(regmap, LM95245_REG_RW_REMOTE_OS_LIMIT, val);
		return ret;
	case hwmon_temp_crit:
		reg = channel ? LM95245_REG_RW_REMOTE_TCRIT_LIMIT :
				LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT;
		val = clamp_val(val / 1000, 0, channel ? 255 : 127);
		ret = regmap_write(regmap, reg, val);
		return ret;
	case hwmon_temp_crit_hyst:
		mutex_lock(&data->update_lock);
		ret = regmap_read(regmap, LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT,
				  &regval);
		if (ret < 0) {
			mutex_unlock(&data->update_lock);
			return ret;
		}
		/* Clamp to reasonable range to prevent overflow */
		val = clamp_val(val, -1000000, 1000000);
		val = regval - val / 1000;
		val = clamp_val(val, 0, 31);
		ret = regmap_write(regmap, LM95245_REG_RW_COMMON_HYSTERESIS,
				   val);
		mutex_unlock(&data->update_lock);
		return ret;
	case hwmon_temp_offset:
		val = clamp_val(val, -128000, 127875);
		val = val * 256 / 1000;
		mutex_lock(&data->update_lock);
		ret = regmap_write(regmap, LM95245_REG_RW_REMOTE_OFFL,
				   val & 0xe0);
		if (ret < 0) {
			mutex_unlock(&data->update_lock);
			return ret;
		}
		ret = regmap_write(regmap, LM95245_REG_RW_REMOTE_OFFH,
				   (val >> 8) & 0xff);
		mutex_unlock(&data->update_lock);
		return ret;
	case hwmon_temp_type:
		if (val != 1 && val != 2)
			return -EINVAL;
		ret = regmap_update_bits(regmap, LM95245_REG_RW_CONFIG2,
					 CFG2_REMOTE_TT,
					 val == 1 ? CFG2_REMOTE_TT : 0);
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95245_read_chip(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct lm95245_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_chip_update_interval:
		*val = data->interval;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95245_write_chip(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct lm95245_data *data = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_chip_update_interval:
		mutex_lock(&data->update_lock);
		ret = lm95245_set_conversion_rate(data, val);
		mutex_unlock(&data->update_lock);
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95245_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return lm95245_read_chip(dev, attr, channel, val);
	case hwmon_temp:
		return lm95245_read_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95245_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return lm95245_write_chip(dev, attr, channel, val);
	case hwmon_temp:
		return lm95245_write_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t lm95245_temp_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_max_alarm:
	case hwmon_temp_max_hyst:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_fault:
		return 0444;
	case hwmon_temp_type:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_offset:
		return 0644;
	case hwmon_temp_crit_hyst:
		return (channel == 0) ? 0644 : 0444;
	default:
		return 0;
	}
}

static umode_t lm95245_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		default:
			return 0;
		}
	case hwmon_temp:
		return lm95245_temp_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm95245_detect(struct i2c_client *new_client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	int address = new_client->addr;
	const char *name;
	int rev, id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	id = i2c_smbus_read_byte_data(new_client, LM95245_REG_R_MAN_ID);
	if (id != MANUFACTURER_ID)
		return -ENODEV;

	rev = i2c_smbus_read_byte_data(new_client, LM95245_REG_R_CHIP_ID);
	switch (rev) {
	case LM95235_REVISION:
		if (address != 0x18 && address != 0x29 && address != 0x4c)
			return -ENODEV;
		name = "lm95235";
		break;
	case LM95245_REVISION:
		name = "lm95245";
		break;
	default:
		return -ENODEV;
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);
	return 0;
}

static int lm95245_init_client(struct lm95245_data *data)
{
	int ret;

	ret = lm95245_read_conversion_rate(data);
	if (ret < 0)
		return ret;

	return regmap_update_bits(data->regmap, LM95245_REG_RW_CONFIG1,
				  CFG_STOP, 0);
}

static bool lm95245_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LM95245_REG_RW_CONFIG1:
	case LM95245_REG_RW_CONVERS_RATE:
	case LM95245_REG_W_ONE_SHOT:
	case LM95245_REG_RW_CONFIG2:
	case LM95245_REG_RW_REMOTE_OFFH:
	case LM95245_REG_RW_REMOTE_OFFL:
	case LM95245_REG_RW_REMOTE_OS_LIMIT:
	case LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT:
	case LM95245_REG_RW_REMOTE_TCRIT_LIMIT:
	case LM95245_REG_RW_COMMON_HYSTERESIS:
		return true;
	default:
		return false;
	}
}

static bool lm95245_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LM95245_REG_R_STATUS1:
	case LM95245_REG_R_STATUS2:
	case LM95245_REG_R_LOCAL_TEMPH_S:
	case LM95245_REG_R_LOCAL_TEMPL_S:
	case LM95245_REG_R_REMOTE_TEMPH_S:
	case LM95245_REG_R_REMOTE_TEMPL_S:
	case LM95245_REG_R_REMOTE_TEMPH_U:
	case LM95245_REG_R_REMOTE_TEMPL_U:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config lm95245_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = lm95245_is_writeable_reg,
	.volatile_reg = lm95245_is_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct hwmon_channel_info *lm95245_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_CRIT_ALARM,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_FAULT |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM |
			   HWMON_T_TYPE | HWMON_T_OFFSET),
	NULL
};

static const struct hwmon_ops lm95245_hwmon_ops = {
	.is_visible = lm95245_is_visible,
	.read = lm95245_read,
	.write = lm95245_write,
};

static const struct hwmon_chip_info lm95245_chip_info = {
	.ops = &lm95245_hwmon_ops,
	.info = lm95245_info,
};

static int lm95245_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lm95245_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct lm95245_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &lm95245_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	mutex_init(&data->update_lock);

	/* Initialize the LM95245 chip */
	ret = lm95245_init_client(data);
	if (ret < 0)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &lm95245_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* Driver data (common to all clients) */
static const struct i2c_device_id lm95245_id[] = {
	{ "lm95235", 0 },
	{ "lm95245", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm95245_id);

static const struct of_device_id __maybe_unused lm95245_of_match[] = {
	{ .compatible = "national,lm95235" },
	{ .compatible = "national,lm95245" },
	{ },
};
MODULE_DEVICE_TABLE(of, lm95245_of_match);

static struct i2c_driver lm95245_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm95245",
		.of_match_table = of_match_ptr(lm95245_of_match),
	},
	.probe		= lm95245_probe,
	.id_table	= lm95245_id,
	.detect		= lm95245_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm95245_driver);

MODULE_AUTHOR("Alexander Stein <alexander.stein@systec-electronic.com>");
MODULE_DESCRIPTION("LM95235/LM95245 sensor driver");
MODULE_LICENSE("GPL");
