// SPDX-License-Identifier: GPL-2.0-only

/*
 * Driver for hwmon elements found on QNAP-MCU devices
 *
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/hwmon.h>
#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/thermal.h>

struct qnap_mcu_hwmon {
	struct qnap_mcu *mcu;
	struct device *dev;

	unsigned int pwm_min;
	unsigned int pwm_max;

	struct fwnode_handle *fan_node;
	unsigned int fan_state;
	unsigned int fan_max_state;
	unsigned int *fan_cooling_levels;

	struct thermal_cooling_device *cdev;
	struct hwmon_chip_info info;
};

static int qnap_mcu_hwmon_get_rpm(struct qnap_mcu_hwmon *hwm)
{
	static const u8 cmd[] = { '@', 'F', 'A' };
	u8 reply[6];
	int ret;

	/* poll the fan rpm */
	ret = qnap_mcu_exec(hwm->mcu, cmd, sizeof(cmd), reply, sizeof(reply));
	if (ret)
		return ret;

	/* First 2 bytes must mirror the sent command */
	if (memcmp(cmd, reply, 2))
		return -EIO;

	return reply[4] * 30;
}

static int qnap_mcu_hwmon_get_pwm(struct qnap_mcu_hwmon *hwm)
{
	static const u8 cmd[] = { '@', 'F', 'Z', '0' }; /* 0 = fan-id? */
	u8 reply[4];
	int ret;

	/* poll the fan pwm */
	ret = qnap_mcu_exec(hwm->mcu, cmd, sizeof(cmd), reply, sizeof(reply));
	if (ret)
		return ret;

	/* First 3 bytes must mirror the sent command */
	if (memcmp(cmd, reply, 3))
		return -EIO;

	return reply[3];
}

static int qnap_mcu_hwmon_set_pwm(struct qnap_mcu_hwmon *hwm, u8 pwm)
{
	const u8 cmd[] = { '@', 'F', 'W', '0', pwm }; /* 0 = fan-id?, pwm 0-255 */

	/* set the fan pwm */
	return qnap_mcu_exec_with_ack(hwm->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_hwmon_get_temp(struct qnap_mcu_hwmon *hwm)
{
	static const u8 cmd[] = { '@', 'T', '3' };
	u8 reply[4];
	int ret;

	/* poll the fan rpm */
	ret = qnap_mcu_exec(hwm->mcu, cmd, sizeof(cmd), reply, sizeof(reply));
	if (ret)
		return ret;

	/* First bytes must mirror the sent command */
	if (memcmp(cmd, reply, sizeof(cmd)))
		return -EIO;

	/*
	 * There is an unknown bit set in bit7.
	 * Bits [6:0] report the actual temperature as returned by the
	 * original qnap firmware-tools, so just drop bit7 for now.
	 */
	return (reply[3] & 0x7f) * 1000;
}

static int qnap_mcu_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
				u32 attr, int channel, long val)
{
	struct qnap_mcu_hwmon *hwm = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;

		if (val != 0)
			val = clamp_val(val, hwm->pwm_min, hwm->pwm_max);

		return qnap_mcu_hwmon_set_pwm(hwm, val);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int qnap_mcu_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, long *val)
{
	struct qnap_mcu_hwmon *hwm = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			ret = qnap_mcu_hwmon_get_pwm(hwm);
			if (ret < 0)
				return ret;

			*val = ret;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	case hwmon_fan:
		ret = qnap_mcu_hwmon_get_rpm(hwm);
		if (ret < 0)
			return ret;

		*val = ret;
		return 0;
	case hwmon_temp:
		ret = qnap_mcu_hwmon_get_temp(hwm);
		if (ret < 0)
			return ret;

		*val = ret;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t qnap_mcu_hwmon_is_visible(const void *data,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		return 0444;

	case hwmon_pwm:
		return 0644;

	case hwmon_fan:
		return 0444;

	default:
		return 0;
	}
}

static const struct hwmon_ops qnap_mcu_hwmon_hwmon_ops = {
	.is_visible = qnap_mcu_hwmon_is_visible,
	.read = qnap_mcu_hwmon_read,
	.write = qnap_mcu_hwmon_write,
};

/* thermal cooling device callbacks */
static int qnap_mcu_hwmon_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct qnap_mcu_hwmon *hwm = cdev->devdata;

	if (!hwm)
		return -EINVAL;

	*state = hwm->fan_max_state;

	return 0;
}

static int qnap_mcu_hwmon_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct qnap_mcu_hwmon *hwm = cdev->devdata;

	if (!hwm)
		return -EINVAL;

	*state = hwm->fan_state;

	return 0;
}

static int qnap_mcu_hwmon_set_cur_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct qnap_mcu_hwmon *hwm = cdev->devdata;
	int ret;

	if (!hwm || state > hwm->fan_max_state)
		return -EINVAL;

	if (state == hwm->fan_state)
		return 0;

	ret = qnap_mcu_hwmon_set_pwm(hwm, hwm->fan_cooling_levels[state]);
	if (ret)
		return ret;

	hwm->fan_state = state;

	return ret;
}

static const struct thermal_cooling_device_ops qnap_mcu_hwmon_cooling_ops = {
	.get_max_state = qnap_mcu_hwmon_get_max_state,
	.get_cur_state = qnap_mcu_hwmon_get_cur_state,
	.set_cur_state = qnap_mcu_hwmon_set_cur_state,
};

static void devm_fan_node_release(void *data)
{
	struct qnap_mcu_hwmon *hwm = data;

	fwnode_handle_put(hwm->fan_node);
}

static int qnap_mcu_hwmon_get_cooling_data(struct device *dev, struct qnap_mcu_hwmon *hwm)
{
	struct fwnode_handle *fwnode;
	int num, i, ret;

	fwnode = device_get_named_child_node(dev->parent, "fan-0");
	if (!fwnode)
		return 0;

	/* if we found the fan-node, we're keeping it until device-unbind */
	hwm->fan_node = fwnode;
	ret = devm_add_action_or_reset(dev, devm_fan_node_release, hwm);
	if (ret)
		return ret;

	num = fwnode_property_count_u32(fwnode, "cooling-levels");
	if (num <= 0)
		return dev_err_probe(dev, num ? : -EINVAL,
				     "Failed to count elements in 'cooling-levels'\n");

	hwm->fan_cooling_levels = devm_kcalloc(dev, num, sizeof(u32),
					       GFP_KERNEL);
	if (!hwm->fan_cooling_levels)
		return -ENOMEM;

	ret = fwnode_property_read_u32_array(fwnode, "cooling-levels",
					     hwm->fan_cooling_levels, num);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read 'cooling-levels'\n");

	for (i = 0; i < num; i++) {
		if (hwm->fan_cooling_levels[i] > hwm->pwm_max)
			return dev_err_probe(dev, -EINVAL, "fan state[%d]:%d > %d\n", i,
					     hwm->fan_cooling_levels[i], hwm->pwm_max);
	}

	hwm->fan_max_state = num - 1;

	return 0;
}

static const struct hwmon_channel_info * const qnap_mcu_hwmon_channels[] = {
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static int qnap_mcu_hwmon_probe(struct platform_device *pdev)
{
	struct qnap_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	const struct qnap_mcu_variant *variant = pdev->dev.platform_data;
	struct qnap_mcu_hwmon *hwm;
	struct thermal_cooling_device *cdev;
	struct device *dev = &pdev->dev;
	struct device *hwmon;
	int ret;

	hwm = devm_kzalloc(dev, sizeof(*hwm), GFP_KERNEL);
	if (!hwm)
		return -ENOMEM;

	hwm->mcu = mcu;
	hwm->dev = &pdev->dev;
	hwm->pwm_min = variant->fan_pwm_min;
	hwm->pwm_max = variant->fan_pwm_max;

	platform_set_drvdata(pdev, hwm);

	/*
	 * Set duty cycle to maximum allowed.
	 */
	ret = qnap_mcu_hwmon_set_pwm(hwm, hwm->pwm_max);
	if (ret)
		return ret;

	hwm->info.ops = &qnap_mcu_hwmon_hwmon_ops;
	hwm->info.info = qnap_mcu_hwmon_channels;

	ret = qnap_mcu_hwmon_get_cooling_data(dev, hwm);
	if (ret)
		return ret;

	hwm->fan_state = hwm->fan_max_state;

	hwmon = devm_hwmon_device_register_with_info(dev, "qnapmcu",
						     hwm, &hwm->info, NULL);
	if (IS_ERR(hwmon))
		return dev_err_probe(dev, PTR_ERR(hwmon), "Failed to register hwmon device\n");

	/*
	 * Only register cooling device when we found cooling-levels.
	 * qnap_mcu_hwmon_get_cooling_data() will fail when reading malformed
	 * levels and only succeed with either no or correct cooling levels.
	 */
	if (IS_ENABLED(CONFIG_THERMAL) && hwm->fan_cooling_levels) {
		cdev = devm_thermal_of_cooling_device_register(dev,
					to_of_node(hwm->fan_node), "qnap-mcu-hwmon",
					hwm, &qnap_mcu_hwmon_cooling_ops);
		if (IS_ERR(cdev))
			return dev_err_probe(dev, PTR_ERR(cdev),
				"Failed to register qnap-mcu-hwmon as cooling device\n");
		hwm->cdev = cdev;
	}

	return 0;
}

static struct platform_driver qnap_mcu_hwmon_driver = {
	.probe = qnap_mcu_hwmon_probe,
	.driver = {
		.name = "qnap-mcu-hwmon",
	},
};
module_platform_driver(qnap_mcu_hwmon_driver);

MODULE_ALIAS("platform:qnap-mcu-hwmon");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU hwmon driver");
MODULE_LICENSE("GPL");
