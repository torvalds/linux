// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2008, 2010 Davide Rizzo <elpa.rizzo@gmail.com>
 *
 * The LM95241 is a sensor chip made by National Semiconductors.
 * It reports up to three temperatures (its own plus up to two external ones).
 * Complete datasheet can be obtained from National's website at:
 *   http://www.national.com/ds.cgi/LM/LM95241.pdf
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define DEVNAME "lm95241"

static const unsigned short normal_i2c[] = {
	0x19, 0x2a, 0x2b, I2C_CLIENT_END };

/* LM95241 registers */
#define LM95241_REG_R_MAN_ID		0xFE
#define LM95241_REG_R_CHIP_ID		0xFF
#define LM95241_REG_R_STATUS		0x02
#define LM95241_REG_RW_CONFIG		0x03
#define LM95241_REG_RW_REM_FILTER	0x06
#define LM95241_REG_RW_TRUTHERM		0x07
#define LM95241_REG_W_ONE_SHOT		0x0F
#define LM95241_REG_R_LOCAL_TEMPH	0x10
#define LM95241_REG_R_REMOTE1_TEMPH	0x11
#define LM95241_REG_R_REMOTE2_TEMPH	0x12
#define LM95241_REG_R_LOCAL_TEMPL	0x20
#define LM95241_REG_R_REMOTE1_TEMPL	0x21
#define LM95241_REG_R_REMOTE2_TEMPL	0x22
#define LM95241_REG_RW_REMOTE_MODEL	0x30

/* LM95241 specific bitfields */
#define CFG_STOP	BIT(6)
#define CFG_CR0076	0x00
#define CFG_CR0182	BIT(4)
#define CFG_CR1000	BIT(5)
#define CFG_CR2700	(BIT(4) | BIT(5))
#define CFG_CRMASK	(BIT(4) | BIT(5))
#define R1MS_MASK	BIT(0)
#define R2MS_MASK	BIT(2)
#define R1DF_MASK	BIT(1)
#define R2DF_MASK	BIT(2)
#define R1FE_MASK	BIT(0)
#define R2FE_MASK	BIT(2)
#define R1DM		BIT(0)
#define R2DM		BIT(1)
#define TT1_SHIFT	0
#define TT2_SHIFT	4
#define TT_OFF		0
#define TT_ON		1
#define TT_MASK		7
#define NATSEMI_MAN_ID	0x01
#define LM95231_CHIP_ID	0xA1
#define LM95241_CHIP_ID	0xA4

static const u8 lm95241_reg_address[] = {
	LM95241_REG_R_LOCAL_TEMPH,
	LM95241_REG_R_LOCAL_TEMPL,
	LM95241_REG_R_REMOTE1_TEMPH,
	LM95241_REG_R_REMOTE1_TEMPL,
	LM95241_REG_R_REMOTE2_TEMPH,
	LM95241_REG_R_REMOTE2_TEMPL
};

/* Client data (each client gets its own) */
struct lm95241_data {
	struct i2c_client *client;
	struct mutex update_lock;
	unsigned long last_updated;	/* in jiffies */
	unsigned long interval;		/* in milli-seconds */
	bool valid;		/* false until following fields are valid */
	/* registers values */
	u8 temp[ARRAY_SIZE(lm95241_reg_address)];
	u8 status, config, model, trutherm;
};

/* Conversions */
static int temp_from_reg_signed(u8 val_h, u8 val_l)
{
	s16 val_hl = (val_h << 8) | val_l;
	return val_hl * 1000 / 256;
}

static int temp_from_reg_unsigned(u8 val_h, u8 val_l)
{
	u16 val_hl = (val_h << 8) | val_l;
	return val_hl * 1000 / 256;
}

static struct lm95241_data *lm95241_update_device(struct device *dev)
{
	struct lm95241_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated
		       + msecs_to_jiffies(data->interval)) ||
	    !data->valid) {
		int i;

		dev_dbg(dev, "Updating lm95241 data.\n");
		for (i = 0; i < ARRAY_SIZE(lm95241_reg_address); i++)
			data->temp[i]
			  = i2c_smbus_read_byte_data(client,
						     lm95241_reg_address[i]);

		data->status = i2c_smbus_read_byte_data(client,
							LM95241_REG_R_STATUS);
		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int lm95241_read_chip(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct lm95241_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_chip_update_interval:
		*val = data->interval;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95241_read_temp(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct lm95241_data *data = lm95241_update_device(dev);

	switch (attr) {
	case hwmon_temp_input:
		if (!channel || (data->config & BIT(channel - 1)))
			*val = temp_from_reg_signed(data->temp[channel * 2],
						data->temp[channel * 2 + 1]);
		else
			*val = temp_from_reg_unsigned(data->temp[channel * 2],
						data->temp[channel * 2 + 1]);
		return 0;
	case hwmon_temp_min:
		if (channel == 1)
			*val = (data->config & R1DF_MASK) ? -128000 : 0;
		else
			*val = (data->config & R2DF_MASK) ? -128000 : 0;
		return 0;
	case hwmon_temp_max:
		if (channel == 1)
			*val = (data->config & R1DF_MASK) ? 127875 : 255875;
		else
			*val = (data->config & R2DF_MASK) ? 127875 : 255875;
		return 0;
	case hwmon_temp_type:
		if (channel == 1)
			*val = (data->model & R1MS_MASK) ? 1 : 2;
		else
			*val = (data->model & R2MS_MASK) ? 1 : 2;
		return 0;
	case hwmon_temp_fault:
		if (channel == 1)
			*val = !!(data->status & R1DM);
		else
			*val = !!(data->status & R2DM);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95241_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return lm95241_read_chip(dev, attr, channel, val);
	case hwmon_temp:
		return lm95241_read_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95241_write_chip(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct lm95241_data *data = dev_get_drvdata(dev);
	int convrate;
	u8 config;
	int ret;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_chip_update_interval:
		config = data->config & ~CFG_CRMASK;
		if (val < 130) {
			convrate = 76;
			config |= CFG_CR0076;
		} else if (val < 590) {
			convrate = 182;
			config |= CFG_CR0182;
		} else if (val < 1850) {
			convrate = 1000;
			config |= CFG_CR1000;
		} else {
			convrate = 2700;
			config |= CFG_CR2700;
		}
		data->interval = convrate;
		data->config = config;
		ret = i2c_smbus_write_byte_data(data->client,
						LM95241_REG_RW_CONFIG, config);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static int lm95241_write_temp(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct lm95241_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_temp_min:
		if (channel == 1) {
			if (val < 0)
				data->config |= R1DF_MASK;
			else
				data->config &= ~R1DF_MASK;
		} else {
			if (val < 0)
				data->config |= R2DF_MASK;
			else
				data->config &= ~R2DF_MASK;
		}
		data->valid = false;
		ret = i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG,
						data->config);
		break;
	case hwmon_temp_max:
		if (channel == 1) {
			if (val <= 127875)
				data->config |= R1DF_MASK;
			else
				data->config &= ~R1DF_MASK;
		} else {
			if (val <= 127875)
				data->config |= R2DF_MASK;
			else
				data->config &= ~R2DF_MASK;
		}
		data->valid = false;
		ret = i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG,
						data->config);
		break;
	case hwmon_temp_type:
		if (val != 1 && val != 2) {
			ret = -EINVAL;
			break;
		}
		if (channel == 1) {
			data->trutherm &= ~(TT_MASK << TT1_SHIFT);
			if (val == 1) {
				data->model |= R1MS_MASK;
				data->trutherm |= (TT_ON << TT1_SHIFT);
			} else {
				data->model &= ~R1MS_MASK;
				data->trutherm |= (TT_OFF << TT1_SHIFT);
			}
		} else {
			data->trutherm &= ~(TT_MASK << TT2_SHIFT);
			if (val == 1) {
				data->model |= R2MS_MASK;
				data->trutherm |= (TT_ON << TT2_SHIFT);
			} else {
				data->model &= ~R2MS_MASK;
				data->trutherm |= (TT_OFF << TT2_SHIFT);
			}
		}
		ret = i2c_smbus_write_byte_data(client,
						LM95241_REG_RW_REMOTE_MODEL,
						data->model);
		if (ret < 0)
			break;
		ret = i2c_smbus_write_byte_data(client, LM95241_REG_RW_TRUTHERM,
						data->trutherm);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&data->update_lock);

	return ret;
}

static int lm95241_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return lm95241_write_chip(dev, attr, channel, val);
	case hwmon_temp:
		return lm95241_write_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t lm95241_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_fault:
			return 0444;
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_type:
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm95241_detect(struct i2c_client *new_client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	const char *name;
	int mfg_id, chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	mfg_id = i2c_smbus_read_byte_data(new_client, LM95241_REG_R_MAN_ID);
	if (mfg_id != NATSEMI_MAN_ID)
		return -ENODEV;

	chip_id = i2c_smbus_read_byte_data(new_client, LM95241_REG_R_CHIP_ID);
	switch (chip_id) {
	case LM95231_CHIP_ID:
		name = "lm95231";
		break;
	case LM95241_CHIP_ID:
		name = "lm95241";
		break;
	default:
		return -ENODEV;
	}

	/* Fill the i2c board info */
	strscpy(info->type, name, I2C_NAME_SIZE);
	return 0;
}

static void lm95241_init_client(struct i2c_client *client,
				struct lm95241_data *data)
{
	data->interval = 1000;
	data->config = CFG_CR1000;
	data->trutherm = (TT_OFF << TT1_SHIFT) | (TT_OFF << TT2_SHIFT);

	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG, data->config);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REM_FILTER,
				  R1FE_MASK | R2FE_MASK);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_TRUTHERM,
				  data->trutherm);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REMOTE_MODEL,
				  data->model);
}

static const struct hwmon_channel_info *lm95241_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN |
			   HWMON_T_TYPE | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN |
			   HWMON_T_TYPE | HWMON_T_FAULT),
	NULL
};

static const struct hwmon_ops lm95241_hwmon_ops = {
	.is_visible = lm95241_is_visible,
	.read = lm95241_read,
	.write = lm95241_write,
};

static const struct hwmon_chip_info lm95241_chip_info = {
	.ops = &lm95241_hwmon_ops,
	.info = lm95241_info,
};

static int lm95241_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lm95241_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(dev, sizeof(struct lm95241_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the LM95241 chip */
	lm95241_init_client(client, data);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							   data,
							   &lm95241_chip_info,
							   NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* Driver data (common to all clients) */
static const struct i2c_device_id lm95241_id[] = {
	{ "lm95231", 0 },
	{ "lm95241", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm95241_id);

static struct i2c_driver lm95241_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= DEVNAME,
	},
	.probe_new	= lm95241_probe,
	.id_table	= lm95241_id,
	.detect		= lm95241_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm95241_driver);

MODULE_AUTHOR("Davide Rizzo <elpa.rizzo@gmail.com>");
MODULE_DESCRIPTION("LM95231/LM95241 sensor driver");
MODULE_LICENSE("GPL");
