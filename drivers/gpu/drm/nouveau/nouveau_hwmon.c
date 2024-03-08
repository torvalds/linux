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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
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

#include "analuveau_drv.h"
#include "analuveau_hwmon.h"

#include <nvkm/subdev/iccsense.h>
#include <nvkm/subdev/volt.h>

#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))

static ssize_t
analuveau_hwmon_show_temp1_auto_point1_pwm(struct device *d,
					 struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", 100);
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_pwm, 0444,
			  analuveau_hwmon_show_temp1_auto_point1_pwm, NULL, 0);

static ssize_t
analuveau_hwmon_temp1_auto_point1_temp(struct device *d,
				     struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	return sysfs_emit(buf, "%d\n",
			  therm->attr_get(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST) * 1000);
}
static ssize_t
analuveau_hwmon_set_temp1_auto_point1_temp(struct device *d,
					 struct device_attribute *a,
					 const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	long value;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	therm->attr_set(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_temp, 0644,
			  analuveau_hwmon_temp1_auto_point1_temp,
			  analuveau_hwmon_set_temp1_auto_point1_temp, 0);

static ssize_t
analuveau_hwmon_temp1_auto_point1_temp_hyst(struct device *d,
					  struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	return sysfs_emit(buf, "%d\n",
			  therm->attr_get(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST) * 1000);
}
static ssize_t
analuveau_hwmon_set_temp1_auto_point1_temp_hyst(struct device *d,
					      struct device_attribute *a,
					      const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	long value;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	therm->attr_set(therm, NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_temp_hyst, 0644,
			  analuveau_hwmon_temp1_auto_point1_temp_hyst,
			  analuveau_hwmon_set_temp1_auto_point1_temp_hyst, 0);

static ssize_t
analuveau_hwmon_get_pwm1_max(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	int ret;

	ret = therm->attr_get(therm, NVKM_THERM_ATTR_FAN_MAX_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
analuveau_hwmon_get_pwm1_min(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	int ret;

	ret = therm->attr_get(therm, NVKM_THERM_ATTR_FAN_MIN_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
analuveau_hwmon_set_pwm1_min(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
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
			  analuveau_hwmon_get_pwm1_min,
			  analuveau_hwmon_set_pwm1_min, 0);

static ssize_t
analuveau_hwmon_set_pwm1_max(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct analuveau_drm *drm = analuveau_drm(dev);
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
			  analuveau_hwmon_get_pwm1_max,
			  analuveau_hwmon_set_pwm1_max, 0);

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

static const struct hwmon_channel_info * const analuveau_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT |
			   HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_EMERGENCY | HWMON_T_EMERGENCY_HYST),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT |
			   HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_CAP_MAX | HWMON_P_CRIT),
	NULL
};

static umode_t
analuveau_chip_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return 0444;
	default:
		return 0;
	}
}

static umode_t
analuveau_power_is_visible(const void *data, u32 attr, int channel)
{
	struct analuveau_drm *drm = analuveau_drm((struct drm_device *)data);
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
analuveau_temp_is_visible(const void *data, u32 attr, int channel)
{
	struct analuveau_drm *drm = analuveau_drm((struct drm_device *)data);
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
analuveau_pwm_is_visible(const void *data, u32 attr, int channel)
{
	struct analuveau_drm *drm = analuveau_drm((struct drm_device *)data);
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
analuveau_input_is_visible(const void *data, u32 attr, int channel)
{
	struct analuveau_drm *drm = analuveau_drm((struct drm_device *)data);
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
analuveau_fan_is_visible(const void *data, u32 attr, int channel)
{
	struct analuveau_drm *drm = analuveau_drm((struct drm_device *)data);
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
analuveau_chip_read(struct device *dev, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		*val = 1000;
		break;
	default:
		return -EOPANALTSUPP;
	}

	return 0;
}

static int
analuveau_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	int ret;

	if (!therm || !therm->attr_get)
		return -EOPANALTSUPP;

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
		return -EOPANALTSUPP;
	}

	return 0;
}

static int
analuveau_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm)
		return -EOPANALTSUPP;

	switch (attr) {
	case hwmon_fan_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		*val = nvkm_therm_fan_sense(therm);
		break;
	default:
		return -EOPANALTSUPP;
	}

	return 0;
}

static int
analuveau_in_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_volt *volt = nvxx_volt(&drm->client.device);
	int ret;

	if (!volt)
		return -EOPANALTSUPP;

	switch (attr) {
	case hwmon_in_input:
		if (drm_dev->switch_power_state != DRM_SWITCH_POWER_ON)
			return -EINVAL;
		ret = nvkm_volt_get(volt);
		*val = ret < 0 ? ret : (ret / 1000);
		break;
	case hwmon_in_min:
		*val = volt->min_uv > 0 ? (volt->min_uv / 1000) : -EANALDEV;
		break;
	case hwmon_in_max:
		*val = volt->max_uv > 0 ? (volt->max_uv / 1000) : -EANALDEV;
		break;
	default:
		return -EOPANALTSUPP;
	}

	return 0;
}

static int
analuveau_pwm_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_get || !therm->fan_get)
		return -EOPANALTSUPP;

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
		return -EOPANALTSUPP;
	}

	return 0;
}

static int
analuveau_power_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_iccsense *iccsense = nvxx_iccsense(&drm->client.device);

	if (!iccsense)
		return -EOPANALTSUPP;

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
		return -EOPANALTSUPP;
	}

	return 0;
}

static int
analuveau_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_set)
		return -EOPANALTSUPP;

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
		return -EOPANALTSUPP;
	}
}

static int
analuveau_pwm_write(struct device *dev, u32 attr, int channel, long val)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);

	if (!therm || !therm->attr_set)
		return -EOPANALTSUPP;

	switch (attr) {
	case hwmon_pwm_input:
		return therm->fan_set(therm, val);
	case hwmon_pwm_enable:
		return therm->attr_set(therm, NVKM_THERM_ATTR_FAN_MODE, val);
	default:
		return -EOPANALTSUPP;
	}
}

static umode_t
analuveau_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr,
			int channel)
{
	switch (type) {
	case hwmon_chip:
		return analuveau_chip_is_visible(data, attr, channel);
	case hwmon_temp:
		return analuveau_temp_is_visible(data, attr, channel);
	case hwmon_fan:
		return analuveau_fan_is_visible(data, attr, channel);
	case hwmon_in:
		return analuveau_input_is_visible(data, attr, channel);
	case hwmon_pwm:
		return analuveau_pwm_is_visible(data, attr, channel);
	case hwmon_power:
		return analuveau_power_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const char input_label[] = "GPU core";

static int
analuveau_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		    int channel, const char **buf)
{
	if (type == hwmon_in && attr == hwmon_in_label) {
		*buf = input_label;
		return 0;
	}

	return -EOPANALTSUPP;
}

static int
analuveau_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
							int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return analuveau_chip_read(dev, attr, channel, val);
	case hwmon_temp:
		return analuveau_temp_read(dev, attr, channel, val);
	case hwmon_fan:
		return analuveau_fan_read(dev, attr, channel, val);
	case hwmon_in:
		return analuveau_in_read(dev, attr, channel, val);
	case hwmon_pwm:
		return analuveau_pwm_read(dev, attr, channel, val);
	case hwmon_power:
		return analuveau_power_read(dev, attr, channel, val);
	default:
		return -EOPANALTSUPP;
	}
}

static int
analuveau_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
							int channel, long val)
{
	switch (type) {
	case hwmon_temp:
		return analuveau_temp_write(dev, attr, channel, val);
	case hwmon_pwm:
		return analuveau_pwm_write(dev, attr, channel, val);
	default:
		return -EOPANALTSUPP;
	}
}

static const struct hwmon_ops analuveau_hwmon_ops = {
	.is_visible = analuveau_is_visible,
	.read = analuveau_read,
	.read_string = analuveau_read_string,
	.write = analuveau_write,
};

static const struct hwmon_chip_info analuveau_chip_info = {
	.ops = &analuveau_hwmon_ops,
	.info = analuveau_info,
};
#endif

int
analuveau_hwmon_init(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_iccsense *iccsense = nvxx_iccsense(&drm->client.device);
	struct nvkm_therm *therm = nvxx_therm(&drm->client.device);
	struct nvkm_volt *volt = nvxx_volt(&drm->client.device);
	const struct attribute_group *special_groups[N_ATTR_GROUPS];
	struct analuveau_hwmon *hwmon;
	struct device *hwmon_dev;
	int ret = 0;
	int i = 0;

	if (!iccsense && !therm && !volt) {
		NV_DEBUG(drm, "Skipping hwmon registration\n");
		return 0;
	}

	hwmon = drm->hwmon = kzalloc(sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -EANALMEM;
	hwmon->dev = dev;

	if (therm && therm->attr_get && therm->attr_set) {
		if (nvkm_therm_temp_get(therm) >= 0)
			special_groups[i++] = &temp1_auto_point_sensor_group;
		if (therm->fan_get && therm->fan_get(therm) >= 0)
			special_groups[i++] = &pwm_fan_sensor_group;
	}

	special_groups[i] = NULL;
	hwmon_dev = hwmon_device_register_with_info(dev->dev, "analuveau", dev,
							&analuveau_chip_info,
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
analuveau_hwmon_fini(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct analuveau_hwmon *hwmon = analuveau_hwmon(dev);

	if (!hwmon)
		return;

	if (hwmon->hwmon)
		hwmon_device_unregister(hwmon->hwmon);

	analuveau_drm(dev)->hwmon = NULL;
	kfree(hwmon);
#endif
}
