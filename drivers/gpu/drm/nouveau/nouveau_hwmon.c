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

#include <drm/drmP.h>

#include "nouveau_drm.h"
#include "nouveau_hwmon.h"

#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
static ssize_t
nouveau_hwmon_show_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	int temp = therm->temp_get(therm);

	if (temp < 0)
		return temp;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp * 1000);
}
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, nouveau_hwmon_show_temp,
						  NULL, 0);

static ssize_t
nouveau_hwmon_show_temp1_auto_point1_pwm(struct device *d,
					 struct device_attribute *a, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 100);
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_pwm, S_IRUGO,
			  nouveau_hwmon_show_temp1_auto_point1_pwm, NULL, 0);

static ssize_t
nouveau_hwmon_temp1_auto_point1_temp(struct device *d,
				     struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	      therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_FAN_BOOST) * 1000);
}
static ssize_t
nouveau_hwmon_set_temp1_auto_point1_temp(struct device *d,
					 struct device_attribute *a,
					 const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_FAN_BOOST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_temp, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_temp1_auto_point1_temp,
			  nouveau_hwmon_set_temp1_auto_point1_temp, 0);

static ssize_t
nouveau_hwmon_temp1_auto_point1_temp_hyst(struct device *d,
					  struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	 therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_FAN_BOOST_HYST) * 1000);
}
static ssize_t
nouveau_hwmon_set_temp1_auto_point1_temp_hyst(struct device *d,
					      struct device_attribute *a,
					      const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_FAN_BOOST_HYST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_auto_point1_temp_hyst, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_temp1_auto_point1_temp_hyst,
			  nouveau_hwmon_set_temp1_auto_point1_temp_hyst, 0);

static ssize_t
nouveau_hwmon_max_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	       therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_DOWN_CLK) * 1000);
}
static ssize_t
nouveau_hwmon_set_max_temp(struct device *d, struct device_attribute *a,
						const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_DOWN_CLK, value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR, nouveau_hwmon_max_temp,
						  nouveau_hwmon_set_max_temp,
						  0);

static ssize_t
nouveau_hwmon_max_temp_hyst(struct device *d, struct device_attribute *a,
			    char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	  therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_DOWN_CLK_HYST) * 1000);
}
static ssize_t
nouveau_hwmon_set_max_temp_hyst(struct device *d, struct device_attribute *a,
						const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_DOWN_CLK_HYST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_max_temp_hyst,
			  nouveau_hwmon_set_max_temp_hyst, 0);

static ssize_t
nouveau_hwmon_critical_temp(struct device *d, struct device_attribute *a,
							char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	       therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_CRITICAL) * 1000);
}
static ssize_t
nouveau_hwmon_set_critical_temp(struct device *d, struct device_attribute *a,
							    const char *buf,
								size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_CRITICAL, value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO | S_IWUSR,
						nouveau_hwmon_critical_temp,
						nouveau_hwmon_set_critical_temp,
						0);

static ssize_t
nouveau_hwmon_critical_temp_hyst(struct device *d, struct device_attribute *a,
							char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	  therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_CRITICAL_HYST) * 1000);
}
static ssize_t
nouveau_hwmon_set_critical_temp_hyst(struct device *d,
				     struct device_attribute *a,
				     const char *buf,
				     size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_CRITICAL_HYST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_critical_temp_hyst,
			  nouveau_hwmon_set_critical_temp_hyst, 0);
static ssize_t
nouveau_hwmon_emergency_temp(struct device *d, struct device_attribute *a,
							char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	       therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_SHUTDOWN) * 1000);
}
static ssize_t
nouveau_hwmon_set_emergency_temp(struct device *d, struct device_attribute *a,
							    const char *buf,
								size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_SHUTDOWN, value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_emergency, S_IRUGO | S_IWUSR,
					nouveau_hwmon_emergency_temp,
					nouveau_hwmon_set_emergency_temp,
					0);

static ssize_t
nouveau_hwmon_emergency_temp_hyst(struct device *d, struct device_attribute *a,
							char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	  therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_SHUTDOWN_HYST) * 1000);
}
static ssize_t
nouveau_hwmon_set_emergency_temp_hyst(struct device *d,
				      struct device_attribute *a,
				      const char *buf,
				      size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_SHUTDOWN_HYST,
			value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_emergency_hyst, S_IRUGO | S_IWUSR,
					nouveau_hwmon_emergency_temp_hyst,
					nouveau_hwmon_set_emergency_temp_hyst,
					0);

static ssize_t nouveau_hwmon_show_name(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "nouveau\n");
}
static SENSOR_DEVICE_ATTR(name, S_IRUGO, nouveau_hwmon_show_name, NULL, 0);

static ssize_t nouveau_hwmon_show_update_rate(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "1000\n");
}
static SENSOR_DEVICE_ATTR(update_rate, S_IRUGO,
						nouveau_hwmon_show_update_rate,
						NULL, 0);

static ssize_t
nouveau_hwmon_show_fan1_input(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n", therm->fan_sense(therm));
}
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, nouveau_hwmon_show_fan1_input,
			  NULL, 0);

 static ssize_t
nouveau_hwmon_get_pwm1_enable(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	int ret;

	ret = therm->attr_get(therm, NOUVEAU_THERM_ATTR_FAN_MODE);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1_enable(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;
	int ret;

	ret = kstrtol(buf, 10, &value);
	if (ret)
		return ret;

	ret = therm->attr_set(therm, NOUVEAU_THERM_ATTR_FAN_MODE, value);
	if (ret)
		return ret;
	else
		return count;
}
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1_enable,
			  nouveau_hwmon_set_pwm1_enable, 0);

static ssize_t
nouveau_hwmon_get_pwm1(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	int ret;

	ret = therm->fan_get(therm);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1(struct device *d, struct device_attribute *a,
		       const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	int ret = -ENODEV;
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->fan_set(therm, value);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1,
			  nouveau_hwmon_set_pwm1, 0);

static ssize_t
nouveau_hwmon_get_pwm1_min(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	int ret;

	ret = therm->attr_get(therm, NOUVEAU_THERM_ATTR_FAN_MIN_DUTY);
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
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;
	int ret;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->attr_set(therm, NOUVEAU_THERM_ATTR_FAN_MIN_DUTY, value);
	if (ret < 0)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm1_min, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1_min,
			  nouveau_hwmon_set_pwm1_min, 0);

static ssize_t
nouveau_hwmon_get_pwm1_max(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	int ret;

	ret = therm->attr_get(therm, NOUVEAU_THERM_ATTR_FAN_MAX_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1_max(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	long value;
	int ret;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->attr_set(therm, NOUVEAU_THERM_ATTR_FAN_MAX_DUTY, value);
	if (ret < 0)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm1_max, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1_max,
			  nouveau_hwmon_set_pwm1_max, 0);

static struct attribute *hwmon_default_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_update_rate.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_temp_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency_hyst.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_fan_rpm_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_pwm_fan_attributes[] = {
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_max.dev_attr.attr,
	NULL
};

static const struct attribute_group hwmon_default_attrgroup = {
	.attrs = hwmon_default_attributes,
};
static const struct attribute_group hwmon_temp_attrgroup = {
	.attrs = hwmon_temp_attributes,
};
static const struct attribute_group hwmon_fan_rpm_attrgroup = {
	.attrs = hwmon_fan_rpm_attributes,
};
static const struct attribute_group hwmon_pwm_fan_attrgroup = {
	.attrs = hwmon_pwm_fan_attributes,
};
#endif

int
nouveau_hwmon_init(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nvkm_therm(&drm->device);
	struct nouveau_hwmon *hwmon;
	struct device *hwmon_dev;
	int ret = 0;

	hwmon = drm->hwmon = kzalloc(sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;
	hwmon->dev = dev;

	if (!therm || !therm->temp_get || !therm->attr_get || !therm->attr_set)
		return -ENODEV;

	hwmon_dev = hwmon_device_register(&dev->pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		NV_ERROR(drm, "Unable to register hwmon device: %d\n", ret);
		return ret;
	}
	dev_set_drvdata(hwmon_dev, dev);

	/* set the default attributes */
	ret = sysfs_create_group(&hwmon_dev->kobj, &hwmon_default_attrgroup);
	if (ret)
		goto error;

	/* if the card has a working thermal sensor */
	if (therm->temp_get(therm) >= 0) {
		ret = sysfs_create_group(&hwmon_dev->kobj, &hwmon_temp_attrgroup);
		if (ret)
			goto error;
	}

	/* if the card has a pwm fan */
	/*XXX: incorrect, need better detection for this, some boards have
	 *     the gpio entries for pwm fan control even when there's no
	 *     actual fan connected to it... therm table? */
	if (therm->fan_get && therm->fan_get(therm) >= 0) {
		ret = sysfs_create_group(&hwmon_dev->kobj,
					 &hwmon_pwm_fan_attrgroup);
		if (ret)
			goto error;
	}

	/* if the card can read the fan rpm */
	if (therm->fan_sense(therm) >= 0) {
		ret = sysfs_create_group(&hwmon_dev->kobj,
					 &hwmon_fan_rpm_attrgroup);
		if (ret)
			goto error;
	}

	hwmon->hwmon = hwmon_dev;

	return 0;

error:
	NV_ERROR(drm, "Unable to create some hwmon sysfs files: %d\n", ret);
	hwmon_device_unregister(hwmon_dev);
	hwmon->hwmon = NULL;
	return ret;
#else
	return 0;
#endif
}

void
nouveau_hwmon_fini(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_hwmon *hwmon = nouveau_hwmon(dev);

	if (hwmon->hwmon) {
		sysfs_remove_group(&hwmon->hwmon->kobj, &hwmon_default_attrgroup);
		sysfs_remove_group(&hwmon->hwmon->kobj, &hwmon_temp_attrgroup);
		sysfs_remove_group(&hwmon->hwmon->kobj, &hwmon_pwm_fan_attrgroup);
		sysfs_remove_group(&hwmon->hwmon->kobj, &hwmon_fan_rpm_attrgroup);

		hwmon_device_unregister(hwmon->hwmon);
	}

	nouveau_drm(dev)->hwmon = NULL;
	kfree(hwmon);
#endif
}
