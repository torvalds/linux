/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include "nouveau_drv.h"
#include "nouveau_hwmon.h"

#include <nvkm/subdev/iccsense.h>
#include <nvkm/subdev/volt.h>

#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))

static ssize_t
nouveau_hwmon_show_temp1_auto_point1_pwm(struct device *d,
					 struct device_attribute *a, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 100);
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_pwm, 0444,
			  nouveau_hwmon_show_temp1_auto_point1_pwm, NULL, 0);

static ssize_t
nouveau_hwmon_temp1_auto_point1_temp(struct device *d,
				     struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	      therm->attr_get(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST) * 1000);
}
static ssize_t
nouveau_hwmon_set_temp1_auto_point1_temp(struct device *d,
					 struct device_attribute *a,
					 const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	long value;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	therm->attr_set(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_temp, 0644,
			  nouveau_hwmon_temp1_auto_point1_temp,
			  nouveau_hwmon_set_temp1_auto_point1_temp, 0);

static ssize_t
nouveau_hwmon_temp1_auto_point1_temp_hyst(struct device *d,
					  struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	 therm->attr_get(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST) * 1000);
}
static ssize_t
nouveau_hwmon_set_temp1_auto_point1_temp_hyst(struct device *d,
					      struct device_attribute *a,
					      const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	long value;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	therm->attr_set(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_temp_hyst, 0644,
			  nouveau_hwmon_temp1_auto_point1_temp_hyst,
			  nouveau_hwmon_set_temp1_auto_point1_temp_hyst, 0);

static ssize_t
nouveau_hwmon_get_pwm1_max(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	int ret;

	ret = therm->attr_get(therm, NVKM_THERM_ATTR_FAN_MAX_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_get_pwm1_min(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	int ret;

	ret = therm->attr_get(therm, NVKM_THERM_ATTR_FAN_MIN_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1_min(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	long value;
	int ret;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	ret = therm->attr_set(therm, NVKM_THERM_ATTR_FAN_MIN_DUTY, value);
	if (ret < 0)
		return ret;

	return count;
}
static SENSOR_DEVICE_ATTR(pwm1_min, 0644,
			  nouveau_hwmon_get_pwm1_min,
			  nouveau_hwmon_set_pwm1_min, 0);

static ssize_t
nouveau_hwmon_set_pwm1_max(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	long value;
	int ret;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	ret = therm->attr_set(therm, NVKM_THERM_ATTR_FAN_MAX_DUTY, value);
	if (ret < 0)
		return ret;

	return count;
}
static SENSOR_DEVICE_ATTR(pwm1_max, 0644,
			  nouveau_hwmon_get_pwm1_max,
			  nouveau_hwmon_set_pwm1_max, 0);

static struct attribute *pwm_fan_sensor_attrs[] = {
	&sensor_dev_attr_pwm1_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_max.dev_attr.attr,
	NULL
};
static const struct attribute_group pwm_fan_sensor_group = {
	.attrs = pwm_fan_sensor_attrs,
};

static struct attribute *temp1_auto_point_sensor_attrs[] = {
	&sensor_dev_attr_temp1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp_hyst.dev_attr.attr,
	NULL
};
static const struct attribute_group temp1_auto_point_sensor_group = {
	.attrs = temp1_auto_point_sensor_attrs,
};

#define N_ATTR_GROUPS   3

static const u32 nouveau_config_chip[] = {
	HWMON_C_UPDATE_INTERVAL,
	0
};

static const u32 nouveau_config_in[] = {
	HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_LABEL,
	0
};

static const u32 nouveau_config_temp[] = {
	HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
	HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_EMERGENCY |
	HWMON_T_EMERGENCY_HYST,
	0
};

static const u32 nouveau_config_fan[] = {
	HWMON_F_INPUT,
	0
};

static const u32 nouveau_config_pwm[] = {
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	0
};

static const u32 nouveau_config_power[] = {
	HWMON_P_INPUT | HWMON_P_CAP_MAX | HWMON_P_CRIT,
	0
};

static const struct hwmon_channel_info nouveau_chip = {
	.type = hwmon_chip,
	.config = nouveau_config_chip,
};

static const struct hwmon_channel_info nouveau_temp = {
	.type = hwmon_temp,
	.config = nouveau_config_temp,
};

static const struct hwmon_channel_info nouveau_fan = {
	.type = hwmon_fan,
	.config = nouveau_config_fan,
};

static const struct hwmon_channel_info nouveau_in = {
	.type = hwmon_in,
	.config = nouveau_config_in,
};

static const struct hwmon_channel_info nouveau_pwm = {
	.type = hwmon_pwm,
	.config = nouveau_config_pwm,
};

static const struct hwmon_channel_info nouveau_power = {
	.type = hwmon_power,
	.config = nouveau_config_power,
};

static const struct hwmon_channel_info *nouveau_info[] = {
	&nouveau_chip,
	&nouveau_temp,
	&nouveau_fan,
	&nouveau_in,
	&nouveau_pwm,
	&nouveau_power,
	NULL
};

static umode_t
nouveau_chip_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return 0444;
	default:
		return 0;
	}
}

static umode_t
nouveau_power_is_visible(const void *data, u32 attr, int channel)
{
	struct nouveau_drm *drm = nouveau_drm((struct drm_device *)data);
	struct nvkm_iccsense *iccsense = nvxx_iccsense(&drm->client.device);

	if (!iccsense || !iccsense->data_valid || list_empty(&iccsense->rails))
		return 0;

	switch (attr) {
	case hwmon_power_input:
		return 0444;
	case hwmon_power_max:
		if (iccsense->power_w_max)
			return 0444;
		return 0;
	case hwmon_power_crit:
		if (iccsense->power_w_crit)
			return 0444;
		return 0;
	default:
		return 0;
	}
}

static umode_t
nouveau_temp_is_visible(const void *data, u32 attr, int channel)
{
	struct nouveau_drm *drm = nouveau_drm((struct drm_device *)data);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_get || nvkm_therm_temp_get(therm) < 0)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	case hwmon_temp_max:
	case hwmon_temp_max_hyst:
	case hwmon_temp_crit:
	case hwmon_temp_crit_hyst:
	case hwmon_temp_emergency:
	case hwmon_temp_emergency_hyst:
		return 0644;
	default:
		return 0;
	}
}

static umode_t
nouveau_pwm_is_visible(const void *data, u32 attr, int channel)
{
	struct nouveau_drm *drm = nouveau_drm((struct drm_device *)data);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_get || !therm->fan_get ||
	    therm->fan_get(therm) < 0)
		return 0;

	switch (attr) {
	case hwmon_pwm_enable:
	case hwmon_pwm_input:
		return 0644;
	default:
		return 0;
	}
}

static umode_t
nouveau_input_is_visible(const void *data, u32 attr, int channel)
{
	struct nouveau_drm *drm = nouveau_drm((struct drm_device *)data);
	struct nvkm_volt *volt = nvxx_volt(&drm->client.device);

	if (!volt || nvkm_volt_get(volt) < 0)
		return 0;

	switch (attr) {
	case hwmon_in_input:
	case hwmon_in_label:
	case hwmon_in_min:
	case hwmon_in_max:
		return 0444;
	default:
		return 0;
	}
}

static umode_t
nouveau_fan_is_visible(const void *data, u32 attr, int channel)
{
	struct nouveau_drm *drm = nouveau_drm((struct drm_device *)data);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_get || nvkm_therm_fan_sense(therm) < 0)
		return 0;

	switch (attr) {
	case hwmon_fan_input:
		return 0444;
	default:
		return 0;
	}
}

static int
nouveau_chip_read(struct device *dev, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		*val = 1000;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nouveau_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	int ret;

	if (!therm || !therm->attr_get)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		ret = nvkm_therm_temp_get(therm);
		*val = ret < 0 ? ret : (ret * 1000);
		break;
	case hwmon_temp_max:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_THRS_DOWN_CLK)
					* 1000;
		break;
	case hwmon_temp_max_hyst:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST)
					* 1000;
		break;
	case hwmon_temp_crit:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_THRS_CRITICAL)
					* 1000;
		break;
	case hwmon_temp_crit_hyst:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_THRS_CRITICAL_HYST)
					* 1000;
		break;
	case hwmon_temp_emergency:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_THRS_SHUTDOWN)
					* 1000;
		break;
	case hwmon_temp_emergency_hyst:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST)
					* 1000;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nouveau_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_fan_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		*val = nvkm_therm_fan_sense(therm);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nouveau_in_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_volt *volt = nvxx_volt(&drm->client.device);
	int ret;

	if (!volt)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_in_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		ret = nvkm_volt_get(volt);
		*val = ret < 0 ? ret : (ret / 1000);
		break;
	case hwmon_in_min:
		*val = volt->min_uv > 0 ? (volt->min_uv / 1000) : -ENODEV;
		break;
	case hwmon_in_max:
		*val = volt->max_uv > 0 ? (volt->max_uv / 1000) : -ENODEV;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nouveau_pwm_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_get || !therm->fan_get)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_pwm_enable:
		*val = therm->attr_get(therm, NVKM_THERM_ATTR_FAN_MODE);
		break;
	case hwmon_pwm_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		*val = therm->fan_get(therm);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nouveau_power_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_iccsense *iccsense = nvxx_iccsense(&drm->client.device);

	if (!iccsense)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_power_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		*val = nvkm_iccsense_read_all(iccsense);
		break;
	case hwmon_power_max:
		*val = iccsense->power_w_max;
		break;
	case hwmon_power_crit:
		*val = iccsense->power_w_crit;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nouveau_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_set)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_max:
		return therm->attr_set(therm, NVKM_THERM_ATTR_THRS_DOWN_CLK,
					val / 1000);
	case hwmon_temp_max_hyst:
		return therm->attr_set(therm, NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST,
					val / 1000);
	case hwmon_temp_crit:
		return therm->attr_set(therm, NVKM_THERM_ATTR_THRS_CRITICAL,
					val / 1000);
	case hwmon_temp_crit_hyst:
		return therm->attr_set(therm, NVKM_THERM_ATTR_THRS_CRITICAL_HYST,
					val / 1000);
	case hwmon_temp_emergency:
		return therm->attr_set(therm, NVKM_THERM_ATTR_THRS_SHUTDOWN,
					val / 1000);
	case hwmon_temp_emergency_hyst:
		return therm->attr_set(therm, NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST,
					val / 1000);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nouveau_pwm_write(struct device *dev, u32 attr, int channel, long val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_set)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_pwm_input:
		return therm->fan_set(therm, val);
	case hwmon_pwm_enable:
		return therm->attr_set(therm, NVKM_THERM_ATTR_FAN_MODE, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
nouveau_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr,
			int channel)
{
	switch (type) {
	case hwmon_chip:
		return nouveau_chip_is_visible(data, attr, channel);
	case hwmon_temp:
		return nouveau_temp_is_visible(data, attr, channel);
	case hwmon_fan:
		return nouveau_fan_is_visible(data, attr, channel);
	case hwmon_in:
		return nouveau_input_is_visible(data, attr, channel);
	case hwmon_pwm:
		return nouveau_pwm_is_visible(data, attr, channel);
	case hwmon_power:
		return nouveau_power_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const char input_label[] = "GPU core";

static int
nouveau_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		    int channel, const char **buf)
{
	if (type == hwmon_in && attr == hwmon_in_label) {
		*buf = input_label;
		return 0;
	}

	return -EOPNOTSUPP;
}

static int
nouveau_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
							int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return nouveau_chip_read(dev, attr, channel, val);
	case hwmon_temp:
		return nouveau_temp_read(dev, attr, channel, val);
	case hwmon_fan:
		return nouveau_fan_read(dev, attr, channel, val);
	case hwmon_in:
		return nouveau_in_read(dev, attr, channel, val);
	case hwmon_pwm:
		return nouveau_pwm_read(dev, attr, channel, val);
	case hwmon_power:
		return nouveau_power_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nouveau_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
							int channel, long val)
{
	switch (type) {
	case hwmon_temp:
		return nouveau_temp_write(dev, attr, channel, val);
	case hwmon_pwm:
		return nouveau_pwm_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops nouveau_hwmon_ops = {
	.is_visible = nouveau_is_visible,
	.read = nouveau_read,
	.read_string = nouveau_read_string,
	.write = nouveau_write,
};

static const struct hwmon_chip_info nouveau_chip_info = {
	.ops = &nouveau_hwmon_ops,
	.info = nouveau_info,
};
#endif

int
nouveau_hwmon_init(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_iccsense *iccsense = nvxx_iccsense(&drm->client.device);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	struct nvkm_volt *volt = nvxx_volt(&drm->client.device);
	const struct attribute_group *special_groups[N_ATTR_GROUPS];
	struct nouveau_hwmon *hwmon;
	struct device *hwmon_dev;
	int ret = 0;
	int i = 0;

	if (!iccsense && !therm && !volt) {
		NV_DEBUG(drm, "Skipping hwmon registration\n");
		return 0;
	}

	hwmon = drm->hwmon = kzalloc(sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;
	hwmon->dev = dev;

	if (therm && therm->attr_get && therm->attr_set) {
		if (nvkm_therm_temp_get(therm) >= 0)
			special_groups[i++] = &temp1_auto_point_sensor_group;
		if (therm->fan_get && therm->fan_get(therm) >= 0)
			special_groups[i++] = &pwm_fan_sensor_group;
	}

	special_groups[i] = NULL;
	hwmon_dev = hwmon_device_register_with_info(dev->dev, "nouveau", dev,
							&nouveau_chip_info,
							special_groups);
	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		NV_ERROR(drm, "Unable to register hwmon device: %d\n", ret);
		return ret;
	}

	hwmon->hwmon = hwmon_dev;
	return 0;
#else
	return 0;
#endif
}

void
nouveau_hwmon_fini(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_hwmon *hwmon = nouveau_hwmon(dev);

	if (!hwmon)
		return;

	if (hwmon->hwmon)
		hwmon_device_unregister(hwmon->hwmon);

	nouveau_drm(dev)->hwmon = NULL;
	kfree(hwmon);
#endif
}
