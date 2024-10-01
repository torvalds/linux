// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This is a non-complete driver implementation for the
 * HS3001 humidity and temperature sensor and compatibles. It does not include
 * the configuration possibilities, where it needs to be set to 'programming mode'
 * during power-up.
 *
 *
 * Copyright (C) 2023 SYS TEC electronic AG
 * Author: Andre Werner <andre.werner@systec-electronic.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/types.h>

/* Measurement times */
#define HS3001_WAKEUP_TIME	100	/* us */
#define HS3001_8BIT_RESOLUTION	550	/* us */
#define HS3001_10BIT_RESOLUTION	1310	/* us */
#define HS3001_12BIT_RESOLUTION	4500	/* us */
#define HS3001_14BIT_RESOLUTION	16900	/* us */

#define HS3001_RESPONSE_LENGTH	4

#define HS3001_FIXPOINT_ARITH	1000U

#define HS3001_MASK_HUMIDITY_0X3FFF	GENMASK(13, 0)
#define HS3001_MASK_STATUS_0XC0	GENMASK(7, 6)

/* Definitions for Status Bits of A/D Data */
#define HS3001_DATA_VALID	0x00	/* Valid Data */
#define HS3001_DATA_STALE	0x01	/* Stale Data */

struct hs3001_data {
	struct i2c_client *client;
	struct mutex i2c_lock; /* lock for sending i2c commands */
	u32 wait_time;		/* in us */
	int temperature;	/* in milli degree */
	u32 humidity;		/* in milli % */
};

static int hs3001_extract_temperature(u16 raw)
{
	/* fixpoint arithmetic 1 digit */
	u32 temp = (raw >> 2) * HS3001_FIXPOINT_ARITH * 165;

	temp /= (1 << 14) - 1;

	return (int)temp - 40 * HS3001_FIXPOINT_ARITH;
}

static u32 hs3001_extract_humidity(u16 raw)
{
	u32 hum = (raw & HS3001_MASK_HUMIDITY_0X3FFF) * HS3001_FIXPOINT_ARITH * 100;

	return hum / (1 << 14) - 1;
}

static int hs3001_data_fetch_command(struct i2c_client *client,
				     struct hs3001_data *data)
{
	int ret;
	u8 buf[HS3001_RESPONSE_LENGTH];
	u8 hs3001_status;

	ret = i2c_master_recv(client, buf, HS3001_RESPONSE_LENGTH);
	if (ret != HS3001_RESPONSE_LENGTH) {
		ret = ret < 0 ? ret : -EIO;
		dev_dbg(&client->dev,
			"Error in i2c communication. Error code: %d.\n", ret);
		return ret;
	}

	hs3001_status = FIELD_GET(HS3001_MASK_STATUS_0XC0, buf[0]);
	if (hs3001_status == HS3001_DATA_STALE) {
		dev_dbg(&client->dev, "Sensor busy.\n");
		return -EBUSY;
	}
	if (hs3001_status != HS3001_DATA_VALID) {
		dev_dbg(&client->dev, "Data invalid.\n");
		return -EIO;
	}

	data->humidity =
		hs3001_extract_humidity(be16_to_cpup((__be16 *)&buf[0]));
	data->temperature =
		hs3001_extract_temperature(be16_to_cpup((__be16 *)&buf[2]));

	return 0;
}

static umode_t hs3001_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	/* Both, humidity and temperature can only be read. */
	return 0444;
}

static int hs3001_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct hs3001_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	mutex_lock(&data->i2c_lock);
	ret = i2c_master_send(client, NULL, 0);
	if (ret < 0) {
		mutex_unlock(&data->i2c_lock);
		return ret;
	}

	/*
	 * Sensor needs some time to process measurement depending on
	 * resolution (ref. datasheet)
	 */
	fsleep(data->wait_time);

	ret = hs3001_data_fetch_command(client, data);
	mutex_unlock(&data->i2c_lock);

	if (ret < 0)
		return ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			*val = data->temperature;
			break;
		default:
			return -EINVAL;
		}
		break;
	case hwmon_humidity:
		switch (attr) {
		case hwmon_humidity_input:
			*val = data->humidity;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct hwmon_channel_info *hs3001_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(humidity, HWMON_H_INPUT),
	NULL
};

static const struct hwmon_ops hs3001_hwmon_ops = {
	.is_visible = hs3001_is_visible,
	.read = hs3001_read,
};

static const struct hwmon_chip_info hs3001_chip_info = {
	.ops = &hs3001_hwmon_ops,
	.info = hs3001_info,
};

/* device ID table */
static const struct i2c_device_id hs3001_ids[] = {
	{ "hs3001" },
	{ },
};

MODULE_DEVICE_TABLE(i2c, hs3001_ids);

static const struct of_device_id hs3001_of_match[] = {
	{.compatible = "renesas,hs3001"},
	{ },
};

MODULE_DEVICE_TABLE(of, hs3001_of_match);

static int hs3001_probe(struct i2c_client *client)
{
	struct hs3001_data *data;
	struct device *hwmon_dev;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;

	/*
	 * Measurement time = wake-up time + measurement time temperature
	 * + measurement time humidity. This is currently static, because
	 * enabling programming mode is not supported, yet.
	 */
	data->wait_time = (HS3001_WAKEUP_TIME + HS3001_14BIT_RESOLUTION +
			   HS3001_14BIT_RESOLUTION);

	mutex_init(&data->i2c_lock);

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 client->name,
							 data,
							 &hs3001_chip_info,
							 NULL);

	if (IS_ERR(hwmon_dev))
		return dev_err_probe(dev, PTR_ERR(hwmon_dev),
				     "Unable to register hwmon device.\n");

	return 0;
}

static struct i2c_driver hs3001_i2c_driver = {
	.driver = {
		   .name = "hs3001",
		   .of_match_table = hs3001_of_match,
	},
	.probe = hs3001_probe,
	.id_table = hs3001_ids,
};

module_i2c_driver(hs3001_i2c_driver);

MODULE_AUTHOR("Andre Werner <andre.werner@systec-electronic.com>");
MODULE_DESCRIPTION("HS3001 humidity and temperature sensor base driver");
MODULE_LICENSE("GPL");
