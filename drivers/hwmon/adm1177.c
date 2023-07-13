// SPDX-License-Identifier: GPL-2.0
/*
 * ADM1177 Hot Swap Controller and Digital Power Monitor with Soft Start Pin
 *
 * Copyright 2015-2019 Analog Devices Inc.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

/*  Command Byte Operations */
#define ADM1177_CMD_V_CONT	BIT(0)
#define ADM1177_CMD_I_CONT	BIT(2)
#define ADM1177_CMD_VRANGE	BIT(4)

/* Extended Register */
#define ADM1177_REG_ALERT_TH	2

#define ADM1177_BITS		12

/**
 * struct adm1177_state - driver instance specific data
 * @client:		pointer to i2c client
 * @r_sense_uohm:	current sense resistor value
 * @alert_threshold_ua:	current limit for shutdown
 * @vrange_high:	internal voltage divider
 */
struct adm1177_state {
	struct i2c_client	*client;
	u32			r_sense_uohm;
	u32			alert_threshold_ua;
	bool			vrange_high;
};

static int adm1177_read_raw(struct adm1177_state *st, u8 num, u8 *data)
{
	return i2c_master_recv(st->client, data, num);
}

static int adm1177_write_cmd(struct adm1177_state *st, u8 cmd)
{
	return i2c_smbus_write_byte(st->client, cmd);
}

static int adm1177_write_alert_thr(struct adm1177_state *st,
				   u32 alert_threshold_ua)
{
	u64 val;
	int ret;

	val = 0xFFULL * alert_threshold_ua * st->r_sense_uohm;
	val = div_u64(val, 105840000U);
	val = div_u64(val, 1000U);
	if (val > 0xFF)
		val = 0xFF;

	ret = i2c_smbus_write_byte_data(st->client, ADM1177_REG_ALERT_TH,
					val);
	if (ret)
		return ret;

	st->alert_threshold_ua = alert_threshold_ua;
	return 0;
}

static int adm1177_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct adm1177_state *st = dev_get_drvdata(dev);
	u8 data[3];
	long dummy;
	int ret;

	switch (type) {
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			ret = adm1177_read_raw(st, 3, data);
			if (ret < 0)
				return ret;
			dummy = (data[1] << 4) | (data[2] & 0xF);
			/*
			 * convert to milliamperes
			 * ((105.84mV / 4096) x raw) / senseResistor(ohm)
			 */
			*val = div_u64((105840000ull * dummy),
				       4096 * st->r_sense_uohm);
			return 0;
		case hwmon_curr_max_alarm:
			*val = st->alert_threshold_ua;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	case hwmon_in:
		ret = adm1177_read_raw(st, 3, data);
		if (ret < 0)
			return ret;
		dummy = (data[0] << 4) | (data[2] >> 4);
		/*
		 * convert to millivolts based on resistor devision
		 * (V_fullscale / 4096) * raw
		 */
		if (st->vrange_high)
			dummy *= 26350;
		else
			dummy *= 6650;

		*val = DIV_ROUND_CLOSEST(dummy, 4096);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int adm1177_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct adm1177_state *st = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_max_alarm:
			adm1177_write_alert_thr(st, val);
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t adm1177_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct adm1177_state *st = data;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			return 0444;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			if (st->r_sense_uohm)
				return 0444;
			return 0;
		case hwmon_curr_max_alarm:
			if (st->r_sense_uohm)
				return 0644;
			return 0;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const adm1177_info[] = {
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_MAX_ALARM),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT),
	NULL
};

static const struct hwmon_ops adm1177_hwmon_ops = {
	.is_visible = adm1177_is_visible,
	.read = adm1177_read,
	.write = adm1177_write,
};

static const struct hwmon_chip_info adm1177_chip_info = {
	.ops = &adm1177_hwmon_ops,
	.info = adm1177_info,
};

static int adm1177_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct adm1177_state *st;
	u32 alert_threshold_ua;
	int ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->client = client;

	ret = devm_regulator_get_enable_optional(&client->dev, "vref");
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (device_property_read_u32(dev, "shunt-resistor-micro-ohms",
				     &st->r_sense_uohm))
		st->r_sense_uohm = 0;
	if (device_property_read_u32(dev, "adi,shutdown-threshold-microamp",
				     &alert_threshold_ua)) {
		if (st->r_sense_uohm)
			/*
			 * set maximum default value from datasheet based on
			 * shunt-resistor
			 */
			alert_threshold_ua = div_u64(105840000000,
						     st->r_sense_uohm);
		else
			alert_threshold_ua = 0;
	}
	st->vrange_high = device_property_read_bool(dev,
						    "adi,vrange-high-enable");
	if (alert_threshold_ua && st->r_sense_uohm)
		adm1177_write_alert_thr(st, alert_threshold_ua);

	ret = adm1177_write_cmd(st, ADM1177_CMD_V_CONT |
				    ADM1177_CMD_I_CONT |
				    (st->vrange_high ? 0 : ADM1177_CMD_VRANGE));
	if (ret)
		return ret;

	hwmon_dev =
		devm_hwmon_device_register_with_info(dev, client->name, st,
						     &adm1177_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adm1177_id[] = {
	{"adm1177", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, adm1177_id);

static const struct of_device_id adm1177_dt_ids[] = {
	{ .compatible = "adi,adm1177" },
	{},
};
MODULE_DEVICE_TABLE(of, adm1177_dt_ids);

static struct i2c_driver adm1177_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "adm1177",
		.of_match_table = adm1177_dt_ids,
	},
	.probe = adm1177_probe,
	.id_table = adm1177_id,
};
module_i2c_driver(adm1177_driver);

MODULE_AUTHOR("Beniamin Bia <beniamin.bia@analog.com>");
MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADM1177 ADC driver");
MODULE_LICENSE("GPL v2");
