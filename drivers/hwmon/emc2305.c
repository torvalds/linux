// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for EMC2305 fan controller
 *
 * Copyright (C) 2022 Nvidia Technologies Ltd.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_data/emc2305.h>
#include <linux/thermal.h>

static const unsigned short
emc2305_normal_i2c[] = { 0x27, 0x2c, 0x2d, 0x2e, 0x2f, 0x4c, 0x4d, I2C_CLIENT_END };

#define EMC2305_REG_DRIVE_FAIL_STATUS	0x27
#define EMC2305_REG_VENDOR		0xfe
#define EMC2305_FAN_MAX			0xff
#define EMC2305_FAN_MIN			0x00
#define EMC2305_FAN_MAX_STATE		10
#define EMC2305_DEVICE			0x34
#define EMC2305_VENDOR			0x5d
#define EMC2305_REG_PRODUCT_ID		0xfd
#define EMC2305_TACH_REGS_UNUSE_BITS	3
#define EMC2305_TACH_CNT_MULTIPLIER	0x02
#define EMC2305_TACH_RANGE_MIN		480

#define EMC2305_PWM_DUTY2STATE(duty, max_state, pwm_max) \
	DIV_ROUND_CLOSEST((duty) * (max_state), (pwm_max))
#define EMC2305_PWM_STATE2DUTY(state, max_state, pwm_max) \
	DIV_ROUND_CLOSEST((state) * (pwm_max), (max_state))

/*
 * Factor by equations [2] and [3] from data sheet; valid for fans where the number of edges
 * equal (poles * 2 + 1).
 */
#define EMC2305_RPM_FACTOR		3932160

#define EMC2305_REG_FAN_DRIVE(n)	(0x30 + 0x10 * (n))
#define EMC2305_REG_FAN_MIN_DRIVE(n)	(0x38 + 0x10 * (n))
#define EMC2305_REG_FAN_TACH(n)		(0x3e + 0x10 * (n))

enum emc230x_product_id {
	EMC2305 = 0x34,
	EMC2303 = 0x35,
	EMC2302 = 0x36,
	EMC2301 = 0x37,
};

static const struct i2c_device_id emc2305_ids[] = {
	{ "emc2305", 0 },
	{ "emc2303", 0 },
	{ "emc2302", 0 },
	{ "emc2301", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, emc2305_ids);

/**
 * struct emc2305_cdev_data - device-specific cooling device state
 * @cdev: cooling device
 * @cur_state: cooling current state
 * @last_hwmon_state: last cooling state updated by hwmon subsystem
 * @last_thermal_state: last cooling state updated by thermal subsystem
 *
 * The 'last_hwmon_state' and 'last_thermal_state' fields are provided to support fan low limit
 * speed feature. The purpose of this feature is to provides ability to limit fan speed
 * according to some system wise considerations, like absence of some replaceable units (PSU or
 * line cards), high system ambient temperature, unreliable transceivers temperature sensing or
 * some other factors which indirectly impacts system's airflow
 * Fan low limit feature is supported through 'hwmon' interface: 'hwmon' 'pwm' attribute is
 * used for setting low limit for fan speed in case 'thermal' subsystem is configured in
 * kernel. In this case setting fan speed through 'hwmon' will never let the 'thermal'
 * subsystem to select a lower duty cycle than the duty cycle selected with the 'pwm'
 * attribute.
 * From other side, fan speed is to be updated in hardware through 'pwm' only in case the
 * requested fan speed is above last speed set by 'thermal' subsystem, otherwise requested fan
 * speed will be just stored with no PWM update.
 */
struct emc2305_cdev_data {
	struct thermal_cooling_device *cdev;
	unsigned int cur_state;
	unsigned long last_hwmon_state;
	unsigned long last_thermal_state;
};

/**
 * struct emc2305_data - device-specific data
 * @client: i2c client
 * @hwmon_dev: hwmon device
 * @max_state: maximum cooling state of the cooling device
 * @pwm_num: number of PWM channels
 * @pwm_separate: separate PWM settings for every channel
 * @pwm_min: array of minimum PWM per channel
 * @cdev_data: array of cooling devices data
 */
struct emc2305_data {
	struct i2c_client *client;
	struct device *hwmon_dev;
	u8 max_state;
	u8 pwm_num;
	bool pwm_separate;
	u8 pwm_min[EMC2305_PWM_MAX];
	struct emc2305_cdev_data cdev_data[EMC2305_PWM_MAX];
};

static char *emc2305_fan_name[] = {
	"emc2305_fan",
	"emc2305_fan1",
	"emc2305_fan2",
	"emc2305_fan3",
	"emc2305_fan4",
	"emc2305_fan5",
};

static void emc2305_unset_tz(struct device *dev);

static int emc2305_get_max_channel(const struct emc2305_data *data)
{
	return data->pwm_num;
}

static int emc2305_get_cdev_idx(struct thermal_cooling_device *cdev)
{
	struct emc2305_data *data = cdev->devdata;
	size_t len = strlen(cdev->type);
	int ret;

	if (len <= 0)
		return -EINVAL;

	/*
	 * Returns index of cooling device 0..4 in case of separate PWM setting.
	 * Zero index is used in case of one common PWM setting.
	 * If the mode is not set as pwm_separate, all PWMs are to be bound
	 * to the common thermal zone and should work at the same speed
	 * to perform cooling for the same thermal junction.
	 * Otherwise, return specific channel that will be used in bound
	 * related PWM to the thermal zone.
	 */
	if (!data->pwm_separate)
		return 0;

	ret = cdev->type[len - 1];
	switch (ret) {
	case '1' ... '5':
		return ret - '1';
	default:
		break;
	}
	return -EINVAL;
}

static int emc2305_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	int cdev_idx;
	struct emc2305_data *data = cdev->devdata;

	cdev_idx = emc2305_get_cdev_idx(cdev);
	if (cdev_idx < 0)
		return cdev_idx;

	*state = data->cdev_data[cdev_idx].cur_state;
	return 0;
}

static int emc2305_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct emc2305_data *data = cdev->devdata;
	*state = data->max_state;
	return 0;
}

static int __emc2305_set_cur_state(struct emc2305_data *data, int cdev_idx, unsigned long state)
{
	int ret;
	struct i2c_client *client = data->client;
	u8 val, i;

	state = max_t(unsigned long, state, data->cdev_data[cdev_idx].last_hwmon_state);

	val = EMC2305_PWM_STATE2DUTY(state, data->max_state, EMC2305_FAN_MAX);

	data->cdev_data[cdev_idx].cur_state = state;
	if (data->pwm_separate) {
		ret = i2c_smbus_write_byte_data(client, EMC2305_REG_FAN_DRIVE(cdev_idx), val);
		if (ret < 0)
			return ret;
	} else {
		/*
		 * Set the same PWM value in all channels
		 * if common PWM channel is used.
		 */
		for (i = 0; i < data->pwm_num; i++) {
			ret = i2c_smbus_write_byte_data(client, EMC2305_REG_FAN_DRIVE(i), val);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int emc2305_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	int cdev_idx, ret;
	struct emc2305_data *data = cdev->devdata;

	if (state > data->max_state)
		return -EINVAL;

	cdev_idx =  emc2305_get_cdev_idx(cdev);
	if (cdev_idx < 0)
		return cdev_idx;

	/* Save thermal state. */
	data->cdev_data[cdev_idx].last_thermal_state = state;
	ret = __emc2305_set_cur_state(data, cdev_idx, state);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct thermal_cooling_device_ops emc2305_cooling_ops = {
	.get_max_state = emc2305_get_max_state,
	.get_cur_state = emc2305_get_cur_state,
	.set_cur_state = emc2305_set_cur_state,
};

static int emc2305_show_fault(struct device *dev, int channel)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int status_reg;

	status_reg = i2c_smbus_read_byte_data(client, EMC2305_REG_DRIVE_FAIL_STATUS);
	if (status_reg < 0)
		return status_reg;

	return status_reg & (1 << channel) ? 1 : 0;
}

static int emc2305_show_fan(struct device *dev, int channel)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	ret = i2c_smbus_read_word_swapped(client, EMC2305_REG_FAN_TACH(channel));
	if (ret <= 0)
		return ret;

	ret = ret >> EMC2305_TACH_REGS_UNUSE_BITS;
	ret = EMC2305_RPM_FACTOR / ret;
	if (ret <= EMC2305_TACH_RANGE_MIN)
		return 0;

	return ret * EMC2305_TACH_CNT_MULTIPLIER;
}

static int emc2305_show_pwm(struct device *dev, int channel)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	return i2c_smbus_read_byte_data(client, EMC2305_REG_FAN_DRIVE(channel));
}

static int emc2305_set_pwm(struct device *dev, long val, int channel)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if (val < data->pwm_min[channel] || val > EMC2305_FAN_MAX)
		return -EINVAL;

	ret = i2c_smbus_write_byte_data(client, EMC2305_REG_FAN_DRIVE(channel), val);
	if (ret < 0)
		return ret;
	data->cdev_data[channel].cur_state = EMC2305_PWM_DUTY2STATE(val, data->max_state,
								    EMC2305_FAN_MAX);
	return 0;
}

static int emc2305_set_single_tz(struct device *dev, int idx)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	long pwm;
	int i, cdev_idx, ret;

	cdev_idx = (idx) ? idx - 1 : 0;
	pwm = data->pwm_min[cdev_idx];

	data->cdev_data[cdev_idx].cdev =
		thermal_cooling_device_register(emc2305_fan_name[idx], data,
						&emc2305_cooling_ops);

	if (IS_ERR(data->cdev_data[cdev_idx].cdev)) {
		dev_err(dev, "Failed to register cooling device %s\n", emc2305_fan_name[idx]);
		return PTR_ERR(data->cdev_data[cdev_idx].cdev);
	}
	/* Set minimal PWM speed. */
	if (data->pwm_separate) {
		ret = emc2305_set_pwm(dev, pwm, cdev_idx);
		if (ret < 0)
			return ret;
	} else {
		for (i = 0; i < data->pwm_num; i++) {
			ret = emc2305_set_pwm(dev, pwm, i);
			if (ret < 0)
				return ret;
		}
	}
	data->cdev_data[cdev_idx].cur_state =
		EMC2305_PWM_DUTY2STATE(data->pwm_min[cdev_idx], data->max_state,
				       EMC2305_FAN_MAX);
	data->cdev_data[cdev_idx].last_hwmon_state =
		EMC2305_PWM_DUTY2STATE(data->pwm_min[cdev_idx], data->max_state,
				       EMC2305_FAN_MAX);
	return 0;
}

static int emc2305_set_tz(struct device *dev)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	int i, ret;

	if (!data->pwm_separate)
		return emc2305_set_single_tz(dev, 0);

	for (i = 0; i < data->pwm_num; i++) {
		ret = emc2305_set_single_tz(dev, i + 1);
		if (ret)
			goto thermal_cooling_device_register_fail;
	}
	return 0;

thermal_cooling_device_register_fail:
	emc2305_unset_tz(dev);
	return ret;
}

static void emc2305_unset_tz(struct device *dev)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	int i;

	/* Unregister cooling device. */
	for (i = 0; i < EMC2305_PWM_MAX; i++)
		if (data->cdev_data[i].cdev)
			thermal_cooling_device_unregister(data->cdev_data[i].cdev);
}

static umode_t
emc2305_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
	int max_channel = emc2305_get_max_channel(data);

	/* Don't show channels which are not physically connected. */
	if (channel >= max_channel)
		return 0;
	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		case hwmon_fan_fault:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return 0644;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
};

static int
emc2305_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long val)
{
	struct emc2305_data *data = dev_get_drvdata(dev);
	int cdev_idx;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			/* If thermal is configured - handle PWM limit setting. */
			if (IS_REACHABLE(CONFIG_THERMAL)) {
				if (data->pwm_separate)
					cdev_idx = channel;
				else
					cdev_idx = 0;
				data->cdev_data[cdev_idx].last_hwmon_state =
					EMC2305_PWM_DUTY2STATE(val, data->max_state,
							       EMC2305_FAN_MAX);
				/*
				 * Update PWM only in case requested state is not less than the
				 * last thermal state.
				 */
				if (data->cdev_data[cdev_idx].last_hwmon_state >=
				    data->cdev_data[cdev_idx].last_thermal_state)
					return __emc2305_set_cur_state(data, cdev_idx,
							data->cdev_data[cdev_idx].last_hwmon_state);
				return 0;
			}
			return emc2305_set_pwm(dev, val, channel);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
};

static int
emc2305_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
	int ret;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			ret = emc2305_show_fan(dev, channel);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		case hwmon_fan_fault:
			ret = emc2305_show_fault(dev, channel);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			ret = emc2305_show_pwm(dev, channel);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
};

static const struct hwmon_ops emc2305_ops = {
	.is_visible = emc2305_is_visible,
	.read = emc2305_read,
	.write = emc2305_write,
};

static const struct hwmon_channel_info * const emc2305_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info emc2305_chip_info = {
	.ops = &emc2305_ops,
	.info = emc2305_info,
};

static int emc2305_identify(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct emc2305_data *data = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_read_byte_data(client, EMC2305_REG_PRODUCT_ID);
	if (ret < 0)
		return ret;

	switch (ret) {
	case EMC2305:
		data->pwm_num = 5;
		break;
	case EMC2303:
		data->pwm_num = 3;
		break;
	case EMC2302:
		data->pwm_num = 2;
		break;
	case EMC2301:
		data->pwm_num = 1;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static int emc2305_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct emc2305_data *data;
	struct emc2305_platform_data *pdata;
	int vendor;
	int ret;
	int i;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	vendor = i2c_smbus_read_byte_data(client, EMC2305_REG_VENDOR);
	if (vendor != EMC2305_VENDOR)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;

	ret = emc2305_identify(dev);
	if (ret)
		return ret;

	pdata = dev_get_platdata(&client->dev);
	if (pdata) {
		if (!pdata->max_state || pdata->max_state > EMC2305_FAN_MAX_STATE)
			return -EINVAL;
		data->max_state = pdata->max_state;
		/*
		 * Validate a number of active PWM channels. Note that
		 * configured number can be less than the actual maximum
		 * supported by the device.
		 */
		if (!pdata->pwm_num || pdata->pwm_num > EMC2305_PWM_MAX)
			return -EINVAL;
		data->pwm_num = pdata->pwm_num;
		data->pwm_separate = pdata->pwm_separate;
		for (i = 0; i < EMC2305_PWM_MAX; i++)
			data->pwm_min[i] = pdata->pwm_min[i];
	} else {
		data->max_state = EMC2305_FAN_MAX_STATE;
		data->pwm_separate = false;
		for (i = 0; i < EMC2305_PWM_MAX; i++)
			data->pwm_min[i] = EMC2305_FAN_MIN;
	}

	data->hwmon_dev = devm_hwmon_device_register_with_info(dev, "emc2305", data,
							       &emc2305_chip_info, NULL);
	if (IS_ERR(data->hwmon_dev))
		return PTR_ERR(data->hwmon_dev);

	if (IS_REACHABLE(CONFIG_THERMAL)) {
		ret = emc2305_set_tz(dev);
		if (ret != 0)
			return ret;
	}

	for (i = 0; i < data->pwm_num; i++) {
		ret = i2c_smbus_write_byte_data(client, EMC2305_REG_FAN_MIN_DRIVE(i),
						data->pwm_min[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void emc2305_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	if (IS_REACHABLE(CONFIG_THERMAL))
		emc2305_unset_tz(dev);
}

static struct i2c_driver emc2305_driver = {
	.class  = I2C_CLASS_HWMON,
	.driver = {
		.name = "emc2305",
	},
	.probe = emc2305_probe,
	.remove	  = emc2305_remove,
	.id_table = emc2305_ids,
	.address_list = emc2305_normal_i2c,
};

module_i2c_driver(emc2305_driver);

MODULE_AUTHOR("Nvidia");
MODULE_DESCRIPTION("Microchip EMC2305 fan controller driver");
MODULE_LICENSE("GPL");
