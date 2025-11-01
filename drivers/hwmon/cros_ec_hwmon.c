// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ChromeOS EC driver for hwmon
 *
 *  Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */

#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/units.h>

#define DRV_NAME	"cros-ec-hwmon"

#define CROS_EC_HWMON_PWM_GET_FAN_DUTY_CMD_VERSION	0
#define CROS_EC_HWMON_PWM_SET_FAN_DUTY_CMD_VERSION	1
#define CROS_EC_HWMON_THERMAL_AUTO_FAN_CTRL_CMD_VERSION	2

struct cros_ec_hwmon_priv {
	struct cros_ec_device *cros_ec;
	const char *temp_sensor_names[EC_TEMP_SENSOR_ENTRIES + EC_TEMP_SENSOR_B_ENTRIES];
	u8 usable_fans;
	bool fan_control_supported;
	u8 manual_fans; /* bits to indicate whether the fan is set to manual */
	u8 manual_fan_pwm[EC_FAN_SPEED_ENTRIES];
};

struct cros_ec_hwmon_cooling_priv {
	struct cros_ec_hwmon_priv *hwmon_priv;
	u8 index;
};

static int cros_ec_hwmon_read_fan_speed(struct cros_ec_device *cros_ec, u8 index, u16 *speed)
{
	int ret;
	__le16 __speed;

	ret = cros_ec_cmd_readmem(cros_ec, EC_MEMMAP_FAN + index * 2, 2, &__speed);
	if (ret < 0)
		return ret;

	*speed = le16_to_cpu(__speed);
	return 0;
}

static int cros_ec_hwmon_read_pwm_value(struct cros_ec_device *cros_ec, u8 index, u8 *pwm_value)
{
	struct ec_params_pwm_get_fan_duty req = {
		.fan_idx = index,
	};
	struct ec_response_pwm_get_fan_duty resp;
	int ret;

	ret = cros_ec_cmd(cros_ec, CROS_EC_HWMON_PWM_GET_FAN_DUTY_CMD_VERSION,
			  EC_CMD_PWM_GET_FAN_DUTY, &req, sizeof(req), &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	*pwm_value = (u8)DIV_ROUND_CLOSEST(le32_to_cpu(resp.percent) * 255, 100);
	return 0;
}

static int cros_ec_hwmon_read_pwm_enable(struct cros_ec_device *cros_ec, u8 index,
					 u8 *control_method)
{
	struct ec_params_auto_fan_ctrl_v2 req = {
		.cmd = EC_AUTO_FAN_CONTROL_CMD_GET,
		.fan_idx = index,
	};
	struct ec_response_auto_fan_control resp;
	int ret;

	ret = cros_ec_cmd(cros_ec, CROS_EC_HWMON_THERMAL_AUTO_FAN_CTRL_CMD_VERSION,
			  EC_CMD_THERMAL_AUTO_FAN_CTRL, &req, sizeof(req), &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	*control_method = resp.is_auto ? 2 : 1;
	return 0;
}

static int cros_ec_hwmon_read_temp(struct cros_ec_device *cros_ec, u8 index, u8 *temp)
{
	unsigned int offset;
	int ret;

	if (index < EC_TEMP_SENSOR_ENTRIES)
		offset = EC_MEMMAP_TEMP_SENSOR + index;
	else
		offset = EC_MEMMAP_TEMP_SENSOR_B + index - EC_TEMP_SENSOR_ENTRIES;

	ret = cros_ec_cmd_readmem(cros_ec, offset, 1, temp);
	if (ret < 0)
		return ret;
	return 0;
}

static bool cros_ec_hwmon_is_error_fan(u16 speed)
{
	return speed == EC_FAN_SPEED_NOT_PRESENT || speed == EC_FAN_SPEED_STALLED;
}

static bool cros_ec_hwmon_is_error_temp(u8 temp)
{
	return temp == EC_TEMP_SENSOR_NOT_PRESENT     ||
	       temp == EC_TEMP_SENSOR_ERROR           ||
	       temp == EC_TEMP_SENSOR_NOT_POWERED     ||
	       temp == EC_TEMP_SENSOR_NOT_CALIBRATED;
}

static long cros_ec_hwmon_temp_to_millicelsius(u8 temp)
{
	return kelvin_to_millicelsius((((long)temp) + EC_TEMP_SENSOR_OFFSET));
}

static int cros_ec_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct cros_ec_hwmon_priv *priv = dev_get_drvdata(dev);
	int ret = -EOPNOTSUPP;
	u8 control_method;
	u8 pwm_value;
	u16 speed;
	u8 temp;

	if (type == hwmon_fan) {
		if (attr == hwmon_fan_input) {
			ret = cros_ec_hwmon_read_fan_speed(priv->cros_ec, channel, &speed);
			if (ret == 0) {
				if (cros_ec_hwmon_is_error_fan(speed))
					ret = -ENODATA;
				else
					*val = speed;
			}
		} else if (attr == hwmon_fan_fault) {
			ret = cros_ec_hwmon_read_fan_speed(priv->cros_ec, channel, &speed);
			if (ret == 0)
				*val = cros_ec_hwmon_is_error_fan(speed);
		}
	} else if (type == hwmon_pwm) {
		if (attr == hwmon_pwm_enable) {
			ret = cros_ec_hwmon_read_pwm_enable(priv->cros_ec, channel,
							    &control_method);
			if (ret == 0)
				*val = control_method;
		} else if (attr == hwmon_pwm_input) {
			ret = cros_ec_hwmon_read_pwm_value(priv->cros_ec, channel, &pwm_value);
			if (ret == 0)
				*val = pwm_value;
		}
	} else if (type == hwmon_temp) {
		if (attr == hwmon_temp_input) {
			ret = cros_ec_hwmon_read_temp(priv->cros_ec, channel, &temp);
			if (ret == 0) {
				if (cros_ec_hwmon_is_error_temp(temp))
					ret = -ENODATA;
				else
					*val = cros_ec_hwmon_temp_to_millicelsius(temp);
			}
		} else if (attr == hwmon_temp_fault) {
			ret = cros_ec_hwmon_read_temp(priv->cros_ec, channel, &temp);
			if (ret == 0)
				*val = cros_ec_hwmon_is_error_temp(temp);
		}
	}

	return ret;
}

static int cros_ec_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				     u32 attr, int channel, const char **str)
{
	struct cros_ec_hwmon_priv *priv = dev_get_drvdata(dev);

	if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = priv->temp_sensor_names[channel];
		return 0;
	}

	return -EOPNOTSUPP;
}

static int cros_ec_hwmon_set_fan_pwm_val(struct cros_ec_device *cros_ec, u8 index, u8 val)
{
	struct ec_params_pwm_set_fan_duty_v1 req = {
		.fan_idx = index,
		.percent = DIV_ROUND_CLOSEST((uint32_t)val * 100, 255),
	};
	int ret;

	ret = cros_ec_cmd(cros_ec, CROS_EC_HWMON_PWM_SET_FAN_DUTY_CMD_VERSION,
			  EC_CMD_PWM_SET_FAN_DUTY, &req, sizeof(req), NULL, 0);
	if (ret < 0)
		return ret;
	return 0;
}

static int cros_ec_hwmon_write_pwm_input(struct cros_ec_device *cros_ec, u8 index, u8 val)
{
	u8 control_method;
	int ret;

	ret = cros_ec_hwmon_read_pwm_enable(cros_ec, index, &control_method);
	if (ret)
		return ret;
	if (control_method != 1)
		return -EOPNOTSUPP;

	return cros_ec_hwmon_set_fan_pwm_val(cros_ec, index, val);
}

static int cros_ec_hwmon_write_pwm_enable(struct cros_ec_device *cros_ec, u8 index, u8 val)
{
	struct ec_params_auto_fan_ctrl_v2 req = {
		.fan_idx = index,
		.cmd = EC_AUTO_FAN_CONTROL_CMD_SET,
	};
	int ret;

	/* No CrOS EC supports no fan speed control */
	if (val == 0)
		return -EOPNOTSUPP;

	req.set_auto = (val != 1) ? true : false;
	ret = cros_ec_cmd(cros_ec, CROS_EC_HWMON_THERMAL_AUTO_FAN_CTRL_CMD_VERSION,
			  EC_CMD_THERMAL_AUTO_FAN_CTRL, &req, sizeof(req), NULL, 0);
	if (ret < 0)
		return ret;
	return 0;
}

static int cros_ec_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			       int channel, long val)
{
	struct cros_ec_hwmon_priv *priv = dev_get_drvdata(dev);

	if (type == hwmon_pwm) {
		switch (attr) {
		case hwmon_pwm_input:
			return cros_ec_hwmon_write_pwm_input(priv->cros_ec, channel, val);
		case hwmon_pwm_enable:
			return cros_ec_hwmon_write_pwm_enable(priv->cros_ec, channel, val);
		default:
			return -EOPNOTSUPP;
		}
	}

	return -EOPNOTSUPP;
}

static umode_t cros_ec_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	const struct cros_ec_hwmon_priv *priv = data;

	if (type == hwmon_fan) {
		if (priv->usable_fans & BIT(channel))
			return 0444;
	} else if (type == hwmon_pwm) {
		if (priv->fan_control_supported && priv->usable_fans & BIT(channel))
			return 0644;
	} else if (type == hwmon_temp) {
		if (priv->temp_sensor_names[channel])
			return 0444;
	}

	return 0;
}

static const struct hwmon_channel_info * const cros_ec_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL),
	NULL
};

static int cros_ec_hwmon_cooling_get_max_state(struct thermal_cooling_device *cdev,
					       unsigned long *val)
{
	*val = 255;
	return 0;
}

static int cros_ec_hwmon_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					       unsigned long *val)
{
	const struct cros_ec_hwmon_cooling_priv *priv = cdev->devdata;
	u8 read_val;
	int ret;

	ret = cros_ec_hwmon_read_pwm_value(priv->hwmon_priv->cros_ec, priv->index, &read_val);
	if (ret)
		return ret;

	*val = read_val;
	return 0;
}

static int cros_ec_hwmon_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					       unsigned long val)
{
	const struct cros_ec_hwmon_cooling_priv *priv = cdev->devdata;

	return cros_ec_hwmon_write_pwm_input(priv->hwmon_priv->cros_ec, priv->index, val);
}

static const struct thermal_cooling_device_ops cros_ec_thermal_cooling_ops = {
	.get_max_state = cros_ec_hwmon_cooling_get_max_state,
	.get_cur_state = cros_ec_hwmon_cooling_get_cur_state,
	.set_cur_state = cros_ec_hwmon_cooling_set_cur_state,
};

static const struct hwmon_ops cros_ec_hwmon_ops = {
	.read = cros_ec_hwmon_read,
	.read_string = cros_ec_hwmon_read_string,
	.write = cros_ec_hwmon_write,
	.is_visible = cros_ec_hwmon_is_visible,
};

static const struct hwmon_chip_info cros_ec_hwmon_chip_info = {
	.ops = &cros_ec_hwmon_ops,
	.info = cros_ec_hwmon_info,
};

static void cros_ec_hwmon_probe_temp_sensors(struct device *dev, struct cros_ec_hwmon_priv *priv,
					     u8 thermal_version)
{
	struct ec_params_temp_sensor_get_info req = {};
	struct ec_response_temp_sensor_get_info resp;
	size_t candidates, i, sensor_name_size;
	int ret;
	u8 temp;

	if (thermal_version < 2)
		candidates = EC_TEMP_SENSOR_ENTRIES;
	else
		candidates = ARRAY_SIZE(priv->temp_sensor_names);

	for (i = 0; i < candidates; i++) {
		if (cros_ec_hwmon_read_temp(priv->cros_ec, i, &temp) < 0)
			continue;

		if (temp == EC_TEMP_SENSOR_NOT_PRESENT)
			continue;

		req.id = i;
		ret = cros_ec_cmd(priv->cros_ec, 0, EC_CMD_TEMP_SENSOR_GET_INFO,
				  &req, sizeof(req), &resp, sizeof(resp));
		if (ret < 0)
			continue;

		sensor_name_size = strnlen(resp.sensor_name, sizeof(resp.sensor_name));
		priv->temp_sensor_names[i] = devm_kasprintf(dev, GFP_KERNEL, "%.*s",
							    (int)sensor_name_size,
							    resp.sensor_name);
	}
}

static void cros_ec_hwmon_probe_fans(struct cros_ec_hwmon_priv *priv)
{
	u16 speed;
	size_t i;
	int ret;

	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		ret = cros_ec_hwmon_read_fan_speed(priv->cros_ec, i, &speed);
		if (ret == 0 && speed != EC_FAN_SPEED_NOT_PRESENT)
			priv->usable_fans |= BIT(i);
	}
}

static inline bool is_cros_ec_cmd_available(struct cros_ec_device *cros_ec,
					    u16 cmd, u8 version)
{
	int ret;

	ret = cros_ec_get_cmd_versions(cros_ec, cmd);
	return ret >= 0 && (ret & EC_VER_MASK(version));
}

static bool cros_ec_hwmon_probe_fan_control_supported(struct cros_ec_device *cros_ec)
{
	return is_cros_ec_cmd_available(cros_ec, EC_CMD_PWM_GET_FAN_DUTY,
					CROS_EC_HWMON_PWM_GET_FAN_DUTY_CMD_VERSION) &&
	       is_cros_ec_cmd_available(cros_ec, EC_CMD_PWM_SET_FAN_DUTY,
					CROS_EC_HWMON_PWM_SET_FAN_DUTY_CMD_VERSION) &&
	       is_cros_ec_cmd_available(cros_ec, EC_CMD_THERMAL_AUTO_FAN_CTRL,
					CROS_EC_HWMON_THERMAL_AUTO_FAN_CTRL_CMD_VERSION);
}

static void cros_ec_hwmon_register_fan_cooling_devices(struct device *dev,
						       struct cros_ec_hwmon_priv *priv)
{
	struct cros_ec_hwmon_cooling_priv *cpriv;
	struct thermal_cooling_device *cdev;
	const char *type;
	size_t i;

	if (!IS_ENABLED(CONFIG_THERMAL))
		return;

	if (!priv->fan_control_supported)
		return;

	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		if (!(priv->usable_fans & BIT(i)))
			continue;

		cpriv = devm_kzalloc(dev, sizeof(*cpriv), GFP_KERNEL);
		if (!cpriv)
			continue;

		type = devm_kasprintf(dev, GFP_KERNEL, "%s-fan%zu", dev_name(dev), i);
		if (!type) {
			dev_warn(dev, "no memory to compose cooling device type for fan %zu\n", i);
			continue;
		}

		cpriv->hwmon_priv = priv;
		cpriv->index = i;
		cdev = devm_thermal_of_cooling_device_register(dev, NULL, type, cpriv,
							       &cros_ec_thermal_cooling_ops);
		if (IS_ERR(cdev)) {
			dev_warn(dev, "failed to register fan %zu as a cooling device: %pe\n", i,
				 cdev);
			continue;
		}
	}
}

static int cros_ec_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct cros_ec_hwmon_priv *priv;
	struct device *hwmon_dev;
	u8 thermal_version;
	int ret;

	ret = cros_ec_cmd_readmem(cros_ec, EC_MEMMAP_THERMAL_VERSION, 1, &thermal_version);
	if (ret < 0)
		return ret;

	/* Covers both fan and temp sensors */
	if (thermal_version == 0)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->cros_ec = cros_ec;

	cros_ec_hwmon_probe_temp_sensors(dev, priv, thermal_version);
	cros_ec_hwmon_probe_fans(priv);
	priv->fan_control_supported = cros_ec_hwmon_probe_fan_control_supported(priv->cros_ec);
	cros_ec_hwmon_register_fan_cooling_devices(dev, priv);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "cros_ec", priv,
							 &cros_ec_hwmon_chip_info, NULL);
	platform_set_drvdata(pdev, priv);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int cros_ec_hwmon_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct cros_ec_hwmon_priv *priv = platform_get_drvdata(pdev);
	u8 control_method;
	size_t i;
	int ret;

	if (!priv->fan_control_supported)
		return 0;

	/* EC sets fan control to auto after suspended, store settings before suspending. */
	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		if (!(priv->usable_fans & BIT(i)))
			continue;

		ret = cros_ec_hwmon_read_pwm_enable(priv->cros_ec, i, &control_method);
		if (ret) {
			dev_warn(&pdev->dev, "failed to get mode setting for fan %zu: %d\n", i,
				 ret);
			continue;
		}

		if (control_method != 1) {
			priv->manual_fans &= ~BIT(i);
			continue;
		} else {
			priv->manual_fans |= BIT(i);
		}

		ret = cros_ec_hwmon_read_pwm_value(priv->cros_ec, i, &priv->manual_fan_pwm[i]);
		/*
		 * If storing the value failed, invalidate the stored mode value by setting it
		 * to auto control. EC will automatically switch to auto mode for that fan after
		 * suspended.
		 */
		if (ret) {
			dev_warn(&pdev->dev, "failed to get PWM setting for fan %zu: %pe\n", i,
				 ERR_PTR(ret));
			priv->manual_fans &= ~BIT(i);
			continue;
		}
	}

	return 0;
}

static int cros_ec_hwmon_resume(struct platform_device *pdev)
{
	const struct cros_ec_hwmon_priv *priv = platform_get_drvdata(pdev);
	size_t i;
	int ret;

	if (!priv->fan_control_supported)
		return 0;

	/* EC sets fan control to auto after suspend, restore to settings before suspend. */
	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		if (!(priv->manual_fans & BIT(i)))
			continue;

		/*
		 * Setting fan PWM value to EC will change the mode to manual for that fan in EC as
		 * well, so we do not need to issue a separate fan mode to manual call.
		 */
		ret = cros_ec_hwmon_set_fan_pwm_val(priv->cros_ec, i, priv->manual_fan_pwm[i]);
		if (ret)
			dev_warn(&pdev->dev, "failed to restore settings for fan %zu: %pe\n", i,
				 ERR_PTR(ret));
	}

	return 0;
}

static const struct platform_device_id cros_ec_hwmon_id[] = {
	{ DRV_NAME, 0 },
	{}
};

static struct platform_driver cros_ec_hwmon_driver = {
	.driver.name	= DRV_NAME,
	.probe		= cros_ec_hwmon_probe,
	.suspend	= pm_ptr(cros_ec_hwmon_suspend),
	.resume		= pm_ptr(cros_ec_hwmon_resume),
	.id_table	= cros_ec_hwmon_id,
};
module_platform_driver(cros_ec_hwmon_driver);

MODULE_DEVICE_TABLE(platform, cros_ec_hwmon_id);
MODULE_DESCRIPTION("ChromeOS EC Hardware Monitoring Driver");
MODULE_AUTHOR("Thomas Weißschuh <linux@weissschuh.net");
MODULE_LICENSE("GPL");
